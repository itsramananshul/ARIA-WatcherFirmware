
#pragma once
#include <stdint.h>
#include "cJSON.h"

#ifdef __cplusplus
extern "C"
{
#endif

struct tf_module_ops
{
    int (*start)(void *p_module);
    int (*stop)(void *p_module);
    int (*cfg)(void *p_module, cJSON *p_json);
    int (*msgs_sub_set)(void *p_module, int evt_id);
    int (*msgs_pub_set)(void *p_module, int output_index, int *p_evt_id, int num);
};

typedef struct 
{
    const struct tf_module_ops *ops;
    void *p_module;
} tf_module_t;


typedef struct tf_module_mgmt {
    tf_module_t *(*tf_module_instance)(void);
    void (*tf_module_destroy)(tf_module_t *p_module);
}tf_module_mgmt_t;


static inline int tf_module_start(tf_module_t *handle)
{
    return handle->ops->start(handle->p_module);
}

static inline int tf_module_stop(tf_module_t *handle)
{
    return handle->ops->stop(handle->p_module);
}

static inline int tf_module_cfg(tf_module_t *handle, cJSON *p_json)
{
    return handle->ops->cfg(handle->p_module, p_json);
}

static inline int tf_module_msgs_sub_set(tf_module_t *handle, int evt_id)
{
    return handle->ops->msgs_sub_set(handle->p_module, evt_id);
}

static inline int tf_module_msgs_pub_set(tf_module_t *handle, int output_index, int *p_evt_id, int num)
{
    return handle->ops->msgs_pub_set(handle->p_module, output_index, p_evt_id, num);
}

#ifdef __cplusplus
}
#endif