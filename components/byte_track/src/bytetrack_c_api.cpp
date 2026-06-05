#include "bytetrack_c_api.h"

#include <cstdlib>

#include "BYTETracker.h"

bt_handler_t bt_tracker_create(const bt_config_t* config) {
    if (config == nullptr) {
        return nullptr;
    }

    auto* tracker = new BYTETracker(config);
    return reinterpret_cast<bt_handler_t>(tracker);
}

bt_error_t bt_tracker_update(
  bt_handler_t tracker, const bt_bbox_t* objects, size_t num_objects, bt_bbox_t** tracks, size_t* num_tracks) {
    if (tracker == nullptr) {
        return BT_ERR_INVALID_TRACKER;
    }

    if (objects == nullptr && num_objects != 0) {
        return BT_ERR_INVALID_OBJECTS;
    }

    auto tracker_ptr = reinterpret_cast<BYTETracker*>(tracker);
    auto tracks_vec  = tracker_ptr->update(objects, num_objects);
    tracks_vec.shrink_to_fit();

    if (num_tracks == nullptr) {
        return BT_ERR_OK;
    }

    if (tracks == nullptr) {
        *num_tracks = tracks_vec.size();
        return BT_ERR_OK;
    }

    void* tracks_ptr = nullptr;
    if (*tracks == nullptr) {
        tracks_ptr = calloc(tracks_vec.size(), sizeof(bt_bbox_t));
        if (tracks_ptr == nullptr) {
            return BT_ERR_MEM_ALLOC_FAIL;
        }

        *num_tracks = tracks_vec.size();
    }

    const auto size = std::min(tracks_vec.size(), *num_tracks);
    for (size_t i = 0; i < size; ++i) {
        auto& track     = tracks_vec[i];
        auto& track_ptr = reinterpret_cast<bt_bbox_t*>(tracks_ptr)[i];

        for (size_t j = 0; j < 4; ++j) {
            track_ptr.tlwh[j] = track.tlwh[j];
        }
        track_ptr.prob     = track.score;
        track_ptr.label    = track.label;
        track_ptr.track_id = track.track_id;
    }

    *tracks = reinterpret_cast<bt_bbox_t*>(tracks_ptr);

    return BT_ERR_OK;
}

bt_error_t bt_tracker_destroy(bt_handler_t tracker) {
    if (tracker == nullptr) {
        return BT_ERR_INVALID_TRACKER;
    }

    auto tracker_ptr = reinterpret_cast<BYTETracker*>(tracker);
    delete tracker_ptr;

    return BT_ERR_OK;
}
