
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    TF_DATA_TYPE_UNKNOWN = 0,
    TF_DATA_TYPE_UINT8,
    TF_DATA_TYPE_UINT16,
    TF_DATA_TYPE_UINT32,
    TF_DATA_TYPE_UINT64,
    TF_DATA_TYPE_INT8,
    TF_DATA_TYPE_INT16,
    TF_DATA_TYPE_INT32,
    TF_DATA_TYPE_INT64,
    TF_DATA_TYPE_FLOAT32,
    TF_DATA_TYPE_FLOAT64,
    TF_DATA_TYPE_TIME,
    TF_DATA_TYPE_BUFFER,
    TF_DATA_TYPE_DUALIMAGE_WITH_INFERENCE,
    TF_DATA_TYPE_DUALIMAGE_WITH_INFERENCE_AUDIO_TEXT,
};

struct tf_data_buf
{
    uint8_t *p_buf;
    uint32_t len;
};

struct tf_data_image
{
    uint8_t *p_buf;  //base64 data
    uint32_t len;
    time_t   time;
};

enum tf_data_inference_type {
    INFERENCE_TYPE_UNKNOWN = 0,
    INFERENCE_TYPE_BOX,    //sscma_client_box_t
    INFERENCE_TYPE_CLASS,  //sscma_client_class_t
    INFERENCE_TYPE_POINT,  //sscma_client_point_t
    INFERENCE_TYPE_KEYPOINT     //sscma_client_keypoint_t
};

// classes max num
#define CONFIG_MODEL_CLASSES_MAX_NUM       20

struct tf_data_inference_info
{
    bool is_valid;
    enum tf_data_inference_type   type;
    void  *p_data;
    uint32_t cnt;
    char *classes[CONFIG_MODEL_CLASSES_MAX_NUM];
};


/*************************************************************************
 * Modules input or output data define
 * 
 * Template:
 * typedef struct {
 *      uint32_t  type; 
 *      //other data
 * } tf_data_xxx_t;
 * 
 ************************************************************************/

typedef struct {
    uint32_t  type; // TF_DATA_TYPE_TIME
    time_t   time;
} tf_data_time_t;

typedef struct {
    uint32_t  type; // TF_DATA_TYPE_BUFFER
    struct tf_data_buf data;
} tf_data_buffer_t;

typedef struct tf_data_dualimage_with_inference
{
    uint32_t type; //TF_DATA_TYPE_DUALIMAGE_WITH_INFERENCE
    struct  tf_data_image img_small;
    struct  tf_data_image img_large;
    struct tf_data_inference_info inference;
} tf_data_dualimage_with_inference_t;

typedef struct tf_data_dualimage_with_audio_text
{
    uint32_t type; // TF_DATA_TYPE_DUALIMAGE_WITH_INFERENCE_AUDIO_TEXT
    struct tf_data_image img_small;
    struct tf_data_image img_large;
    struct tf_data_inference_info inference;
    struct tf_data_buf   audio;
    struct tf_data_buf   text;
} tf_data_dualimage_with_audio_text_t;

#ifdef __cplusplus
}
#endif
