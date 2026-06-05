#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <regex.h>
#include <time.h>
#include <mbedtls/base64.h>
#include <sys/param.h>

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "cJSON.h"
#include "esp_check.h"
#include "sensecap-watcher.h"

#include "data_defs.h"
#include "event_loops.h"
#include "tf.h"
#include "app_time.h"
#include "app_wifi.h"
#include "app_ble.h"
#include "uhash.h"
#include "at_cmd.h"
#include "app_device_info.h"
#include "util.h"
#include "storage.h"
#include "app_png.h"


#define AT_CMD_BUFFER_LEN_STEP   (1024 * 100)  // the growing step of the size of at cmd buffer
#define AT_CMD_BUFFER_MAX_LEN    (1024 * 500)  // top limit of the at cmd buffer size, don't be too huge
#define BLE_MSG_Q_SIZE            10


/*------------------system basic DS-----------------------------------------------------*/
const char *TAG = "at_cmd";
const char *pattern = "^AT\\+([a-zA-Z0-9]+)(\\?|=(\\{.*\\}))?\r\n$";
command_entry *commands = NULL; // Global variable to store the commands
static at_cmd_buffer_t g_at_cmd_buffer = {.buff = NULL, .cap = AT_CMD_BUFFER_LEN_STEP, .wr_ptr = 0};
QueueHandle_t ble_msg_queue;

/*------------------network DS----------------------------------------------------------*/
SemaphoreHandle_t wifi_stack_semaphore;
static int network_connect_flag;
static wifi_ap_record_t current_connected_wifi;
// static int task_flow_resp;
static struct view_data_taskflow_status taskflow_status;
static struct view_data_ota_status  ai_model_ota_status;

SemaphoreHandle_t semaphorewificonnected;
SemaphoreHandle_t semaphorewifidisconnected;

SemaphoreHandle_t xBinarySemaphore_wifitable;

/*-----------------------------------bind index--------------------------------------------*/
static int bind_index;

/*----------------------------------------------------------------------------------------*/

static SemaphoreHandle_t data_sem_handle = NULL;

static void __data_lock(void)
{
    if( data_sem_handle == NULL ) {
        return;
    }
    xSemaphoreTake(data_sem_handle, portMAX_DELAY);
}
static void __data_unlock(void)
{
    if( data_sem_handle == NULL ) {
        return;
    }
    xSemaphoreGive(data_sem_handle);  
}

/**
 * @brief Initialize the Wi-Fi stack semaphore.
 *
 * This function creates a mutex semaphore for the Wi-Fi stack.
 * A semaphore is a synchronization primitive used to control access
 * to a shared resource in a concurrent system such as a multitasking
 * operating system. In this case, the semaphore is used to manage
 * access to the Wi-Fi stack to ensure thread safety.
 */
void wifi_stack_semaphore_init()
{
    wifi_stack_semaphore = xSemaphoreCreateMutex();
}

/**
 * @brief Initialize the Wi-Fi stack with a specified capacity.
 *
 * This function initializes a Wi-Fi stack by allocating memory for the stack entries
 * and setting its initial size and capacity. The memory is allocated from a specific
 * heap region suitable for large allocations.
 *
 * @param stack A pointer to the Wi-Fi stack structure to be initialized.
 * @param capacity The maximum number of entries the Wi-Fi stack can hold.
 */
void initWiFiStack(WiFiStack *stack, int capacity)
{
    stack->entries = (WiFiEntry *)psram_calloc(1, capacity * sizeof(WiFiEntry));
    stack->size = 0;
    stack->capacity = capacity;
}

/**
 * @brief Push a new Wi-Fi entry onto the Wi-Fi stack.
 *
 * This function adds a new Wi-Fi entry to the stack. It ensures thread safety
 * by using a semaphore to protect the critical section where the stack is modified.
 * If the stack's capacity is exceeded, the stack's capacity is doubled and the memory
 * for the entries is reallocated.
 *
 * @param stack A pointer to the Wi-Fi stack where the entry will be added.
 * @param entry The Wi-Fi entry to be added to the stack.
 */
void pushWiFiStack(WiFiStack *stack, WiFiEntry entry)
{
    // Acquire the semaphore to protect the critical section
    xSemaphoreTake(wifi_stack_semaphore, portMAX_DELAY);

    if (stack->size >= stack->capacity)
    {
        return;
    }
    stack->entries[stack->size++] = entry;

    // Release the semaphore to allow other tasks to access the critical section
    xSemaphoreGive(wifi_stack_semaphore);
}

/**
 * @brief Free the memory allocated for the Wi-Fi stack.
 *
 * This function releases the memory allocated for the Wi-Fi stack entries and
 * resets the stack's attributes to indicate that it is empty and uninitialized.
 *
 * @param stack A pointer to the Wi-Fi stack to be freed.
 */
void freeWiFiStack(WiFiStack *stack)
{
    free(stack->entries);
    stack->entries = NULL;
    stack->size = 0;
    stack->capacity = 0;
}

void resetWiFiStack(WiFiStack *stack)
{
    if (stack && stack->entries && stack->capacity > 0)
    {
        if (stack->size > 0)
        {
            for (size_t i = 0; i < stack->size; i++)
            {
                WiFiEntry *wifi = &stack->entries[i];
                // they're all from strdup, need to be freed
                free(wifi->ssid);
                free(wifi->rssi);
                free(wifi->encryption);
            }
        }
        stack->entries = memset(stack->entries, 0, stack->capacity * sizeof(WiFiEntry));
        stack->size = 0;
    }
}

/**
 * @brief Create a JSON object representing a Wi-Fi entry.
 *
 * This function creates a JSON object from a given Wi-Fi entry. The JSON object
 * contains the SSID, RSSI, and encryption type of the Wi-Fi entry.
 *
 * @param entry A pointer to the Wi-Fi entry to be converted to JSON.
 * @return A cJSON object representing the Wi-Fi entry.
 */
cJSON *create_wifi_entry_json(WiFiEntry *entry)
{
    cJSON *wifi_json = cJSON_CreateObject();
    cJSON_AddStringToObject(wifi_json, "ssid", entry->ssid);
    cJSON_AddStringToObject(wifi_json, "rssi", entry->rssi);
    cJSON_AddStringToObject(wifi_json, "encryption", entry->encryption);
    return wifi_json;
}

/**
 * @brief Create a JSON object representing the scanned and connected Wi-Fi stacks.
 *
 * This function creates a JSON object that contains two arrays: one for the
 * scanned Wi-Fi entries and another for the connected Wi-Fi entries.
 *
 * @param stack_scnned_wifi A pointer to the Wi-Fi stack containing scanned Wi-Fi entries.
 * @param stack_connected_wifi A pointer to the Wi-Fi stack containing connected Wi-Fi entries.
 * @return A cJSON object representing both the scanned and connected Wi-Fi stacks.
 */
cJSON *create_wifi_stack_json(WiFiStack *stack_scnned_wifi, WiFiStack *stack_connected_wifi)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *scanned_array = cJSON_CreateArray();
    cJSON *connected_array = cJSON_CreateArray();
    for (int i = 0; i < stack_connected_wifi->size; i++)
    {
        cJSON_AddItemToArray(connected_array, create_wifi_entry_json(&stack_connected_wifi->entries[i]));
    }

    for (int i = 0; i < stack_scnned_wifi->size; i++)
    {
        cJSON_AddItemToArray(scanned_array, create_wifi_entry_json(&stack_scnned_wifi->entries[i]));
    }
    cJSON_AddItemToObject(root, "connected_wifi", connected_array);
    cJSON_AddItemToObject(root, "scanned_wifi", scanned_array);
    resetWiFiStack(&wifiStack_scanned);
    resetWiFiStack(&wifiStack_connected);

    return root;
}

// AT command system
/*----------------------------------------------------------------------------------------------------*/

/**
 * @brief Creates an AT command response by appending a standard suffix to the given message.
 *
 * This function takes a message string, appends the standard suffix "\r\nok\r\n" to it,
 * and allocates memory for the complete response. It returns an AT_Response structure
 * containing the formatted response and its length.
 *
 * @param message A constant character pointer to the message to be included in the response.
 *                If the message is NULL, an empty response is created.
 * @return AT_Response A structure containing the formatted response string and its length.
 */
esp_err_t send_at_response(const char *message)
{
    esp_err_t ret = ESP_FAIL;
    char *response = NULL;

    if (message)
    {
        const char *suffix = "\r\nok\r\n";
        size_t total_length = strlen(message) + strlen(suffix) + 10; // a few bytes more
        response = psram_calloc(1, total_length);
        if (response)
        {
            strcpy(response, message);
            strcat(response, suffix);
            size_t newlen = strlen(response);

            ret = app_ble_send_indicate((uint8_t *)response, newlen);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "Failed to send AT response, ret=%d", ret);
            }
        }
    }

    if (response)
        free(response);
    return ret;
}

/**
 * @brief Add a command to the hash table of commands.
 *
 * This function creates a new command entry and adds it to the hash table of commands.
 *
 * @param commands A pointer to the hash table of commands.
 * @param name The name of the command.
 * @param func A pointer to the function that implements the command.
 */
void add_command(command_entry **commands, const char *name, at_cmd_error_code (*func)(char *params))
{
    // command_entry *entry = (command_entry *)malloc(sizeof(command_entry)); // Allocate memory for the new entry
    command_entry *entry = (command_entry *)heap_caps_malloc(sizeof(command_entry), MALLOC_CAP_SPIRAM);
    strcpy(entry->command_name, name);            // Copy the command name to the new entry
    entry->func = func;                           // Assign the function pointer to the new entry
    HASH_ADD_STR(*commands, command_name, entry); // Add the new entry to the hash table
}

/**
 * @brief Execute a command from the hash table.
 *
 * This function searches for a command by name in the hash table and executes it
 * with the provided parameters. If the query character is '?', the command is treated
 * as a query command.
 *
 * @param commands A pointer to the hash table of commands.
 * @param name The name of the command to execute.
 * @param params The parameters to pass to the command function.
 * @param query The query character that modifies the command behavior.
 */
void exec_command(command_entry **commands, const char *name, char *params, char query)
{
    at_cmd_error_code error_code=ESP_OK;
    command_entry *entry;
    char full_command[128];
    snprintf(full_command, sizeof(full_command), "%s%c", name, query); // Append the query character to the command name
    ESP_LOGD(TAG, "full_command: %s", full_command);
    HASH_FIND_STR(*commands, full_command, entry);
    if (entry)
    {
        if (query == '?') // If the query character is '?', then the command is a query command
        {
            error_code = entry->func(NULL);
        }
        else
        {
            error_code = entry->func(params);
        }
    }
    else
    {
        ESP_LOGI(TAG, "Command not found\n");
        cJSON *root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "name", "Command_not_found");
        cJSON_AddNumberToObject(root, "code", ERROR_CMD_COMMAND_NOT_FOUND);
        char *json_string = cJSON_Print(root);
        ESP_LOGE(TAG, "JSON String: %s\n", json_string);
        send_at_response(json_string);
        cJSON_Delete(root);
        free(json_string);
    }
    if (error_code != ESP_OK)
    {
        ESP_LOGE(TAG, "Error code: %d\n", error_code);
        ESP_LOGI(TAG, "Commond exec failed \n");
        cJSON *root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "name", "Commond_exec_failed");
        cJSON_AddNumberToObject(root, "code", error_code);
        char *json_string = cJSON_Print(root);
        ESP_LOGD(TAG, "JSON String: %s\n", json_string);
        send_at_response(json_string);
        cJSON_Delete(root);
        free(json_string);
    }
}

/**
 * @brief Register the AT commands.
 *
 * This function adds various AT commands to the hash table of commands.
 */
void AT_command_reg()
{
    // Register the AT commands
    add_command(&commands, "deviceinfo?", handle_deviceinfo_command);
    add_command(&commands, "devicecfg=", handle_deviceinfo_cfg_command);  //why `devicecfg=` ? this would be history problem
    add_command(&commands, "wifi=", handle_wifi_set);
    add_command(&commands, "wifi?", handle_wifi_query);
    add_command(&commands, "wifitable?", handle_wifi_table);
    add_command(&commands, "taskflow?", handle_taskflow_query_command);
    add_command(&commands, "taskflow=", handle_taskflow_command);
    add_command(&commands, "taskflowinfo?", handle_taskflow_info_query_command);
    add_command(&commands, "cloudservice=", handle_cloud_service_command);
    add_command(&commands, "cloudservice?", handle_cloud_service_query_command);
    add_command(&commands, "emoji=", handle_emoji_command);
    add_command(&commands, "bind=", handle_bind_command);
    add_command(&commands, "localservice?", handle_localservice_query);
    add_command(&commands, "localservice=", handle_localservice_set);
}

/**
 * @brief Frees all allocated memory for AT command entries in the hash table.
 *
 * This function iterates over all command entries in the hash table, deletes each entry from the hash table,
 * and frees the allocated memory for each command entry.
 */
void AT_command_free()
{
    command_entry *current_command, *tmp;
    HASH_ITER(hh, commands, current_command, tmp)
    {
        HASH_DEL(commands, current_command); // Delete the entry from the hash table
        free(current_command);
    }
}

/**
 * @brief Handle the bind command by parsing input parameters, extracting the bind index,
 *        posting an event to the app event loop, and creating a JSON response.
 *
 * This function takes a JSON string as input, parses it to extract a "code" value, and posts
 * an event with this value to the application event loop. It then creates a JSON response
 * indicating the success of the bind command and sends this response.
 *
 * @param params JSON string containing the "code" key with an integer value to bind.
 */
at_cmd_error_code handle_bind_command(char *params)
{
    ESP_LOGI(TAG, "handle_bind_command\n");
    cJSON *json = cJSON_Parse(params);
    if (json == NULL)
    {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL)
        {
            ESP_LOGE(TAG, "Error before: %s\n", error_ptr);
        }
        return ERROR_CMD_JSON_PARSE;
    }

    cJSON *data = cJSON_GetObjectItemCaseSensitive(json, "code");
    bind_index = data->valueint;
    esp_err_t event_post_err = esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_CONFIG_SYNC, &bind_index, sizeof(bind_index), portMAX_DELAY);
    if (event_post_err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to post event to app event loop\n");
        cJSON_Delete(json);
        return ERROR_CMD_EVENT_POST;
    }

    ESP_LOGI(TAG, "bind_index: %d\n", bind_index);

    cJSON_Delete(json);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    cJSON *root = cJSON_CreateObject();
    if (root == NULL)
    {
        ESP_LOGE(TAG, "Failed to create JSON object\n");
        return ERROR_CMD_JSON_CREATE;
    }
    cJSON_AddStringToObject(root, "name", "bind");
    cJSON_AddNumberToObject(root, "code", 0);
    char *json_string = cJSON_Print(root);
    ESP_LOGD(TAG, "JSON String: %s\n", json_string);
    esp_err_t send_result = send_at_response(json_string);
    if (send_result != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to send AT response\n");
        cJSON_Delete(root);
        free(json_string);
        return ERROR_CMD_RESPONSE;
    }
    cJSON_Delete(root);
    free(json_string);
    return AT_CMD_SUCCESS;
}

/*-----------------------------------------------------------------------------------------------------------*/
at_cmd_error_code handle_emoji_command(char *params)
{
    esp_err_t ret = ESP_OK;

    ESP_LOGI(TAG, "handle_emoji_command\n");

    cJSON *json = cJSON_Parse(params);
    if (json == NULL)
    {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL)
        {
            ESP_LOGE(TAG, "handle_emoji_command, parse error: %s\n", error_ptr);
        }
        return ERROR_CMD_JSON_PARSE;
    }

    // Get the "filename" from the JSON
    cJSON *filename_item = cJSON_GetObjectItem(json, "filename");
    if (!filename_item || !cJSON_IsString(filename_item))
    {
        ESP_LOGE(TAG, "handle_emoji_command, Error: 'filename' is not found or not a string\n");
        ret = ERROR_CMD_PARAM_RANGE;
        goto handle_emoji_err0;
    }

    // Get the "urls" array from the JSON
    cJSON *urls_array = cJSON_GetObjectItem(json, "urls");
    if (urls_array == NULL || !cJSON_IsArray(urls_array))
    {
        ESP_LOGE(TAG, "handle_emoji_command, Error: 'urls' is not an array\n");
        ret = ERROR_CMD_PARAM_RANGE;
        goto handle_emoji_err0;
    }

    // Extract each URL and copy to the urls array
    int url_count = cJSON_GetArraySize(urls_array);
    if (url_count > MAX_IMAGES) {
        ESP_LOGE(TAG, "handle_emoji_command, too many urls (%d)", url_count);
        ret = ERROR_CMD_PARAM_RANGE;
        goto handle_emoji_err0;
    }
    for (int i = 0; i < url_count; i++)
    {
        cJSON *url_item = cJSON_GetArrayItem(urls_array, i);
        if (!cJSON_IsString(url_item) || strlen(url_item->valuestring) == 0)
        {
            ESP_LOGE(TAG, "handle_emoji_command, Error: URL %d is not a string", i);
            ret = ERROR_CMD_JSON_TYPE;
            goto handle_emoji_err0;
        }
    }

    // validation done, now download
    download_result_t *results = psram_calloc(url_count, sizeof(download_result_t));
    download_summary_t *summary = psram_calloc(1, sizeof(download_summary_t));
    summary->results = results;
    ret = download_emoji_images(summary, filename_item, urls_array, url_count);

    // Create the root JSON object
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "name", "emoji");

    // Create a JSON array to store the result codes
    cJSON *code_array = cJSON_CreateArray();

    for (int i = 0; i < url_count; i++)
    {
        if (ret != ESP_OK) {
            cJSON_AddItemToArray(code_array, cJSON_CreateNumber(ret));
        }
        else if (summary->results[i].success)
        {
            ESP_LOGI(TAG, "Emoji %d downloaded successfully", i);
            cJSON_AddItemToArray(code_array, cJSON_CreateNumber(0));
        }
        else
        {
            ESP_LOGE(TAG, "Failed to download emoji %d, error code: %d", i, summary->results[i].error_code);
            cJSON_AddItemToArray(code_array, cJSON_CreateNumber(summary->results[i].error_code));
        }
    }

    cJSON_AddItemToObject(root, "data", code_array);

    // Convert the root JSON object to a string and send the response
    char *json_string = cJSON_Print(root);
    ESP_LOGD(TAG, "JSON String: %s\n", json_string);
    ret = ESP_OK;
    esp_err_t send_result = send_at_response(json_string);
    if (send_result != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to send AT response\n");
        ret = ERROR_CMD_RESPONSE;
    }

    // Clean up
    free(results);
    free(summary);
    free(json_string);
    cJSON_Delete(root);
    cJSON_Delete(json);
    return ret; // Return success

handle_emoji_err0:
    if (json) cJSON_Delete(json);
    return ret;
}

/*-----------------------------------------------------------------------------------------------------------*/

/**
 * @brief Handle the cloud service query command by retrieving the cloud service switch state
 *        and creating a JSON response.
 *
 * This function retrieves the state of the cloud service switch and creates a JSON response
 * with the state information. The response is then sent to the requester.
 *
 * @param params Unused parameter in this function but kept for consistency in the command handler signature.
 */
at_cmd_error_code handle_cloud_service_query_command(char *params)
{
    (void)params; // Prevent unused parameter warning

    int cloud_service_switch = get_cloud_service_switch(AT_CMD_CALLER);
    if (cloud_service_switch < 0)
    {
        return ERROR_DATA_READ_FAIL;
    }

    ESP_LOGI(TAG, "Handling handle_cloud_service_query_command \n");

    cJSON *root = cJSON_CreateObject();
    if (root == NULL)
    {
        ESP_LOGE(TAG, "Failed to create JSON object\n");
        return ERROR_CMD_JSON_CREATE;
    }
    cJSON *data_rep = cJSON_CreateObject();
    if (data_rep == NULL)
    {
        ESP_LOGE(TAG, "Failed to create JSON object\n");
        cJSON_Delete(root);
        return ERROR_CMD_JSON_CREATE;
    }

    cJSON_AddStringToObject(root, "name", "cloudservice");
    cJSON_AddNumberToObject(root, "code", 0);
    cJSON_AddItemToObject(root, "data", data_rep);
    cJSON_AddNumberToObject(data_rep, "remotecontrol", cloud_service_switch);
    char *json_string = cJSON_Print(root);

    ESP_LOGD(TAG, "JSON String in cloud service query handle: %s\n", json_string);

    esp_err_t send_result = send_at_response(json_string);
    if (send_result != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to send AT response\n");
        cJSON_Delete(root);
        free(json_string);
        return ERROR_CMD_RESPONSE;
    }

    cJSON_Delete(root);
    free(json_string);

    return AT_CMD_SUCCESS;
}

/**
 * @brief Handle the cloud service command by parsing input parameters, updating the cloud service switch state,
 *        and creating a JSON response.
 *
 * This function takes a JSON string as input, parses it to extract the "remotecontrol" value, and updates
 * the cloud service switch state. It then creates a JSON response indicating the success of the operation
 * and sends this response.
 *
 * @param params JSON string containing the "data" key with a "remotecontrol" integer value.
 */
at_cmd_error_code handle_cloud_service_command(char *params)
{
    ESP_LOGI(TAG, "handle_cloud_service_command\n");
    cJSON *json = cJSON_Parse(params);
    if (json == NULL)
    {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL)
        {
            ESP_LOGE(TAG, "Error before: %s\n", error_ptr);
        }
        return ERROR_CMD_JSON_PARSE;
    }

    cJSON *data = cJSON_GetObjectItemCaseSensitive(json, "data");
    if (cJSON_IsObject(data))
    {
        cJSON *cloud_service = cJSON_GetObjectItemCaseSensitive(data, "remotecontrol");
        if (cJSON_IsNumber(cloud_service))
        {
            ESP_LOGI(TAG, "Cloud_Service: %d\n", cloud_service->valueint);
            esp_err_t set_cloud_service_switch_ret = set_cloud_service_switch(AT_CMD_CALLER, cloud_service->valueint);
            if (set_cloud_service_switch_ret != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to set cloud service switch\n");
                cJSON_Delete(json); // Delete the JSON object to prevent memory leak
                return ERROR_DATA_WRITE_FAIL;
            }
        }
        else
        {
            ESP_LOGE(TAG, "Cloud_Service not a valid number in JSON\n");
            cJSON_Delete(json);         // Delete the JSON object to prevent memory leak
            return ERROR_CMD_JSON_TYPE; // Return from the function as the JSON is not valid
        }
    }
    else
    {
        ESP_LOGE(TAG, "Cloud_Service not found or not a valid string in JSON\n");
        cJSON_Delete(json);         // Delete the JSON object to prevent memory leak
        return ERROR_CMD_JSON_TYPE; // Return from the function as the JSON is not valid
    }

    cJSON_Delete(json);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    cJSON *root = cJSON_CreateObject();
    if (root == NULL)
    {
        ESP_LOGE(TAG, "Failed to create JSON object\n");
        return ERROR_CMD_JSON_CREATE;
    }
    cJSON_AddStringToObject(root, "name", "cloudservice");
    cJSON_AddNumberToObject(root, "code", 0);
    char *json_string = cJSON_Print(root);
    ESP_LOGD(TAG, "JSON String: %s\n", json_string);
    esp_err_t send_result = send_at_response(json_string);
    if (send_result != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to send AT response\n");
        cJSON_Delete(root);
        free(json_string);
        return ERROR_CMD_RESPONSE;
    }

    cJSON_Delete(root);
    free(json_string);
    return AT_CMD_SUCCESS;
}

/**
 * @brief Handle the device configuration command.
 *
 * This function processes the device configuration command by parsing the JSON string
 * provided in the parameters, extracting the time zone information, and posting an event
 * with the time zone configuration. It also creates a JSON response with default values
 * for other settings and sends it back.
 *
 * @param params A JSON string containing the device configuration data.
 */
at_cmd_error_code handle_deviceinfo_cfg_command(char *params)
{
    ESP_LOGI(TAG, "handle_deviceinfo_cfg_command\n");
    int time_flag = 0;
    bool is_need_reset_shutdown = false;
    bool is_need_reset_reboot = false;
    bool is_need_shutdown = false;
    bool is_need_reboot = false;

    cJSON *json = cJSON_Parse(params);
    if (json == NULL)
    {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL)
        {
            ESP_LOGE(TAG, "Error before: %s\n", error_ptr);
        }
        return ERROR_CMD_JSON_PARSE;
    }

    cJSON *data = cJSON_GetObjectItemCaseSensitive(json, "data");
    if (cJSON_IsObject(data))
    {
        // Get the "Time_Zone" item
        bool timezone_valid = false;
        bool timestamp_valid = false;
        bool daylight_valid = false;
        int timezone = 0;
        int daylight = 0;
        long long int utc_timestamp = 0;

        cJSON *timezone_json = cJSON_GetObjectItemCaseSensitive(data, "timezone");
        if (cJSON_IsNumber(timezone_json))
        {
            timezone_valid = true;
            timezone = timezone_json->valueint;
        }
        else
        {
            ESP_LOGI(TAG, "Timezone not found or not a valid number in JSON\n");
        }
        cJSON *daylight_json = cJSON_GetObjectItemCaseSensitive(data, "daylight");
        if (cJSON_IsNumber(daylight_json))
        {
            daylight_valid = true;
            daylight = daylight_json->valueint;
        }
        else
        {
            ESP_LOGI(TAG, "Daylight not found or not a valid number in JSON\n");
        }
        cJSON *time = cJSON_GetObjectItemCaseSensitive(data, "timestamp");
        if (cJSON_IsString(time))
        {
            long long int value;
            char *time_str = time->valuestring;
            char *endptr;
            value = strtoll(time_str, &endptr, 10);
            if (endptr == time_str)
            {
                ESP_LOGE(TAG, "No digits were found\n");
            }
            else if (*endptr != '\0')
            {
                ESP_LOGE(TAG, "Further characters after number: %s\n", endptr);
            }
            else
            {
                timestamp_valid = true;
                utc_timestamp = value;
                ESP_LOGI(TAG, "The converted value is %lld\n", value);
            }
        }
        else
        {
            ESP_LOGI(TAG, "Timestamp not found or not a valid string in JSON\n");
        }
        if (timezone_valid || daylight_valid || timestamp_valid)
        {
            struct view_data_time_cfg time_cfg;
            memset(&time_cfg, 0, sizeof(time_cfg));
            app_time_cfg_get(&time_cfg);

            if (timezone_valid)
            {
                time_cfg.zone = timezone;
            }
            if (daylight_valid)
            {
                time_cfg.daylight = daylight;
            }
            if (timestamp_valid)
            {
                // auto_update flag don't change, device will update time automatically if it have network.
                time_cfg.time = utc_timestamp;
                time_cfg.set_time = true;
            }
            esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_TIME_CFG_APPLY, &time_cfg, sizeof(time_cfg), portMAX_DELAY);
        }
        else
        {
            ESP_LOGI(TAG, "Timezone or timestamp or daylight not found or not a valid string in JSON\n");
        }
        // get brightness item
        cJSON *brightness = cJSON_GetObjectItemCaseSensitive(data, "brightness");
        if (cJSON_IsNumber(brightness))
        {
            int brightness_value = brightness->valueint;

            if (brightness_value < 0 || brightness_value > 100)
            {
                ESP_LOGE(TAG, "Brightness value out of range\n");
                cJSON_Delete(json);
                return ERROR_CMD_PARAM_RANGE;
            }

            esp_err_t set_brightness_err = set_brightness(AT_CMD_CALLER, brightness_value);
            if (set_brightness_err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to set brightness\n");
                cJSON_Delete(json);
                return ERROR_DATA_WRITE_FAIL;
            }
        }

        // get rgb_switch item
        cJSON *rgbswitch = cJSON_GetObjectItemCaseSensitive(data, "rgbswitch");
        if (cJSON_IsNumber(rgbswitch))
        {
            int rgbswitch_value = rgbswitch->valueint;
            if (rgbswitch_value < 0 || rgbswitch_value > 1)
            {
                ESP_LOGE(TAG, "RGB switch value out of range\n");
                cJSON_Delete(json);
                return ERROR_CMD_PARAM_RANGE;
            }
            esp_err_t set_rgb_switch_err = set_rgb_switch(AT_CMD_CALLER, rgbswitch_value);
            if (set_rgb_switch_err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to set RGB switch\n");
                cJSON_Delete(json);
                return ERROR_DATA_WRITE_FAIL;
            }
        }
        else
        {
            ESP_LOGI(TAG, "RGB switch not found or not a valid number in JSON\n");
        }
        cJSON *soundvolume = cJSON_GetObjectItemCaseSensitive(data, "sound");
        if (cJSON_IsNumber(soundvolume))
        {
            int volume = soundvolume->valueint;
            if (volume < 0 || volume > 100)
            {
                ESP_LOGE(TAG, "Sound volume value out of range\n");
                cJSON_Delete(json);
                return ERROR_CMD_PARAM_RANGE;
            }
            esp_err_t set_sound_err = set_sound(AT_CMD_CALLER, volume);
            if (set_sound_err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to set sound volume\n");
                cJSON_Delete(json);
                return ERROR_DATA_WRITE_FAIL;
            }
        }
        else
        {
            ESP_LOGI(TAG, "Sound volume not found or not a valid number in JSON\n");
        }

        // set screenoff time
        cJSON *screenofftime = cJSON_GetObjectItemCaseSensitive(data, "screenofftime");
        if (cJSON_IsNumber(screenofftime))
        {
            int screenofftime_value = screenofftime->valueint;
            if (screenofftime_value < 0 || screenofftime_value > 6)
            {
                ESP_LOGE(TAG, "Screenoff time value out of range\n");
                cJSON_Delete(json);
                return ERROR_CMD_PARAM_RANGE;
            }
            esp_err_t set_screenoff_time_err = set_screenoff_time(AT_CMD_CALLER, screenofftime_value);
            if (set_screenoff_time_err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to set screenoff time\n");
                cJSON_Delete(json);
                return ERROR_DATA_WRITE_FAIL;
            }
        }
        else
        {
            ESP_LOGI(TAG, "Screenofftime not found or not a valid number in JSON\n");
        }

        // set screenoff switch
        cJSON *screenoffswitch = cJSON_GetObjectItemCaseSensitive(data, "screenoffswitch");
        if (cJSON_IsNumber(screenoffswitch))
        {
            int screenoffswitch_value = screenoffswitch->valueint;
            if (screenoffswitch_value < 0 || screenoffswitch_value > 1)
            {
                ESP_LOGE(TAG, "Screenoff switch value out of range\n");
                cJSON_Delete(json);
                return ERROR_CMD_PARAM_RANGE;
            }
            esp_err_t set_screenoff_switch_err = set_screenoff_switch(AT_CMD_CALLER, screenoffswitch_value);
            if (set_screenoff_switch_err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to set Screenoff switch\n");
                cJSON_Delete(json);
                return ERROR_DATA_WRITE_FAIL;
            }
        }
        else
        {
            ESP_LOGI(TAG, "Screenoff switch not found or not a valid number in JSON\n");
        }

        cJSON *reset_flag = cJSON_GetObjectItemCaseSensitive(data, "reset");
        if (cJSON_IsNumber(reset_flag))
        {
            if ( reset_flag->valueint )
            {
                is_need_reset_reboot = true;
                ESP_LOGI(TAG, "Reset factory and reboot\n");
            }
        }

        cJSON *resetshutdown_flag = cJSON_GetObjectItemCaseSensitive(data, "resetshutdown");
        if (cJSON_IsNumber(resetshutdown_flag))
        {
            if (resetshutdown_flag->valueint)
            {
                is_need_reset_shutdown = true;
                ESP_LOGI(TAG, "Reset factory and shutdown\n");
            }
        }

        cJSON *json_reboot = cJSON_GetObjectItemCaseSensitive(data, "reboot");
        if (cJSON_IsNumber(json_reboot))
        {
            if ( json_reboot->valueint )
            {
                ESP_LOGI(TAG, "Reboot device\n");
                is_need_reboot = true;
            }
        }

        cJSON *json_shutdown = cJSON_GetObjectItemCaseSensitive(data, "shutdown");
        if (cJSON_IsNumber(json_shutdown))
        {
            if ( json_shutdown->valueint )
            {
                ESP_LOGI(TAG, "Shutdown device\n");
                is_need_shutdown = true;
            }
        }
    }
    else
    {
        ESP_LOGE(TAG, "failed at config json\n");
        cJSON_Delete(json);
        return ERROR_CMD_JSON_TYPE;
    }

    cJSON_Delete(json);
    vTaskDelay(1000 / portTICK_PERIOD_MS);

    cJSON *root = cJSON_CreateObject();
    if (root == NULL)
    {
        ESP_LOGE(TAG, "Failed to create JSON object\n");
        return ERROR_CMD_JSON_CREATE;
    }
    cJSON *data_rep = cJSON_CreateObject();
    if (data_rep == NULL)
    {
        ESP_LOGE(TAG, "Failed to create JSON object\n");
        cJSON_Delete(root);
        return ERROR_CMD_JSON_CREATE;
    }
    cJSON_AddStringToObject(root, "name", "devicecfg");
    cJSON_AddNumberToObject(root, "code", 0);
    char *json_string = cJSON_Print(root);
    ESP_LOGD(TAG, "JSON String in device cfg command: %s\n", json_string);
    esp_err_t send_result = send_at_response(json_string);
    if (send_result != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to send AT response\n");
        cJSON_Delete(root);
        free(json_string);
        return ERROR_CMD_RESPONSE;
    }
    cJSON_Delete(root);
    free(json_string);
    esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_INFO_OBTAIN, NULL, 0, pdMS_TO_TICKS(10000));
    
    if(  is_need_reset_reboot
        || is_need_reset_shutdown
        || is_need_reboot
        || is_need_shutdown)
    {
        //Respond first, then execute
        vTaskDelay(200 / portTICK_PERIOD_MS);

        if( is_need_reset_reboot ) {
            set_reset_factory(false);
        } else if( is_need_reset_shutdown ){
            set_reset_factory(true);
        }

        if( is_need_reboot ) {
            esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_REBOOT, NULL, 0, pdMS_TO_TICKS(10000));
        }

        if ( is_need_shutdown ) {
            esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_SHUTDOWN, NULL, 0, pdMS_TO_TICKS(10000));
        }
        
    }
    return AT_CMD_SUCCESS;
}

/**
 * @brief Handles the "deviceinfo" command by generating a JSON response with device information.
 *
 * This function retrieves the software version and Himax software version, constructs a JSON object
 * containing various pieces of device information, and sends the JSON response.
 *
 * @param params A string containing the parameters for the command. This parameter is currently unused.
 *
 * The generated JSON object includes the following fields:
 * - name: "deviceinfo?"
 * - code: 0
 * - data: An object containing:
 *   - Eui: "1"
 *   - Token: "1"
 *   - Ble_Mac: "123"
 *   - Version: "1"
 *   - Time_Zone: "01"
 *   - Himax_Software_Version: The version of the Himax software.
 *   - Esp32_Software_Version: The version of the ESP32 software.
 *
 * The JSON string is then sent as an AT response.
 */
at_cmd_error_code handle_deviceinfo_command(char *params)
{
    (void)params; // Prevent unused parameter warning

    ESP_LOGI(TAG, "handle_deviceinfo_command\n");
    // Get the software version
    char *software_version = get_software_version(AT_CMD_CALLER);
    if (software_version == NULL)
    {
        ESP_LOGE(TAG, "Failed to get software version\n");
        return ERROR_DATA_READ_FAIL;
    }

    // Get the Himax software version
    char *himax_version = get_himax_software_version(AT_CMD_CALLER);
    if (himax_version == NULL)
    {
        ESP_LOGE(TAG, "Failed to get Himax software version\n");
    }

    // Get the brightness value
    int brightness_value_resp = get_brightness(AT_CMD_CALLER);
    if (brightness_value_resp < 0)
    {
        ESP_LOGE(TAG, "Failed to get brightness value\n");
        return ERROR_DATA_READ_FAIL;
    }
    // Get the sound value
    int sound_value_resp = get_sound(AT_CMD_CALLER);
    if (sound_value_resp < 0)
    {
        ESP_LOGE(TAG, "Failed to get sound value\n");
        return ERROR_DATA_READ_FAIL;
    }

    // Get the RGB switch value
    int rgb_switch = get_rgb_switch(AT_CMD_CALLER);
    if (rgb_switch < 0)
    {
        ESP_LOGE(TAG, "Failed to get RGB switch value\n");
        return ERROR_DATA_READ_FAIL;
    }

    int screenoff_time = get_screenoff_time(AT_CMD_CALLER); 
    if (screenoff_time < 0 || screenoff_time > 6)
    {
        ESP_LOGE(TAG, "Failed to get screenoff time value\n");
        return ERROR_DATA_READ_FAIL;
    }
    int screenoff_switch = get_screenoff_switch(AT_CMD_CALLER);
    if (screenoff_switch < 0)
    {
        ESP_LOGE(TAG, "Failed to get screenoff switch value\n");
        return ERROR_DATA_READ_FAIL;
    }

    int32_t voltage = bsp_battery_get_voltage();
    int battery_percent = bsp_battery_get_percent();

    // Get the time configuration
    struct view_data_time_cfg cfg;
    app_time_cfg_get(&cfg);
    time_t now;
    time(&now);
    char timestamp_str[20];
    snprintf(timestamp_str, sizeof(timestamp_str), "%lld", MAX(cfg.time, now));
    ESP_LOGI(TAG, "Current time configuration:\n");
    ESP_LOGI(TAG, "zone: %d\n", cfg.zone);

    cJSON *root = cJSON_CreateObject();

    cJSON_AddStringToObject(root, "name", "deviceinfo");

    cJSON_AddNumberToObject(root, "code", 0);

    cJSON *data = cJSON_CreateObject();

    char eui_rsp[17] = { 0 };
    byte_array_to_hex_string(get_eui(), 8, (const char *)eui_rsp);
    char bt_mac_rsp[13] = { 0 };
    byte_array_to_hex_string(get_bt_mac(), 6, (const char *)bt_mac_rsp);
    cJSON_AddItemToObject(root, "data", data);
    cJSON_AddStringToObject(data, "eui", (const char *)eui_rsp);
    cJSON_AddStringToObject(data, "blemac", (const char *)bt_mac_rsp);
    cJSON_AddNumberToObject(data, "automatic", cfg.auto_update);
    cJSON_AddNumberToObject(data, "rgbswitch", rgb_switch);
    cJSON_AddNumberToObject(data, "sound", sound_value_resp);
    cJSON_AddNumberToObject(data, "brightness", brightness_value_resp);
    cJSON_AddNumberToObject(data, "screenofftime", screenoff_time);
    cJSON_AddNumberToObject(data, "screenoffswitch", screenoff_switch);
    cJSON_AddStringToObject(data, "timestamp", timestamp_str);
    cJSON_AddNumberToObject(data, "timezone", cfg.zone);
    
    cJSON_AddStringToObject(data, "esp32softwareversion", (const char *)software_version);
    if (himax_version != NULL)
    {
        cJSON_AddStringToObject(data, "himaxsoftwareversion", (const char *)himax_version);
    }
    cJSON_AddNumberToObject(data, "batterypercent", battery_percent);
    cJSON_AddNumberToObject(data, "voltage", voltage); //mv

    char *json_string = cJSON_Print(root);

    ESP_LOGD(TAG, "JSON String in handle_deviceinfo_command: %s\n", json_string);
    esp_err_t send_result = send_at_response(json_string);
    if (send_result != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to send AT response\n");
        cJSON_Delete(root);
        free(json_string);
        return ERROR_CMD_RESPONSE;
    }
    cJSON_Delete(root);
    free(json_string);
    return AT_CMD_SUCCESS;
}

/**
 * @brief Handles the WiFi configuration command by parsing JSON input, setting the WiFi configuration, and generating a JSON response.
 *
 * This function parses the given parameters in JSON format to extract the SSID and password for WiFi configuration.
 * It then configures the WiFi settings using the extracted values, generates a JSON response containing the SSID and
 * connection status, and sends the response.
 *
 * @param params A JSON string containing the parameters for the WiFi configuration. The expected format is:
 * {
 *     "Ssid": "<your_ssid>",
 *     "Password": "<your_password>"
 * }
 *
 * The generated JSON object includes the following fields:
 * - name: The SSID of the WiFi network.
 * - code: The reason code for WiFi connection failure (if any).
 * - data: An object containing:
 *   - Ssid: The SSID of the WiFi network.
 *   - Rssi: The RSSI value (signal strength).
 *   - Encryption: The type of encryption used (e.g., WPA).
 */
at_cmd_error_code handle_wifi_set(char *params)
{
    esp_err_t ret = 0;
    ESP_LOGI(TAG, "Handling wifi command\n");
    
    struct view_data_wifi_config config;
    memset(&config, 0, sizeof(config));

    cJSON *json = cJSON_Parse(params);
    if (json == NULL)
    {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL)
        {
            ESP_LOGE(TAG, "Error before: %s\n", error_ptr);
        }
        return ERROR_CMD_JSON_PARSE;
    }
    cJSON *json_ssid = cJSON_GetObjectItemCaseSensitive(json, "ssid");
    cJSON *json_password = cJSON_GetObjectItemCaseSensitive(json, "password");
    // Get the SSID from the JSON
    if ((json_ssid != NULL) && cJSON_IsString(json_ssid) && (json_ssid->valuestring != NULL))
    {
        ESP_LOGI(TAG, "SSID in json: %s\n", json_ssid->valuestring);
        strncpy(config.ssid, json_ssid->valuestring, sizeof(config.ssid) - 1);
    }  else {
        ESP_LOGE(TAG, "SSID not found in JSON\n");
        cJSON_Delete(json);
        return ERROR_CMD_JSON_TYPE;
    }

    // Get the password from the JSON
    if ((json_password != NULL) && cJSON_IsString(json_password) && (json_password->valuestring != NULL) && (strlen(json_password->valuestring) > 0))
    {
        ESP_LOGI(TAG, "Password in json : %s\n", json_password->valuestring);
        config.have_password = true;
        strncpy(config.password, json_password->valuestring, sizeof(config.password) - 1);
    } else {
        config.have_password = false;
    }
    cJSON_Delete(json);

    //clear
    xSemaphoreTake(semaphorewificonnected, 0);
    xSemaphoreTake(semaphorewifidisconnected, 0);

    ret = esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_CONNECT, &config, sizeof(struct view_data_wifi_config), pdMS_TO_TICKS(10000));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to esp_event_post_to wifi cfg\n");
        return ERROR_DATA_WRITE_FAIL;
    }

    vTaskDelay(5000 / portTICK_PERIOD_MS);

    if ((xSemaphoreTake(semaphorewificonnected, pdMS_TO_TICKS(10000)) == pdTRUE) || (xSemaphoreTake(semaphorewifidisconnected, pdMS_TO_TICKS(10000)) == pdTRUE))
    {
        cJSON *root = cJSON_CreateObject();
        if (root == NULL)
        {
            ESP_LOGE(TAG, "Failed to create JSON object\n");
            return ERROR_CMD_JSON_CREATE;
        }

        cJSON *data = cJSON_CreateObject();
        if (data == NULL)
        {
            ESP_LOGE(TAG, "Failed to create JSON object\n");
            cJSON_Delete(root);
            return ERROR_CMD_JSON_CREATE;
        }
        cJSON_AddStringToObject(root, "name", "wifi");
        cJSON_AddNumberToObject(root, "code", wifi_connect_failed_reason);
        cJSON_AddItemToObject(root, "data", data);
        cJSON_AddStringToObject(data, "ssid", config.ssid);
        char *json_string = cJSON_Print(root);
        ESP_LOGD(TAG, "JSON String: %s", json_string);
        esp_err_t send_result = send_at_response(json_string);
        if (send_result != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to send AT response\n");
            cJSON_Delete(root);
            free(json_string);
            return ERROR_CMD_RESPONSE;
        }
        cJSON_Delete(root);
        free(json_string);
        return AT_CMD_SUCCESS;
    }
    return ERROR_CMD_EXEC_TIMEOUT;
}

/**
 * @brief Handles the WiFi query command by retrieving the current WiFi configuration and generating a JSON response.
 *
 * This function retrieves the currently connected WiFi network's SSID and RSSI (signal strength), constructs a JSON object
 * containing this information, and sends the JSON response.
 *
 * @param params A string containing the parameters for the command. This parameter is currently unused.
 *
 * The generated JSON object includes the following fields:
 * - name: "Wifi_Cfg"
 * - code: The network connection flag indicating the connection status.
 * - data: An object containing:
 *   - Ssid: The SSID of the currently connected WiFi network.
 *   - Rssi: The RSSI value (signal strength) of the current WiFi connection.
 */
at_cmd_error_code handle_wifi_query(char *params)
{
    (void)params; // Prevent unused parameter warning
    ESP_LOGI(TAG, "Handling wifi query command\n");
    current_wifi_get(&current_connected_wifi);
    static char ssid_string[34];
    strncpy(ssid_string, (const char *)current_connected_wifi.ssid, sizeof(ssid_string) - 1);
    ssid_string[sizeof(ssid_string) - 1] = '\0';
    cJSON *root = cJSON_CreateObject();
    if (root == NULL)
    {
        ESP_LOGE(TAG, "Failed to create JSON object\n");
        return ERROR_CMD_JSON_CREATE;
    }
    cJSON *data = cJSON_CreateObject();
    if (data == NULL)
    {
        ESP_LOGE(TAG, "Failed to create JSON object\n");
        cJSON_Delete(root);
        return ERROR_CMD_JSON_CREATE;
    }
    // add json obj
    cJSON_AddStringToObject(root, "name", "wifi");
    cJSON_AddNumberToObject(root, "code", network_connect_flag); // finish flag
    cJSON_AddItemToObject(root, "data", data);
    cJSON_AddStringToObject(data, "ssid", ssid_string);
    char rssi_str[10];
    snprintf(rssi_str, sizeof(rssi_str), "%d", current_connected_wifi.rssi);
    cJSON_AddStringToObject(data, "rssi", rssi_str);
    const char *encryption = print_auth_mode(current_connected_wifi.authmode);
    cJSON_AddStringToObject(data, "encryption", encryption);

    ESP_LOGI(TAG, "current_connected_wifi.ssid: %s", current_connected_wifi.ssid);
    ESP_LOGI(TAG, "current_connected_wifi.rssi: %d", current_connected_wifi.rssi);

    char *json_string = cJSON_Print(root);
    ESP_LOGD(TAG, "JSON String: %s", json_string);
    esp_err_t send_result = send_at_response(json_string);
    if (send_result != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to send AT response\n");
        cJSON_Delete(root);
        free(json_string);
        return ERROR_CMD_RESPONSE;
    }
    cJSON_Delete(root);
    free(json_string);
    return AT_CMD_SUCCESS;
}

/**
 * @brief Handles the WiFi table command by initializing the WiFi stack, simulating a WiFi scan, and generating a JSON response.
 *
 * This function initializes the WiFi stack, triggers a WiFi configuration task, waits for a specified duration, and then
 * simulates adding a WiFi network to the scanned WiFi stack. It then creates a JSON object representing the WiFi stack,
 * prints the JSON string, and sends it as an AT response.
 *
 * @param params A string containing the parameters for the command. This parameter is currently unused.
 *
 * The generated JSON object includes information about the WiFi networks that were scanned and the currently connected WiFi network.
 */
at_cmd_error_code handle_wifi_table(char *params)
{
    ESP_LOGI(TAG, "Handling wifi table command\n");
    resetWiFiStack(&wifiStack_scanned);
    resetWiFiStack(&wifiStack_connected);
    xTaskNotifyGive(xTask_wifi_config_entry);
    xSemaphoreTake(xBinarySemaphore_wifitable, portMAX_DELAY);
    cJSON *json = create_wifi_stack_json(&wifiStack_scanned, &wifiStack_connected);
    char *json_str = cJSON_Print(json);
    esp_err_t send_result = send_at_response(json_str);
    if (send_result != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to send AT response\n");
        cJSON_Delete(json);
        free(json_str);
        return ERROR_CMD_RESPONSE;
    }
    cJSON_Delete(json);
    free(json_str);
    return AT_CMD_SUCCESS;
}

at_cmd_error_code handle_taskflow_query_command(char *params)
{
    ESP_LOGI(TAG, "Handling handle_taskflow_query_command \n");
    vTaskDelay(10 / portTICK_PERIOD_MS);
    cJSON *root = cJSON_CreateObject();
    if (root == NULL)
    {
        ESP_LOGE(TAG, "Failed to create JSON object\n");
        return ERROR_CMD_JSON_CREATE;
    }
    cJSON *data_rep = cJSON_CreateObject();
    if (data_rep == NULL)
    {
        ESP_LOGE(TAG, "Failed to create JSON object\n");
        cJSON_Delete(root);
        return ERROR_CMD_JSON_CREATE;
    }
    cJSON_AddStringToObject(root, "name", "taskflow");

    __data_lock();
    cJSON_AddNumberToObject(root, "code", 0);
    cJSON_AddItemToObject(root, "data", data_rep);
    cJSON_AddNumberToObject(data_rep, "status", taskflow_status.engine_status);
    cJSON_AddNumberToObject(data_rep, "tlid", taskflow_status.tid);
    cJSON_AddNumberToObject(data_rep, "ctd", taskflow_status.ctd);
    cJSON_AddStringToObject(data_rep, "module", taskflow_status.module_name);
    cJSON_AddNumberToObject(data_rep, "module_err_code", taskflow_status.module_status);
    cJSON_AddNumberToObject(data_rep, "percent", ai_model_ota_status.percentage);
    __data_unlock();

    char *json_string = cJSON_Print(root);
    ESP_LOGD(TAG, "JSON String: %s\n", json_string);
    esp_err_t send_result = send_at_response(json_string);
    if (send_result != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to send AT response\n");
        cJSON_Delete(root);
        free(json_string);
        return ERROR_CMD_RESPONSE;
    }
    cJSON_Delete(root);
    free(json_string);
    return AT_CMD_SUCCESS;
}

at_cmd_error_code handle_taskflow_info_query_command(char *params)
{
    ESP_LOGI(TAG, "Handling handle_taskflow_info_query_command \n");
    char * p_json = NULL;
    vTaskDelay(10 / portTICK_PERIOD_MS);
    cJSON *root = cJSON_CreateObject();
    if (root == NULL)
    {
        ESP_LOGE(TAG, "Failed to create JSON object\n");
        return ERROR_CMD_JSON_CREATE;
    }
    cJSON *data_rep = cJSON_CreateObject();
    if (data_rep == NULL)
    {
        ESP_LOGE(TAG, "Failed to create JSON object\n");
        cJSON_Delete(root);
        return ERROR_CMD_JSON_CREATE;
    }
    cJSON_AddStringToObject(root, "name", "taskflowinfo");

    cJSON_AddNumberToObject(root, "code", 0);
    cJSON_AddItemToObject(root, "data", data_rep);

    p_json = tf_engine_flow_get_with_simplify();
    if (p_json == NULL)
    {
        cJSON_AddStringToObject(data_rep, "taskflow", "");
    } else {
        cJSON *taskflow_obj = cJSON_Parse(p_json);
        if (taskflow_obj != NULL) {
            cJSON_AddItemToObject(data_rep, "taskflow", taskflow_obj);
        } else {
            cJSON_AddStringToObject(data_rep, "taskflow", "");
        }
        free(p_json);
    }
    
    char *json_string = cJSON_PrintUnformatted(root);
    ESP_LOGD(TAG, "JSON String: %s\n", json_string);
    esp_err_t send_result = send_at_response(json_string);
    if (send_result != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to send AT response\n");
        cJSON_Delete(root);
        free(json_string);
        return ERROR_CMD_RESPONSE;
    }
    cJSON_Delete(root);
    free(json_string);
    return AT_CMD_SUCCESS;
}

at_cmd_error_code handle_taskflow_command(char *params)
{
    esp_err_t code = ESP_OK;
    ESP_LOGI(TAG, "Handling taskflow command\n");
    cJSON *json = cJSON_Parse(params);
    if (json == NULL)
    {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL)
        {
            ESP_LOGE(TAG, "Error before: %s\n", error_ptr);
        }
        return ERROR_CMD_JSON_PARSE;
    }
    char *taskflow_data_str = NULL;
    cJSON *json_data = cJSON_GetObjectItem(json, "data");
    if (json_data && cJSON_IsObject(json_data))
        taskflow_data_str = cJSON_PrintUnformatted(json_data);
    cJSON_Delete(json);

    if (taskflow_data_str == NULL) {
        return ERROR_CMD_JSON_PARSE;
    }

    ESP_LOGI(TAG, "will send to taskflow, strlen=%d", strlen(taskflow_data_str));

    esp_event_post_to(app_event_loop_handle, CTRL_EVENT_BASE, CTRL_EVENT_TASK_FLOW_START_BY_BLE, 
                        &taskflow_data_str, sizeof(void *), pdMS_TO_TICKS(10000));

    vTaskDelay(10 / portTICK_PERIOD_MS);
    cJSON *root = cJSON_CreateObject();
    if (root == NULL)
    {
        ESP_LOGE(TAG, "Failed to create JSON object\n");
        return ERROR_CMD_JSON_CREATE;
    }
    cJSON *data_rep = cJSON_CreateObject();
    if (data_rep == NULL)
    {
        ESP_LOGE(TAG, "Failed to create JSON object\n");
        cJSON_Delete(root);
        return ERROR_CMD_JSON_CREATE;
    }
    cJSON_AddStringToObject(root, "name", "taskflow");
    cJSON_AddNumberToObject(root, "code", code);
    cJSON_AddItemToObject(root, "data", data_rep);
    char *json_string = cJSON_Print(root);
    ESP_LOGD(TAG, "JSON String: %s", json_string);
    esp_err_t send_result = send_at_response(json_string);
    if (send_result != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to send AT response\n");
        cJSON_Delete(root);
        free(json_string);
        return ERROR_CMD_RESPONSE;
    }
    cJSON_Delete(root);
    free(json_string);
    return AT_CMD_SUCCESS;
}

at_cmd_error_code handle_localservice_query(char *params)
{
    (void)params; // Prevent unused parameter warning
    ESP_LOGI(TAG, "%s \n", __func__);

    cJSON *root = cJSON_CreateObject();
    if (root == NULL)
    {
        ESP_LOGE(TAG, "Failed to create JSON object\n");
        return ERROR_CMD_JSON_CREATE;
    }
    cJSON_AddStringToObject(root, "name", "localservice");
    cJSON_AddNumberToObject(root, "code", 0);
    bool passthrough = false;
    esp_err_t ret = ERROR_CMD_JSON_CREATE, ret_tmp;
    local_service_cfg_type1_t cfg;
    do {
        cJSON *data = cJSON_AddObjectToObject(root, "data");
        if (!data) break;
        //audio_task_composer
        cJSON *data_item = cJSON_AddObjectToObject(data, "audio_task_composer");
        if (!data_item) break;
        ret_tmp = get_local_service_cfg_type1(AT_CMD_CALLER, CFG_ITEM_TYPE1_AUDIO_TASK_COMPOSER, &cfg);
        if (ret_tmp != ESP_OK) { ret = ret_tmp; break; }
        if (!cJSON_AddNumberToObject(data_item, "switch", (int)cfg.enable)) break;
        if (!cJSON_AddStringToObject(data_item, "url", cfg.url)) break;
        if (!cJSON_AddStringToObject(data_item, "token", cfg.token)) break;
        //image_analyzer
        data_item = cJSON_AddObjectToObject(data, "image_analyzer");
        if (!data_item) break;
        ret_tmp = get_local_service_cfg_type1(AT_CMD_CALLER, CFG_ITEM_TYPE1_IMAGE_ANALYZER, &cfg);
        if (ret_tmp != ESP_OK) { ret = ret_tmp; break; }
        if (!cJSON_AddNumberToObject(data_item, "switch", (int)cfg.enable)) break;
        if (!cJSON_AddStringToObject(data_item, "url", cfg.url)) break;
        if (!cJSON_AddStringToObject(data_item, "token", cfg.token)) break;
        //training
        data_item = cJSON_AddObjectToObject(data, "training");
        if (!data_item) break;
        ret_tmp = get_local_service_cfg_type1(AT_CMD_CALLER, CFG_ITEM_TYPE1_TRAINING, &cfg);
        if (ret_tmp != ESP_OK) { ret = ret_tmp; break; }
        if (!cJSON_AddNumberToObject(data_item, "switch", (int)cfg.enable)) break;
        if (!cJSON_AddStringToObject(data_item, "url", cfg.url)) break;
        if (!cJSON_AddStringToObject(data_item, "token", cfg.token)) break;
        //notification_proxy
        data_item = cJSON_AddObjectToObject(data, "notification_proxy");
        if (!data_item) break;
        ret_tmp = get_local_service_cfg_type1(AT_CMD_CALLER, CFG_ITEM_TYPE1_NOTIFICATION_PROXY, &cfg);
        if (ret_tmp != ESP_OK) { ret = ret_tmp; break; }
        if (!cJSON_AddNumberToObject(data_item, "switch", (int)cfg.enable)) break;
        if (!cJSON_AddStringToObject(data_item, "url", cfg.url)) break;
        if (!cJSON_AddStringToObject(data_item, "token", cfg.token)) break;

        passthrough = true;
    } while (0);
    if (!passthrough) {
        if (cfg.url) free(cfg.url);
        if (cfg.token) free(cfg.token);
        cJSON_Delete(root);
        return ret;
    }

    char *json_string = cJSON_Print(root);
    ESP_LOGD(TAG, "%s: JSON String: %s\n", __func__, json_string);
    if (send_at_response(json_string) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to send AT response\n");
        ret = ERROR_CMD_RESPONSE;
    } else {
        ret = AT_CMD_SUCCESS;
    }
    if (cfg.url) free(cfg.url);
    if (cfg.token) free(cfg.token);
    cJSON_Delete(root);
    free(json_string);

    return ret;
}

static esp_err_t __localservice_set_one(cJSON *data, const char *data_key, int cfg_index)
{
    cJSON *data_item = cJSON_GetObjectItem(data, data_key);
    char *token = NULL;
    if (data_item) {
        cJSON *item_enable, *item_url, *item_token;
        item_enable = cJSON_GetObjectItem(data_item, "switch");
        item_url = cJSON_GetObjectItem(data_item, "url");
        item_token = cJSON_GetObjectItem(data_item, "token");
        if (item_enable && item_url && cJSON_IsGeneralBool(item_enable) && cJSON_IsString(item_url)) {
            // simple validation on url
            if (strchr(item_url->valuestring, ' ') != NULL) return ESP_ERR_INVALID_ARG;
            if (item_token && cJSON_IsString(item_token)) {
                token = item_token->valuestring;
            } else {
                token = "";
            }
            return set_local_service_cfg_type1(AT_CMD_CALLER, cfg_index, cJSON_IsGeneralTrue(item_enable), item_url->valuestring, token);
        }
    }
    return ESP_OK;
}

at_cmd_error_code handle_localservice_set(char *params)
{
    ESP_LOGI(TAG, "%s \n", __func__);

    cJSON *json = cJSON_Parse(params);
    if (json == NULL)
    {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL)
        {
            ESP_LOGE(TAG, "Error before: %s\n", error_ptr);
        }
        return ERROR_CMD_JSON_PARSE;
    }

    at_cmd_error_code ret = AT_CMD_SUCCESS;
    cJSON *data = cJSON_GetObjectItem(json, "data");
    if (!data) goto localservice_set_end;

    ESP_GOTO_ON_ERROR(__localservice_set_one(data, "audio_task_composer", CFG_ITEM_TYPE1_AUDIO_TASK_COMPOSER),
                        localservice_set_end, TAG, "%s: error when setting local service cfg!!!", __func__);
    ESP_GOTO_ON_ERROR(__localservice_set_one(data, "image_analyzer", CFG_ITEM_TYPE1_IMAGE_ANALYZER),
                        localservice_set_end, TAG, "%s: error when setting local service cfg!!!", __func__);
    ESP_GOTO_ON_ERROR(__localservice_set_one(data, "training", CFG_ITEM_TYPE1_TRAINING),
                        localservice_set_end, TAG, "%s: error when setting local service cfg!!!", __func__);
    ESP_GOTO_ON_ERROR(__localservice_set_one(data, "notification_proxy", CFG_ITEM_TYPE1_NOTIFICATION_PROXY),
                        localservice_set_end, TAG, "%s: error when setting local service cfg!!!", __func__);

localservice_set_end:
    cJSON_AddNumberToObject(json, "code", (int)ret);
    char *json_string = cJSON_Print(json);
    ESP_LOGD(TAG, "JSON String: %s", json_string);
    if (send_at_response(json_string) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to send AT response\n");
        ret = ERROR_CMD_RESPONSE;
    }
    cJSON_Delete(json);
    free(json_string);

    return ret;
}


/**
 * @brief A static task that handles incoming AT commands, parses them, and executes the corresponding actions.
 *
 * This function runs in an infinite loop, receiving messages from a stream buffer within bluetooth. It parses the received AT commands,
 * converts the hex data to a string, and uses regular expressions to match and extract command details.
 * The extracted command is then executed. The function relies on auxiliary functions like `byte_array_to_hex_string` to process
 * the received data.
 *
 * This task is declared static, indicating that it is intended to be used only within the file it is defined in,and placed in PSRAM
 */

void __at_cmd_proc_task(void *arg)
{
    at_cmd_buffer_t *cmd_buff = &g_at_cmd_buffer;

    while (1)
    {
        ble_msg_t ble_msg;
        if (xQueueReceive(ble_msg_queue, &ble_msg, portMAX_DELAY) != pdPASS) {
            ESP_LOGE(TAG, "Failed to receive ble message from queue");
            continue;
        }

        // grow the buffer
        __data_lock();
        while ((cmd_buff->cap - cmd_buff->wr_ptr - 1/* \0 */) < ble_msg.size && cmd_buff->cap < AT_CMD_BUFFER_MAX_LEN) {
            cmd_buff->cap += AT_CMD_BUFFER_LEN_STEP;
            if (cmd_buff->cap > AT_CMD_BUFFER_MAX_LEN) {
                ESP_LOGE(TAG, "at cmd buffer can't grow anymore, max_size=%d, want=%d", AT_CMD_BUFFER_MAX_LEN, cmd_buff->cap);
                cmd_buff->cap = AT_CMD_BUFFER_MAX_LEN;
            }
            cmd_buff->buff = psram_realloc(cmd_buff->buff, cmd_buff->cap);
            if (cmd_buff->buff == NULL) {
                ESP_LOGE(TAG, "at cmd buffer mem alloc failed!!!");
                __data_unlock();
                vTaskDelay(portMAX_DELAY);
            }
            ESP_LOGI(TAG, "at cmd buffer grow to size %d", cmd_buff->cap);
        }

        // copy msg into buffer
        int wr_len = MIN(ble_msg.size, (cmd_buff->cap - cmd_buff->wr_ptr - 1/* \0 */));
        memcpy(cmd_buff->buff + cmd_buff->wr_ptr, ble_msg.msg, wr_len);
        cmd_buff->wr_ptr += wr_len;
        cmd_buff->buff[cmd_buff->wr_ptr] = '\0';
        __data_unlock();
        ESP_LOGD(TAG, "at cmd buffer write_pointer=%d", cmd_buff->wr_ptr);

        // if this msg is the last slice ( ending with "\r\n")
        if (!strstr((char *)ble_msg.msg, "\r\n")) {  //app_ble.c ensures there's null-terminator in ble_msg.msg
            free(ble_msg.msg);
            continue;
        }
        free(ble_msg.msg);

        ESP_LOGI(TAG, "at cmd buffer recv the \\r\\n, process the buffer...");
        cmd_buff->wr_ptr = 0;
        char *test_strings = (char *)cmd_buff->buff;
        ESP_LOGD(TAG, "%s", test_strings);
        
        regex_t regex;
        int ret;
        ret = regcomp(&regex, pattern, REG_EXTENDED);
        if (ret)
        {
            ESP_LOGI(TAG, "Could not compile regex");
            cJSON *root = cJSON_CreateObject();
            cJSON_AddStringToObject(root, "name", "compile_regex_failed");
            cJSON_AddNumberToObject(root, "code", ERROR_CMD_REGEX_FAIL);
            char *json_string = cJSON_Print(root);
            ESP_LOGD(TAG, "JSON String: %s\n", json_string);
            send_at_response(json_string);
            cJSON_Delete(root);
            free(json_string);
            continue;
        }
        regmatch_t matches[4];
        ret = regexec(&regex, test_strings, 4, matches, 0);
        if (!ret)
        {
            // ESP_LOGI("recv_in match: %.*s\n", test_strings);
            char command_type[20];
            snprintf(command_type, sizeof(command_type), "%.*s", (int)(matches[1].rm_eo - matches[1].rm_so), test_strings + matches[1].rm_so);

            char *params = test_strings;
            if (matches[3].rm_so != -1)
            {
                int length = (int)(matches[3].rm_eo - matches[3].rm_so);
                params = test_strings + matches[3].rm_so;
                ESP_LOGD(TAG, "Matched string: %.50s... (total length: %d)\n", params, length);
            }
            char query_type = test_strings[matches[1].rm_eo] == '?' ? '?' : '=';
            // ESP_LOGD(TAG, "regex, command_type=%s, params=%s, query_type=%c", command_type, params, query_type);
            exec_command(&commands, command_type, params, query_type);
        }
        else if (ret == REG_NOMATCH)
        {
            ESP_LOGE(TAG, "No match: %s\n", test_strings);
            cJSON *root = cJSON_CreateObject();
            cJSON_AddStringToObject(root, "name", "Nomatch");
            cJSON_AddNumberToObject(root, "code", ERROR_CMD_FORMAT);
            char *json_string = cJSON_Print(root);
            ESP_LOGD(TAG, "JSON String: %s\n", json_string);
            send_at_response(json_string);
            cJSON_Delete(root);
            free(json_string);
        }
        else
        {
            char errbuf[100];
            regerror(ret, &regex, errbuf, sizeof(errbuf));
            ESP_LOGE(TAG, "Regex match failed: %s\n", errbuf);
            cJSON *root = cJSON_CreateObject();
            cJSON_AddStringToObject(root, "name", "RegexError");
            cJSON_AddNumberToObject(root, "code", ERROR_CMD_REGEX_FAIL);
            char *json_string = cJSON_Print(root);
            ESP_LOGD(TAG, "JSON String: %s\n", json_string);
            send_at_response(json_string);
            cJSON_Delete(root);
            free(json_string);
        }
        regfree(&regex);
    }
}

/**
 * @brief Handles view events related to WiFi configuration and status updates.
 *
 * This static function processes various WiFi-related events such as WiFi list requests,
 * WiFi list updates, and WiFi status updates. It updates the WiFi stacks and network connection flags accordingly.
 *
 * @param handler_args A pointer to the handler arguments (unused in this function).
 * @param base The event base, typically identifying the module generating the event.
 * @param id The event ID, specifying the particular event being handled.
 * @param event_data A pointer to the event data, providing context-specific information.
 */
static void __view_event_handler(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    struct view_data_wifi_st *p_cfg;
    switch (id)
    {
        case VIEW_EVENT_WIFI_ST:
            static bool fist = true;
            ESP_LOGI("AT_CMD_EVENT_READ:", "event: VIEW_EVENT_WIFI_ST");
            struct view_data_wifi_st *p_st = (struct view_data_wifi_st *)event_data;
            if (p_st->is_network)
            { // todo
                network_connect_flag = 1;
            }
            else
            {
                network_connect_flag = 0;
            }
            break;
        case VIEW_EVENT_TASK_FLOW_STATUS: {
            ESP_LOGI(TAG, "event: VIEW_EVENT_TASK_FLOW_STATUS");
            struct view_data_taskflow_status *p_status = (struct view_data_taskflow_status *)event_data;
            __data_lock();
            memcpy(&taskflow_status, p_status, sizeof(struct view_data_taskflow_status));
            if( taskflow_status.engine_status == TF_STATUS_STARTING ) { 
                ai_model_ota_status.percentage = 0; //Reset percentage
            }
            __data_unlock();
            ESP_LOGI(TAG, "taskflow status = %d", taskflow_status.engine_status);
            break;
        }
        case VIEW_EVENT_BLE_STATUS:
            bool status = *(bool *)event_data;
            ESP_LOGI(TAG, "event: VIEW_EVENT_BLE_STATUS, status=%d", status);
            __data_lock();
            if (!status) g_at_cmd_buffer.wr_ptr = 0;  //reset buffer when ble disconnect
            __data_unlock();
            break;
        default:
            break;
    }
}

static void __ctrl_event_handler(void* handler_args, 
                                 esp_event_base_t base, 
                                 int32_t id, 
                                 void* event_data)
{
    switch (id)
    {
        case CTRL_EVENT_OTA_AI_MODEL: {
            ESP_LOGI(TAG, "event: CTRL_EVENT_OTA_AI_MODEL");
            struct view_data_ota_status * ota_st = (struct view_data_ota_status *)event_data;
            __data_lock();
            memcpy(&ai_model_ota_status, ota_st, sizeof(struct view_data_ota_status));
            __data_unlock();
            break;
        }
        default:
            break;
    }
}

/**
 * @brief Initializes the AT command handling system.
 *
 * This function sets up the necessary components for handling AT commands, including creating the response queue,
 * initializing semaphores, and initializing tasks and WiFi stacks.
 */
void app_at_cmd_init()
{
#if CONFIG_ENABLE_FACTORY_FW_DEBUG_LOG
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
#endif

    data_sem_handle = xSemaphoreCreateMutex();
    xBinarySemaphore_wifitable = xSemaphoreCreateBinary();

    g_at_cmd_buffer.cap = AT_CMD_BUFFER_LEN_STEP;
    g_at_cmd_buffer.wr_ptr = 0;
    g_at_cmd_buffer.buff = psram_calloc(1, g_at_cmd_buffer.cap);

    taskflow_status.engine_status = TF_STATUS_IDLE;

    initWiFiStack(&wifiStack_scanned, WIFI_SCAN_RESULT_CNT_MAX);
    initWiFiStack(&wifiStack_connected, WIFI_SCAN_RESULT_CNT_MAX);
    wifi_stack_semaphore_init();
    AT_command_reg();

    // init at cmd msg Q
    ble_msg_queue = xQueueCreate(BLE_MSG_Q_SIZE, sizeof(ble_msg_t));

    // init at cmd processing task
    const uint32_t stack_size = 10 * 1024;
    StackType_t *task_stack1 = (StackType_t *)psram_calloc(1, stack_size * sizeof(StackType_t));
    StaticTask_t *task_tcb1 = heap_caps_calloc(1, sizeof(StaticTask_t), MALLOC_CAP_INTERNAL);
    xTaskCreateStatic(__at_cmd_proc_task, "at_cmd", stack_size, NULL, 9, task_stack1, task_tcb1);

    ESP_ERROR_CHECK(esp_event_handler_register_with(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_ST, 
                                                    __view_event_handler, NULL));

    ESP_ERROR_CHECK(esp_event_handler_register_with(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_TASK_FLOW_STATUS,
                                                    __view_event_handler, NULL));

    ESP_ERROR_CHECK(esp_event_handler_register_with(app_event_loop_handle, CTRL_EVENT_BASE, CTRL_EVENT_OTA_AI_MODEL, 
                                                        __ctrl_event_handler, NULL));

    ESP_ERROR_CHECK(esp_event_handler_register_with(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_BLE_STATUS,
                                                    __view_event_handler, NULL));

}

