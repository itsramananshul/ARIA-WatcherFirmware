
#include <assert.h>
#include <stdio.h>

#include "bytetrack_c_api.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "main";

void app_main(void) {
    bt_config_t  config  = BT_CONFIG_DEFAULT();
    bt_handler_t tracker = bt_tracker_create(&config);
    if (tracker == NULL) {
        ESP_LOGE(TAG, "Failed to create tracker");
        assert(0);
    }

    bt_bbox_t bboxes[] = {
      {   {10., 20., 30., 40.}, 0.9, 0, 0},
      {   {50., 60., 70., 80.}, 0.8, 1, 0},
      {{90., 100., 110., 120.}, 0.7, 2, 0},
    };

    int try = 3;
    do {
        for (size_t i = 0; i < sizeof(bboxes) / sizeof(bboxes[0]); ++i) {
            bboxes[i].tlwh[0] += 5.0;
            bboxes[i].tlwh[1] += 5.0;
        }

        bt_bbox_t* tracks     = NULL;
        size_t     num_tracks = 0;
        bt_error_t err = bt_tracker_update(tracker, bboxes, sizeof(bboxes) / sizeof(bboxes[0]), &tracks, &num_tracks);
        if (err != BT_ERR_OK) {
            ESP_LOGE(TAG, "Failed to update tracker");
            assert(0);
        }

        for (size_t i = 0; i < num_tracks; ++i) {
            ESP_LOGI(TAG,
                    "Track %d: tlwh = (%f, %f, %f, %f), prob = %f, label = %d, track_id = %d",
                    i,
                    tracks[i].tlwh[0],
                    tracks[i].tlwh[1],
                    tracks[i].tlwh[2],
                    tracks[i].tlwh[3],
                    tracks[i].prob,
                    tracks[i].label,
                    tracks[i].track_id);
        }

        free(tracks);
    } while (--try);

    bt_tracker_destroy(tracker);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}