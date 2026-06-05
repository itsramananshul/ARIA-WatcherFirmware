#pragma once
#include "tf_module.h"
#include "tf_module_data_type.h"
#include "esp_err.h"
#include "sensecap-watcher.h"
#include "sscma_client_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_timer.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define TF_MODULE_AI_CAMERA_NAME "ai camera"
#define TF_MODULE_AI_CAMERA_VERSION "1.0.0"
#define TF_MODULE_AI_CAMERA_DESC "ai camera module"

#ifndef CONFIG_ENABLE_TF_MODULE_AI_CAMERA_DEBUG_LOG
#if CONFIG_ENABLE_FACTORY_FW_DEBUG_LOG
    #define CONFIG_ENABLE_TF_MODULE_AI_CAMERA_DEBUG_LOG 1
#else
    #define CONFIG_ENABLE_TF_MODULE_AI_CAMERA_DEBUG_LOG 0
#endif
#endif

// module run err code
#define TF_MODULE_AI_CAMERA_CODE_OK                  0
#define TF_MODULE_AI_CAMERA_CODE_ERR_SSCMA_SIMPLE    (0X01 << 1)
#define TF_MODULE_AI_CAMERA_CODE_ERR_SSCMA_INVOKE    (0X01 << 2)
#define TF_MODULE_AI_CAMERA_CODE_ERR_SSCMA_MODEL     (0X01 << 3)
#define TF_MODULE_AI_CAMERA_CODE_ERR_SSCMA_MODEL_OTA (0X01 << 4)

/*************************************************************************
 * params config define
 ************************************************************************/

#define TF_MODULE_AI_CAMERA_MODES_INFERENCE  0
#define TF_MODULE_AI_CAMERA_MODES_SAMPLE     1

#define TF_MODULE_AI_CAMERA_MODEL_TYPE_CLOUD         0 // download model via url
#define TF_MODULE_AI_CAMERA_MODEL_TYPE_LOCAL_PERSON  1  
#define TF_MODULE_AI_CAMERA_MODEL_TYPE_LOCAL_APPLE   2
#define TF_MODULE_AI_CAMERA_MODEL_TYPE_LOCAL_GESTURE 3

#define TF_MODULE_AI_CAMERA_SHUTTER_TRIGGER_CONSTANTLY  0
#define TF_MODULE_AI_CAMERA_SHUTTER_TRIGGER_BY_UI       1
#define TF_MODULE_AI_CAMERA_SHUTTER_TRIGGER_BY_INPUT    2
#define TF_MODULE_AI_CAMERA_SHUTTER_TRIGGER_ONCE        3

#define TF_MODULE_AI_CAMERA_CONDITIONS_COMBO_AND   0
#define TF_MODULE_AI_CAMERA_CONDITIONS_COMBO_OR    1

#define TF_MODULE_AI_CAMERA_CONDITION_TYPE_LESS      0
#define TF_MODULE_AI_CAMERA_CONDITION_TYPE_EQUAL     1
#define TF_MODULE_AI_CAMERA_CONDITION_TYPE_GREATER   2
#define TF_MODULE_AI_CAMERA_CONDITION_TYPE_NOT_EQUAL 3

#define TF_MODULE_AI_CAMERA_CONDITION_MODE_PRESENCE_DETECTION   0
#define TF_MODULE_AI_CAMERA_CONDITION_MODE_VALUE_COMPARE        1
#define TF_MODULE_AI_CAMERA_CONDITION_MODE_NUM_CHANGE           2


#define TF_MODULE_AI_CAMERA_OUTPUT_TYPE_SMALL_IMG_ONLY           0
#define TF_MODULE_AI_CAMERA_OUTPUT_TYPE_SMALL_IMG_AND_LARGE_IMG  1

typedef enum {
    TF_MODULE_AI_CAMERA_ALGORITHM_TYPE_UNDEFINED  = 0u,
    TF_MODULE_AI_CAMERA_ALGORITHM_TYPE_FOMO       = 1u,
    TF_MODULE_AI_CAMERA_ALGORITHM_TYPE_PFLD       = 2u,
    TF_MODULE_AI_CAMERA_ALGORITHM_TYPE_YOLO       = 3u,
    TF_MODULE_AI_CAMERA_ALGORITHM_TYPE_IMCLS      = 4u,
    TF_MODULE_AI_CAMERA_ALGORITHM_TYPE_YOLO_POSE  = 5u,
    TF_MODULE_AI_CAMERA_ALGORITHM_TYPE_YOLO_V8    = 6u,
    TF_MODULE_AI_CAMERA_ALGORITHM_TYPE_NVIDIA_DET = 7u,
    TF_MODULE_AI_CAMERA_ALGORITHM_TYPE_YOLO_WORLD = 8u,
} tf_module_ai_camera_algorithm_type_t;

typedef enum {
    TF_MODULE_AI_CAMERA_ALGORITHM_CAT_UNDEFINED = 0u,
    TF_MODULE_AI_CAMERA_ALGORITHM_CAT_DET       = 1u,
    TF_MODULE_AI_CAMERA_ALGORITHM_CAT_POSE      = 2u,
    TF_MODULE_AI_CAMERA_ALGORITHM_CAT_CLS       = 3u,
} tf_module_ai_camera_algorithm_category_t;

struct tf_module_ai_camera_model
{
    char model_id[32]; // TODO max len?
    char url[256];
    char version[16];
    int size;
    char checksum[129];
    int model_type;
    int iou;
    int confidence;
    char *p_info_all;
};

struct tf_module_ai_camera_algorithm
{
    tf_module_ai_camera_algorithm_type_t        type;
    tf_module_ai_camera_algorithm_category_t    category;
};

struct tf_module_ai_camera_condition
{
    char class_name[64];
    int type;
    int num;
    int mode;
};

struct tf_module_ai_camera_time
{
    uint8_t hour; //24 hour
    uint8_t minute;
    uint8_t second;
};

struct tf_module_ai_camera_silent_period
{   
    bool  time_is_valid;
    int repeat[7]; //repeat time period, from Sunday to Saturday
    struct tf_module_ai_camera_time start;
    struct tf_module_ai_camera_time end;
    int silence_duration;
};

struct tf_module_ai_camera_params
{
    int mode;
    struct tf_module_ai_camera_model model;
    struct tf_module_ai_camera_condition *conditions;
    int condition_num;
    int conditions_combo;
    struct tf_module_ai_camera_silent_period silent_period;
    int shutter;
    int output_type;
    struct tf_module_ai_camera_algorithm algorithm;
};

/*************************************************************************
 *  data define
 ************************************************************************/


#define TF_MODULE_AI_CAMERA_TASK_STACK_SIZE 1024 * 5
#define TF_MODULE_AI_CAMERA_TASK_PRIO       13

#define TF_MODULE_AI_CAMERA_SENSOR_RESOLUTION_240_240    0
#define TF_MODULE_AI_CAMERA_SENSOR_RESOLUTION_416_416    1
#define TF_MODULE_AI_CAMERA_SENSOR_RESOLUTION_480_480    2
#define TF_MODULE_AI_CAMERA_SENSOR_RESOLUTION_640_480    3

// 0~100, Class object score threshold. Must be greater than or equal to score
#define CONFIG_TF_MODULE_AI_CAMERA_CLASS_OBJECT_SCORE_THRESHOLD   50

#define CONFIG_TF_MODULE_AI_CAMERA_CONDITION_TRIGGER_BUF_SIZE    5

#define CONFIG_TF_MODULE_AI_CAMERA_MODEL_IOU_DEFAULT            45
#define CONFIG_TF_MODULE_AI_CAMERA_MODEL_CONFIDENCE_DEFAULT     50

// Must be less than or equal to TF_MODULE_AI_CAMERA_CONDITION_TRIGGER_BUF_SIZE
#define CONFIG_TF_MODULE_AI_CAMERA_CONDITION_TRIGGER_THRESHOLD   5 

// default silence duration(if not set)
#define CONFIG_TF_MODULE_AI_CAMERA_SILENCE_DURATION_DEFAULT     60

struct tf_module_ai_camera_preview_info
{
    struct tf_data_image                      img;
    struct tf_data_inference_info inference;
};

typedef struct tf_module_ai_camera
{
    tf_module_t module_base;
    int input_evt_id;
    int *p_output_evt_id;
    int output_evt_num;
    sscma_client_handle_t  sscma_client_handle;
    struct tf_module_ai_camera_params params;
    SemaphoreHandle_t sem_handle; 
    EventGroupHandle_t event_group;
    TaskHandle_t task_handle;
    esp_timer_handle_t timer_handle;
    StaticTask_t *p_task_buf;
    StackType_t *p_task_stack_buf;
    sscma_client_info_t *himax_info;
    uint8_t shutter_trigger_flag;
    bool condition_trigger_buf[CONFIG_TF_MODULE_AI_CAMERA_CONDITION_TRIGGER_BUF_SIZE];
    int condition_trigger_buf_idx;
    time_t last_output_time;
    char *classes[CONFIG_MODEL_CLASSES_MAX_NUM];
    int classes_num_cache[CONFIG_MODEL_CLASSES_MAX_NUM];
    uint8_t target_id_cache;
    int classes_num[CONFIG_MODEL_CLASSES_MAX_NUM];
    tf_data_dualimage_with_inference_t output_data;
    struct tf_module_ai_camera_preview_info preview_info_cache;
    bool start_flag;
    int start_err_code;
    bool ai_model_downloading;
    bool ai_model_download_exit;
    bool need_abort_ai_model_download;
    bool sscma_starting_flag;
    int large_image_check_fail_cnt;
} tf_module_ai_camera_t;

tf_module_t * tf_module_ai_camera_init(tf_module_ai_camera_t *p_module_ins);


esp_err_t tf_module_ai_camera_register(void);

// TODO himax ota need to stop sscma client
char *tf_module_ai_camera_himax_version_get(void);

#ifdef __cplusplus
}
#endif
