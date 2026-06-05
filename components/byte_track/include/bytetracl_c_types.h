#ifndef BYTETRACK_C_TYPES_H
#define BYTETRACK_C_TYPES_H

/**
 * @file bytetracl_c_types.h
 * @brief BYTETrack C API types
 * @version 1.0.0
*/

#include <float.h>
#include <stddef.h>
#include <stdint.h>

#define BT_CONFIG_DEFAULT() \
    { .frame_rate = 10, .track_buffer = 15, .track_thresh = 0.5, .high_thresh = 0.6, .match_thresh = 0.8, }

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bt_bbox_t {
    float tlwh[4];
    float prob;
    int   label;
    int   track_id;
} bt_bbox_t;

typedef struct bt_config_t {
    int   frame_rate;
    int   track_buffer;
    float track_thresh;
    float high_thresh;
    float match_thresh;
} bt_config_t;

typedef enum {
    BT_ERR_OK              = 0,
    BT_ERR_FAIL            = -1,
    BT_ERR_INVALID_TRACKER = -2,
    BT_ERR_INVALID_OBJECTS = -3,
    BT_ERR_MEM_ALLOC_FAIL  = -5,
} bt_error_t;

typedef void* bt_handler_t;

#ifdef __cplusplus
}
#endif

#endif