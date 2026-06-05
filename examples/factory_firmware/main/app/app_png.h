#ifndef APP_PNG_H
#define APP_PNG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include "lvgl/lvgl.h"
#include "cJSON.h"
#include "esp_err.h"

#define MAX_IMAGES 10

typedef struct
{
    void *data;
    size_t size;
} ImageData;

// buildin emoji png count
typedef struct {
    int speaking_count;
    int listening_count;
    int greeting_count;
    int standby_count;
    int detecting_count;
    int detected_count;
    int analyzing_count;
} BuiltInEmojiCount;

// custom emoji png count
typedef struct {
    int custom_speaking_count;
    int custom_listening_count;
    int custom_greeting_count;
    int custom_standby_count;
    int custom_detecting_count;
    int custom_detected_count;
    int custom_analyzing_count;
} CustomEmojiCount;

void init_builtin_emoji_count(BuiltInEmojiCount *count);
void init_custom_emoji_count(CustomEmojiCount *count);
void count_png_images(BuiltInEmojiCount *builtin_count, CustomEmojiCount *custom_count);
void read_and_store_selected_pngs(const char *primary_prefix, const char *secondary_prefix, lv_img_dsc_t **img_dsc_array, int *image_count);
void read_and_store_selected_customed_pngs(const char *primary_prefix, const char *secondary_prefix, lv_img_dsc_t **img_dsc_array, int *image_count);
void check_and_download_files();

typedef enum {
    DOWNLOAD_SUCCESS = 0,
    DOWNLOAD_ERR_ALLOC = -1,
    DOWNLOAD_ERR_HTTP = -2,
    DOWNLOAD_ERR_TIMEOUT = -3,
    DOWNLOAD_ERR_UNKNOWN = -4
} download_status_t;

typedef struct {
    bool success;
    int error_code;
} download_result_t;

typedef struct {
    download_result_t *results;
    int64_t total_time_us;
    double download_speed;
} download_summary_t;

esp_err_t download_emoji_images(download_summary_t *summary, cJSON *filename, cJSON *url_array, int url_count);

#ifdef __cplusplus
}
#endif

#endif // APP_PNG_H
