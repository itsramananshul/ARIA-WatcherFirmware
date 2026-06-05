#ifndef AT_CMD_HEAD
#define AT_CMD_HEAD

#include <regex.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#include <esp_event_loop.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include "uhash.h"
#include "app_wifi.h"


typedef enum {
    // General Errors 
    AT_CMD_SUCCESS = ESP_OK, // No error occurred.
    ERROR_UNKNOWN = 0x2011, // An unknown error occurred.
    ERROR_INVALID_PARAM = 0x2012, // Invalid parameter was passed.
    ERROR_INTERRUPTED = 0x2013, // Operation was interrupted by the user.
    ERROR_RESOURCE_LIMIT = 0x2014, // Resource limit was reached, operation could not be completed.
    ERROR_CMD_MEM_ALLOC = 0x2015,   //  Memory allocation failed.
    ERROR_CMD_RESPONSE = 0x2016, // Command response error.
    ERROR_CMD_EVENT_POST = 0x2017, // Event post error.
    ERROR_CMD_EXEC_TIMEOUT = 0x2018, // Command execution timed out.    

    // Command Parsing Errors
    ERROR_CMD_FORMAT = 0x2020, // Command format error.
    ERROR_CMD_UNSUPPORTED = 0x2021, // Command is not supported.
    ERROR_CMD_TOO_MANY_PARAMS = 0x2022, // Too many parameters for command.
    ERROR_CMD_PARAM_RANGE = 0x2023, // Parameter out of range.
    ERROR_CMD_REGEX_FAIL = 0x2024, // Regular expression matching failed.
    ERROR_CMD_COMMAND_NOT_FOUND = 0x2025, // Command not found.
    ERROR_CMD_JSON_PARSE =   0x2026, // JSON parsing error.  
    ERROR_CMD_JSON_TYPE = 0x2027,           
    ERROR_CMD_JSON_CREATE = 0x2028, // JSON creation error.


    // Network and Connection Errors
    ERROR_NETWORK_FAIL = 0x2030, // Network connection failed.
    ERROR_NETWORK_TIMEOUT = 0x2031, // Network operation timed out.
    ERROR_DISCONNECT_FAIL = 0x2032, // Failed to disconnect properly.
    ERROR_NETWORK_CONFIG = 0x2033, // Network configuration error.
    ERROR_DNS_FAIL = 0x2034, // DNS resolution failed.


    // File System and Storage Errors still  retain
    ERROR_FILE_OPEN_FAIL = 0x2050, // Failed to open file.
    ERROR_FILE_IO = 0x2051, // I/O error in file operation.
    ERROR_STORAGE_FULL = 0x2052, // Insufficient storage space.
    ERROR_FILESYSTEM_CORRUPT = 0x2053, // Filesystem is corrupt.
    ERROR_FILE_LOCKED = 0x2054, // File is locked or in use.
    ERROR_DATA_READ_FAIL = 0x2055, // Failed to read data.  
    ERROR_DATA_WRITE_FAIL = 0x2056, // Failed to write data.
    ERROR_CMD_BASE64_DECODE = 0x2057, // Base64 decoding error.

    // Permission and Security Errors  still retain
    ERROR_PERMISSION_DENIED = 0x2060, // Permission denied.
    ERROR_AUTHENTICATION_FAILED = 0x2061, // Authentication failed.
    ERROR_ENCRYPTION_FAILED = 0x2062, // Encryption or decryption failed.
    ERROR_SECURITY_PROTOCOL = 0x2063 // Security protocol error.

} at_cmd_error_code;   // Error code supports esp general error code
    
       
typedef struct {
    char command_name[100];  // Assuming the command name will not exceed 100 characters
    at_cmd_error_code (*func)(char *params);  // Function pointer to the function that will process the command
    UT_hash_handle hh;  // Makes this structure hashable
} command_entry;

typedef struct {
    uint8_t *msg;
    int size;
} ble_msg_t;

typedef struct {
    uint8_t *buff;
    int cap;
    int wr_ptr;
} at_cmd_buffer_t;

extern QueueHandle_t ble_msg_queue;


void AT_command_reg();  // Function to register the AT commands
void AT_command_free();  // Function to free the memory allocated for the commands

void add_command(command_entry **commands, const char *name, at_cmd_error_code (*func)(char *params));  // Function to add a command to the list of commands
void exec_command(command_entry **commands, const char *name, char *params, char query);  // Function to execute a command

void task_handle_AT_command();  // Function to handle the AT command and select which command to execute


at_cmd_error_code handle_deviceinfo_command();  // Device info command
at_cmd_error_code handle_wifi_set(char *params);  // WiFi command
at_cmd_error_code handle_wifi_query(char *params);   //WiFi query command
at_cmd_error_code handle_wifi_table(char *params);  // WiFi table command
at_cmd_error_code handle_deviceinfo_cfg_command(char *params);  // Timezone command
at_cmd_error_code handle_taskflow_command(char *params); // Taskflow command
at_cmd_error_code handle_taskflow_info_query_command(char *params);    // Taskflow info query command
at_cmd_error_code handle_cloud_service_command(char *params);    // Cloud service command
at_cmd_error_code handle_cloud_service_query_command(char *params);    // Cloud service query command
at_cmd_error_code handle_emoji_command(char *params);    // Emoji command
at_cmd_error_code handle_taskflow_query_command(char *params); // Taskflow query command
at_cmd_error_code handle_bind_command(char *params); // Bind command
at_cmd_error_code handle_localservice_query(char *params);  // Local service query command
at_cmd_error_code handle_localservice_set(char *params);  // Local service config command

void init_event_loop_and_task();
void app_at_cmd_init();

void pushWiFiStack(WiFiStack *stack, WiFiEntry entry);
void freeWiFiStack(WiFiStack *stack);
void initWiFiStack(WiFiStack *stack, int capacity);
void wifi_stack_semaphore_init();



#endif
