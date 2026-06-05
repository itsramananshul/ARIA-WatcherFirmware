#pragma once
#include <stdint.h>
#include <stddef.h>
#include "tf_module_data_type.h"
#include "tf_module_ai_camera.h"

#ifdef __cplusplus
extern "C" {
#endif

const char * tf_data_type_to_str(uint32_t type);

void tf_data_free(void *event_data);


void tf_data_buf_copy(struct tf_data_buf *p_dst, struct tf_data_buf *p_src);
void tf_data_buf_free(struct tf_data_buf *p_data);

void tf_data_image_copy(struct tf_data_image *p_dst, struct tf_data_image *p_src);
void tf_data_image_free(struct tf_data_image *p_data);

void tf_data_inference_copy(struct tf_data_inference_info *p_dst, struct tf_data_inference_info *p_src);
void tf_data_inference_free(struct tf_data_inference_info *p_inference);

#ifdef __cplusplus
}
#endif
