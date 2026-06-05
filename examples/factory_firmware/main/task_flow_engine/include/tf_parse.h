
#pragma once
#include "tf_module.h"

#ifdef __cplusplus
extern "C"
{
#endif

struct tf_module_wires
{
    int *p_evt_id;
    int num;
};
typedef struct tf_module_item
{
    int id;
    int index;
    const char *p_name;
    cJSON *p_params;
    struct tf_module_wires *p_wires;
    int output_port_num;
    tf_module_t *handle;
    tf_module_mgmt_t *mgmt_handle;
    uint32_t flag;
} tf_module_item_t;

typedef struct tf_info
{
    int type;
    intmax_t tid;
    intmax_t ctd;
    const char* p_tf_name; //memory from json parser
}tf_info_t;

int tf_parse_json_with_length(const char *p_str, size_t len,
                              cJSON **pp_json_root,
                              tf_module_item_t **pp_head,
                              tf_info_t *p_info);
                              
int tf_parse_json(const char *p_str,
                  cJSON **pp_json_root,
                  tf_module_item_t **pp_head,
                  tf_info_t *p_info);

void tf_parse_free(cJSON *p_json_root, tf_module_item_t *p_head, int num);


// can delete audio data in taskflow
char* tf_parse_util_simplify_json(const char *p_str);

#ifdef __cplusplus
}
#endif
