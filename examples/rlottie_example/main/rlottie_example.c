
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "sensecap-watcher.h"

extern const uint8_t lv_rlottie_eye[];

void testRlottie(void){
    //lv_obj_t *lottie = lv_rlottie_create_from_raw(lv_scr_act(), 200, 200, (const void *)lv_rlottie_eye);
    lv_obj_t * lottie = lv_rlottie_create_from_file(lv_scr_act(), 300, 300, "/spiffs/test_1.json");
    // lv_rlottie_set_current_frame(lottie, 20);
    lv_obj_center(lottie);
}

void app_main() {
    esp_io_expander_handle_t io_expander = bsp_io_expander_init();
    assert(io_expander != NULL);
    lv_disp_t *lvgl_disp = bsp_lvgl_init();
    assert(lvgl_disp != NULL);
    printf("rlottie example start!\n");
    bsp_spiffs_init_default();
    vTaskDelay(1000 / portTICK_PERIOD_MS);

    testRlottie();

    static char buffer[254];
    for(;;){
        sprintf(buffer, "   Biggest /     Free /    Total\n"
                        "\t  DRAM : [%8d / %8d / %8d]\n"
                        "\t  PSRAM : [%8d / %8d / %8d]\n"
                        "\t  DMA : [%8d / %8d / %8d]\n",
                heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                heap_caps_get_total_size(MALLOC_CAP_INTERNAL),
                heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM),
                heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                heap_caps_get_total_size(MALLOC_CAP_SPIRAM),
                heap_caps_get_largest_free_block(MALLOC_CAP_DMA),
                heap_caps_get_free_size(MALLOC_CAP_DMA),
                heap_caps_get_total_size(MALLOC_CAP_DMA));

        ESP_LOGI("MEM", "%s", buffer);

        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}