#ifndef BYTETRACK_C_API_H
#define BYTETRACK_C_API_H

/**
 * @file bytetracl_c_api.h
 * @brief BYTETrack C API
 * @version 1.0.0
*/

#include "bytetracl_c_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create a new BYTETrack handler
 * @param config BYTETrack configuration
 * @return BYTETrack handler
*/
bt_handler_t bt_tracker_create(const bt_config_t* config);

/**
 * @brief Update the tracker with new objects
 * @param tracker BYTETrack handler
 * @param objects Array of objects to update the tracker with
 * @param num_objects Number of objects in the array
 * @param tracks Output array of tracks
 * @param num_tracks Number of tracks in the output array
 * @return Error code
 * @note The caller is responsible for freeing the memory of the output tracks array
*/
bt_error_t bt_tracker_update(
  bt_handler_t tracker, const bt_bbox_t* objects, size_t num_objects, bt_bbox_t** tracks, size_t* num_tracks);

/**
 * @brief Destroy the BYTETrack handler
 * @param tracker BYTETrack handler
 * @return Error code
*/
bt_error_t bt_tracker_destroy(bt_handler_t tracker);

#ifdef __cplusplus
}
#endif

#endif