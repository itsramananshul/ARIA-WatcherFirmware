#pragma once
#include "tf_module.h"
#include "tf_parse.h"
#include "sys/queue.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define TF_ENGINE_TASK_STACK_SIZE 1024 * 5
#define TF_ENGINE_TASK_PRIO 13
#define TF_ENGINE_QUEUE_SIZE 3

// Define status codes for engine state
#define TF_STATUS_RUNNING               0
#define TF_STATUS_STARTING              1
#define TF_STATUS_STOP                  2
#define TF_STATUS_STOPING               3
#define TF_STATUS_IDLE                  4
#define TF_STATUS_PAUSE                 5

// Define error status codes (greater than or equal to 100 indicates an error)
#define TF_STATUS_ERR_GENERAL           100
#define TF_STATUS_ERR_JSON_PARSE        101
#define TF_STATUS_ERR_MODULE_NOT_FOUND  102
#define TF_STATUS_ERR_MODULES_INSTANCE  103
#define TF_STATUS_ERR_MODULES_PARAMS    104
#define TF_STATUS_ERR_MODULES_WIRES     105
#define TF_STATUS_ERR_MODULES_START     106
#define TF_STATUS_ERR_MODULES_INTERNAL  107   // module runtime internal error

#define TF_STATUS_ERR_DEVICE_OTA        200   // The device is in OTA mode and cannot run the taskflow
#define TF_STATUS_ERR_DEVICE_VI         201   // The device is in voice interaction mode and cannot run taskflow


typedef struct
{
    char *p_data;
    size_t len;
} tf_flow_data_t;

typedef struct tf_module_node
{
    const char *p_name;
    const char *p_desc;
    const char *p_version;
    tf_module_mgmt_t *mgmt_handle;
    SLIST_ENTRY(tf_module_node)
    next;
} tf_module_node_t;

typedef SLIST_HEAD(tf_module_nodes, tf_module_node) tf_module_nodes_t;

typedef void (*tf_engine_status_cb_t)(void * p_arg, intmax_t tid, int status, const char *p_err_module);

typedef void (*tf_module_status_cb_t)(void * p_arg, const char *p_name, int status);

typedef struct tf_engine
{
    esp_event_loop_handle_t event_handle;
    tf_module_nodes_t module_nodes;
    TaskHandle_t task_handle;
    StaticTask_t *p_task_buf;
    StackType_t *p_task_stack_buf;
    QueueHandle_t queue_handle;
    SemaphoreHandle_t sem_handle;
    EventGroupHandle_t event_group;
    cJSON *cur_flow_root;
    tf_module_item_t *p_module_head;
    int module_item_num;
    tf_info_t tf_info;
    tf_engine_status_cb_t  status_cb;
    void * p_status_cb_arg;
    tf_module_status_cb_t  module_status_cb;
    void * p_module_status_cb_arg;
    int status;
} tf_engine_t;

/**
 * Initializes the engine.
 *
 * @return The result of the initialization operation. Possible return values are:
 *         - ESP_OK: The engine was successfully initialized.
 *         - ESP_ERR_NO_MEM: Insufficient memory to initialize the engine.
 *         - ESP_FAIL: An unspecified error occurred during the initialization process.
 *
 * @throws None.
 *
 * @comment This function initializes the engine and prepares it for use.
 */
esp_err_t tf_engine_init(void);

esp_err_t tf_engine_run(void);

/**
 * Stops the engine.
 *
 * @return The result of stopping the engine. Possible return values are:
 *         - ESP_OK: The engine was successfully stopped.
 *         - ESP_FAIL: An unspecified error occurred during the stopping process.
 *
 * @throws None.
 *
 * @comment This function stops the engine and performs any necessary cleanup.
 */
esp_err_t tf_engine_stop(void);

/**
 *  Restarts the engine.
 *  
 * @comment Restart only when you need to run taskflow.
 */
esp_err_t tf_engine_restart(void);

/**
 * Pauses the engine.
 *
 * @return The result of pausing the engine. Possible return values are:
 *         - ESP_OK: The engine was successfully paused.
 *         - ESP_FAIL: An unspecified error occurred during the pausing process.
 *
 * @throws None.
 *
 * @comment This function pauses the engine and temporarily stops its operation.
 */
esp_err_t tf_engine_pause(void);

/**
 * Waiting for the pause engine to complete
 *
 * @return The result of pausing the engine. Possible return values are:
 *         - ESP_OK: The engine was successfully paused.
 *         - ESP_FAIL: An unspecified error occurred during the pausing process.
 *
 * @throws None.
 *
 * @comment This function pauses the engine and temporarily stops its operation.
 */
esp_err_t tf_engine_pause_block(TickType_t xTicksToWait);

/*
* Resumes the engine.
*
* @return The result of resuming the engine. Possible return values are:
*         - ESP_OK: The engine was successfully resumed.
*         - ESP_FAIL: An unspecified error occurred during the resuming process.
*
* @throws None.
*
* @comment This function resumes the engine after it has been paused.
*/
esp_err_t tf_engine_resume(void);

/**
 * Sets the flow of the engine.
 *
 * @param p_str Pointer to the flow string.
 * @param len Length of the flow string.
 *
 * @return The result of setting the flow. Possible return values are:
 *         - ESP_OK: The flow was successfully set.
 *         - ESP_ERR_INVALID_ARG: The flow string or length is invalid.
 *         - ESP_FAIL: An unspecified error occurred during the flow set process.
 *
 * @throws None.
 *
 * @comment This function sets the flow of the engine based on the provided flow string.
 *         The flow string should be in JSON format.
 *         The engine will start executing the flow.
 *         The flow string can be retrieved using the `tf_engine_flow_get` function.
 */
esp_err_t tf_engine_flow_set(const char *p_str, size_t len);

/**
 * Retrieves the current flow of the engine.
 *
 * @return Pointer to the current flow data. Memory needs to be freed after use.
 *
 * @throws None.
 *
 * @comment This function returns a pointer to the current flow data. The caller is responsible for freeing the memory after use.
 */
char* tf_engine_flow_get(void);

/*
 * Retrieves the current flow of the engine with simplified format.
 *
 * @return Pointer to the current flow data. Memory needs to be freed after use.
 *
 * @throws None.
 *
 * @comment This function returns a pointer to the current flow data. The caller is responsible for freeing the memory after use.
*/
char* tf_engine_flow_get_with_simplify(void);

/**
 * Retrieves the current thread ID (TID) of the engine.
 *
 * @param p_tid Pointer to store the retrieved TID.
 *
 * @return The result of the TID retrieval operation. Possible return values are:
 *         - ESP_OK: The TID was successfully retrieved.
 *         - ESP_ERR_INVALID_ARG: The pointer to store the TID is NULL.
 *
 * @throws None.
 *
 * @comment This function retrieves the current thread ID (TID) of the engine and stores it in the memory pointed to by `p_tid`.
 */
esp_err_t tf_engine_tid_get(intmax_t *p_tid);

/**
 * Retrieves the current thread ID (CTD) of the engine.
 *
 * @param p_ctd Pointer to store the retrieved CTD.
 *
 * @return The result of the CTD retrieval operation. Possible return values are:
 *         - ESP_OK: The CTD was successfully retrieved.
 *         - ESP_ERR_INVALID_ARG: The pointer to store the CTD is NULL.
 *
 * @throws None.
 *
 * @comment This function retrieves the current thread ID (CTD) of the engine and stores it in the memory pointed to by `p_ctd`.
 */
esp_err_t tf_engine_ctd_get(intmax_t *p_ctd);

/**
 * Retrieves the type of the engine.
 *
 * @param p_type A pointer to store the retrieved engine type.
 *
 * @return The result of the type retrieval operation. Possible return values are:
 *         - ESP_OK: The engine type was successfully retrieved.
 *         - ESP_ERR_INVALID_ARG: The pointer to store the type is NULL.
 *
 * @throws None.
 *
 */
esp_err_t tf_engine_type_get(int *p_type);

/**
 * Retrieves information about the engine.
 *
 * @param p_info A pointer to store the retrieved engine information.
 *
 * @return The result of the information retrieval operation. Possible return values are:
 *         - ESP_OK: The engine information was successfully retrieved.
 *         - ESP_ERR_INVALID_ARG: The pointer to store the information is NULL.
 *
 * @throws None.
 *
 * @note The retrieved engine information will be stored in the memory pointed to by `p_info`. 
 *          It is important to free the memory pointed to by `p_info->p_tf_name` after use.
 */
esp_err_t tf_engine_info_get(tf_info_t *p_info);

/**
 * Retrieves the current status of the engine.
 *
 * @param p_status A pointer to store the retrieved engine status.
 *
 * @return The result of the status retrieval operation. Possible return values are:
 *         - ESP_OK: The engine status was successfully retrieved.
 *         - ESP_ERR_INVALID_ARG: The pointer to store the status is NULL.
 *
 * @throws None.
 *
 * @comment The retrieved engine status will be stored in the memory pointed to by `p_status`.
 */
esp_err_t tf_engine_status_get(int *p_status);

/**
 * Registers a callback function to receive notifications about engine status changes.
 *
 * @param engine_status_cb The callback function to register.
 * @param p_arg A pointer to the argument to be passed to the callback function.
 *
 * @return ESP_OK: The callback function was successfully registered.
 *
 * @throws None.
 *
 * @comment The registered callback function will be invoked whenever the engine status changes.
 */
esp_err_t tf_engine_status_cb_register(tf_engine_status_cb_t engine_status_cb, void *p_arg);

/**
 * Sets the status of a module.
 *
 * @param p_module_name The name of the module to set the status for.
 * @param status The new status value to set.
 *
 * @return ESP_OK: The status was successfully set.
 *
 * @throws None.
 * 
 * @note When setting the module status, the module status callback function will be executed if registered.
 */
esp_err_t tf_module_status_set(const char *p_module_name, int status);

/**
 * Registers a callback function to receive notifications about module abnormal status.
 *
 * @param module_status_cb The callback function to register.
 * @param p_arg A pointer to the argument to be passed to the callback function.
 *
 * @return ESP_OK: The callback function was successfully registered.
 *
 * @throws None.
 */
esp_err_t tf_module_status_cb_register(tf_module_status_cb_t module_status_cb, void *p_arg);


/**
 * Register a task flow module.
 *
 * @param p_name the name of the module
 * @param p_desc the description of the module
 * @param p_version the version of the module
 * @param mgmt_handle the management handle of the module
 *
 * @return esp_err_t ESP_OK if registration is successful, error code otherwise
 *
 * @throws None
 */
esp_err_t tf_module_register(const char *p_name,
                                const char *p_desc,
                                const char *p_version,
                                tf_module_mgmt_t *mgmt_handle);

esp_err_t tf_modules_report(void);

/**
 * Posts an event to the task flow engine event loop.
 *
 * @param event_id the ID of the event to post
 * @param event_data pointer to the event data
 * @param event_data_size size of the event data
 * @param ticks_to_wait the amount of time to wait for the event to be posted
 *
 * @return esp_err_t ESP_OK if the event is successfully posted, error code otherwise
 *
 * @throws None
 */
esp_err_t tf_event_post(int32_t event_id,
                        const void *event_data,
                        size_t event_data_size,
                        TickType_t ticks_to_wait);

/**
 * Registers an event handler for a specific event ID.
 *
 * @param event_id The ID of the event to register the handler for.
 * @param event_handler The event handler function to register.
 * @param event_handler_arg The argument to pass to the event handler.
 *
 * @return The result of the registration operation.
 *
 * @throws None.
 */
esp_err_t tf_event_handler_register(int32_t event_id,
                                    esp_event_handler_t event_handler,
                                    void *event_handler_arg);
/**
 * Unregisters an event handler for a specific event ID.
 *
 * @param event_id The ID of the event to unregister the handler for.
 * @param event_handler The event handler function to unregister.
 *
 * @return The result of the unregistration operation.
 *
 * @throws None.
 */
esp_err_t tf_event_handler_unregister(int32_t event_id,
                                        esp_event_handler_t event_handler);

#ifdef __cplusplus
}
#endif
