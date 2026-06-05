
#include <stdio.h>
#include <inttypes.h>
#include <sdkconfig.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_wn_iface.h"
#include "esp_wn_models.h"
#include "esp_afe_sr_models.h"
#include "esp_mn_iface.h"
#include "esp_mn_models.h"
#include "model_path.h"
#include "string.h"

#include "sensecap-watcher.h"

#define BUFFER_SIZE      (1024)
#define SAMPLE_RATE      (16000) // For recording
#define SAMPLE_CHANNELS  (1)
#define DEFAULT_VOLUME   (100)
#define RECORDING_LENGTH (300)
typedef struct __attribute__((packed))
{
    uint8_t ignore_0[22];
    uint16_t num_channels;
    uint32_t sample_rate;
    uint8_t ignore_1[6];
    uint16_t bits_per_sample;
    uint8_t ignore_2[4];
    uint32_t data_size;
    uint8_t data[];
} dumb_wav_header_t;

/* Globals */
static const char *TAG = "example";
static QueueHandle_t audio_button_q = NULL;

int detect_flag = 0;
static esp_afe_sr_iface_t *afe_handle = NULL;
static esp_afe_sr_data_t *afe_data = NULL;
static volatile int task_flag = 0;

static void btn_handler(void *button_handle, void *usr_data)
{
    int button_pressed = (int)usr_data;
    xQueueSend(audio_button_q, &button_pressed, 0);
}

static void audio_task(void *arg)
{
    bsp_codec_init();
    bsp_codec_volume_set(DEFAULT_VOLUME, NULL);

    /* Pointer to a file that is going to be played */
    const char recording_filename[] = "/sdcard/rec.wav";
    const char mp3_filename[] = "/sdcard/Canon.wav";
    const char *play_filename = recording_filename;
    int btn_index = 0;
    int16_t *wav_bytes = heap_caps_malloc(BUFFER_SIZE, MALLOC_CAP_DEFAULT);
    assert(wav_bytes != NULL);

    while (1)
    {
        if (xQueueReceive(audio_button_q, &btn_index, portMAX_DELAY) == pdTRUE)
        {
            FILE *record_file = fopen(recording_filename, "wb");
            assert(record_file != NULL);
            dumb_wav_header_t wav_header = { .bits_per_sample = 16, .data_size = RECORDING_LENGTH * BUFFER_SIZE, .num_channels = SAMPLE_CHANNELS, .sample_rate = SAMPLE_RATE };
            uint32_t data_size = sizeof(dumb_wav_header_t) + wav_header.data_size - 4;
            memcpy(&wav_header.ignore_0[0], "RIFF", 4);
            memcpy(&wav_header.ignore_0[4], &data_size, 4);
            memcpy(&wav_header.ignore_0[8], "WAVEfmt ", 8);
            if (fwrite((void *)&wav_header, 1, sizeof(dumb_wav_header_t), record_file) != sizeof(dumb_wav_header_t))
            {
                ESP_LOGW(TAG, "Error in writing to file");
                continue;
            }
            setvbuf(record_file, NULL, _IOFBF, BUFFER_SIZE);

            ESP_LOGI(TAG, "\nRecording start\n");
            bsp_codec_set_fs(SAMPLE_RATE, 16, 2);
            size_t bytes_written = 0;
            while (bytes_written < RECORDING_LENGTH * BUFFER_SIZE)
            {
                size_t data_written = 0;
                ESP_ERROR_CHECK(bsp_i2s_read(wav_bytes, BUFFER_SIZE, &data_written, 0));
                bytes_written += fwrite(wav_bytes, 1, BUFFER_SIZE, record_file);
            }
            bsp_codec_dev_stop();
            ESP_LOGI(TAG, "Recording stop, length: %i bytes", bytes_written);
            fclose(record_file);

            vTaskDelay(1000 / portTICK_PERIOD_MS);

            FILE *play_file = fopen(play_filename, "rb");
            assert(play_file != NULL);
            if (fread((void *)&wav_header, 1, sizeof(wav_header), play_file) != sizeof(wav_header))
            {
                ESP_LOGW(TAG, "Error in reading file");
                break;
            }

            ESP_LOGI(TAG, "\nPlaying start\n");
            bsp_codec_set_fs(wav_header.sample_rate, wav_header.bits_per_sample, wav_header.num_channels);
            size_t bytes_send_to_i2s = 0;
            while (bytes_send_to_i2s < wav_header.data_size)
            {
                size_t bytes_read = fread(wav_bytes, 1, BUFFER_SIZE, play_file);
                ESP_ERROR_CHECK(bsp_i2s_write(wav_bytes, bytes_read, &bytes_read, 0));
                bytes_send_to_i2s += bytes_read;
            }
            bsp_codec_dev_stop();
            ESP_LOGI(TAG, "Playing stop, length: %i bytes", bytes_send_to_i2s);
            fclose(play_file);
        }
    }
    free(wav_bytes);
}

static void audio_init(void)
{
    // bsp_spiffs_init_default();
    bsp_sdcard_init_default();
    // bsp_lvgl_init();
    audio_button_q = xQueueCreate(10, sizeof(int));
    assert(audio_button_q != NULL);
    BaseType_t ret = xTaskCreate(audio_task, "audio_task", 4096, NULL, 6, NULL);
    assert(ret == pdPASS);

    /* Init audio buttons */
    const static button_config_t btn_config = {
        .type = BUTTON_TYPE_CUSTOM,
        .long_press_time = 1000,
        .short_press_time = 200,
        .custom_button_config = {
            .active_level = 0,
            .button_custom_init = bsp_knob_btn_init,
            .button_custom_deinit = bsp_knob_btn_deinit,
            .button_custom_get_key_value = bsp_knob_btn_get_key_value,
        },
    };
    button_handle_t btns = iot_button_create(&btn_config);
    iot_button_register_cb(btns, BUTTON_PRESS_DOWN, btn_handler, (void *)0);
    // lv_demo_widgets();
    // lv_demo_stress();
}

void feed_Task(void *arg)
{
    esp_afe_sr_data_t *afe_data = arg;
    int audio_chunksize = afe_handle->get_feed_chunksize(afe_data);
    int nch = afe_handle->get_channel_num(afe_data);
    int feed_channel = bsp_get_feed_channel();
    // assert(nch<feed_channel);
    int16_t *i2s_buff = malloc(audio_chunksize * sizeof(int16_t) * feed_channel);
    assert(i2s_buff);

    while (task_flag)
    {
        bsp_get_feed_data(false, i2s_buff, audio_chunksize * sizeof(int16_t) * feed_channel);
        afe_handle->feed(afe_data, i2s_buff);
    }
    if (i2s_buff)
    {
        free(i2s_buff);
        i2s_buff = NULL;
    }
    vTaskDelete(NULL);
}

void detect_Task(void *arg)
{
    esp_afe_sr_data_t *afe_data = arg;
    int afe_chunksize = afe_handle->get_fetch_chunksize(afe_data);
    int16_t *buff = malloc(afe_chunksize * sizeof(int16_t));
    assert(buff);
    printf("------------detect start------------\n");
    while (task_flag)
    {
        afe_fetch_result_t *res = afe_handle->fetch(afe_data);
        if (!res || res->ret_value == ESP_FAIL)
        {
            printf("fetch error!\n");
            break;
        }

        if (res->wakeup_state == WAKENET_DETECTED)
        {
            printf("wakeword detected\n");
            printf("model index:%d, word index:%d\n", res->wakenet_model_index, res->wake_word_index);
            printf("-----------LISTENING-----------\n");
        }
    }
    if (buff)
    {
        free(buff);
        buff = NULL;
    }
    vTaskDelete(NULL);
}

static void wakeup_init(void)
{
    ESP_ERROR_CHECK(bsp_codec_init());
    srmodel_list_t *models = esp_srmodel_init("model");
    char *wn_name = NULL;
    char *wn_name_2 = NULL;
    if (models != NULL)
    {
        for (int i = 0; i < models->num; i++)
        {
            if (strstr(models->model_name[i], ESP_WN_PREFIX) != NULL)
            {
                if (wn_name == NULL)
                {
                    wn_name = models->model_name[i];
                    printf("The first wakenet model: %s\n", wn_name);
                }
                else if (wn_name_2 == NULL)
                {
                    wn_name_2 = models->model_name[i];
                    printf("The second wakenet model: %s\n", wn_name_2);
                }
            }
        }
    }
    else
    {
        printf("Please enable wakenet model and select wake word by menuconfig!\n");
        return;
    }

    afe_handle = (esp_afe_sr_iface_t *)&ESP_AFE_SR_HANDLE;
    afe_config_t afe_config = AFE_CONFIG_DEFAULT();
    afe_config.aec_init = false;
    afe_config.pcm_config.total_ch_num = 1;
    afe_config.pcm_config.mic_num = 1;
    afe_config.pcm_config.ref_num = 0;

    afe_config.memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;
    afe_config.wakenet_init = true;
    afe_config.wakenet_model_name = wn_name;
    afe_config.wakenet_model_name_2 = wn_name_2;
    afe_config.voice_communication_init = false;
    afe_data = afe_handle->create_from_config(&afe_config);

    task_flag = 1;
    xTaskCreatePinnedToCore(&feed_Task, "feed", 8 * 1024, (void *)afe_data, 5, NULL, 0);
    xTaskCreatePinnedToCore(&detect_Task, "detect", 4 * 1024, (void *)afe_data, 5, NULL, 1);
}

void app_main(void)
{
    // this will prepare a task to recording and playing audio file in sdcard
    audio_init();

    // this will init wakeup engine and start a task to detect wakeword
    // wakeup_init();
}
