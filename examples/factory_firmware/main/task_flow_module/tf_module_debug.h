
#pragma once
#include "tf_module.h"
#include "tf_module_data_type.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define TF_MODULE_DEBUG_NAME "debug"
#define TF_MODULE_DEBUG_VERSION "1.0.0"
#define TF_MODULE_DEBUG_DESC "debug module"

typedef struct tf_module_debug
{
    tf_module_t module_base;
    int evt_id;
} tf_module_debug_t;

tf_module_t * tf_module_debug_init(tf_module_debug_t *p_module_ins);

esp_err_t tf_module_debug_register(void);

#ifdef __cplusplus
}
#endif
