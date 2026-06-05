#include "app_audio_player.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "util.h"
#include "app_device_info.h"
#include "audio_player.h"
#include "storage.h"


static const char *TAG = "audio_player";

struct app_audio_player *gp_audio_player = NULL;

#define EVENT_STREAM_START      BIT0
#define EVENT_STREAM_STOP       BIT1
#define EVENT_STREAM_STOP_DONE  BIT2
#define EVENT_FILE_MEM_START    BIT3
#define EVENT_FILE_MEM_DONE     BIT4


static int __volume_get(void)
{
    return get_sound(MAX_CALLER);
}

static void __data_lock(struct app_audio_player  *p_audio_player)
{
    xSemaphoreTake(p_audio_player->sem_handle, portMAX_DELAY);
}
static void __data_unlock(struct app_audio_player *p_audio_player)
{
    xSemaphoreGive(p_audio_player->sem_handle);  
}

static esp_err_t __audio_player_mute_set(AUDIO_PLAYER_MUTE_SETTING setting)
{
    bsp_codec_mute_set(setting == AUDIO_PLAYER_MUTE ? true : false);
    // restore the voice volume upon unmuting
    if (setting == AUDIO_PLAYER_UNMUTE) {
        bsp_codec_volume_set(__volume_get(), NULL);
    }
    return ESP_OK;
}

static esp_err_t __audio_player_set_fs(uint32_t sample_rate,
                                        uint8_t  bits_per_sample,
                                        i2s_slot_mode_t  channel)
{
    esp_err_t ret = ESP_OK;

    ret = bsp_codec_set_fs( sample_rate, 
                            bits_per_sample, 
                            channel);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = bsp_codec_mute_set(true);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = bsp_codec_mute_set(false);
    if (ret != ESP_OK) {
        return ret;
    }    
    ret = bsp_codec_volume_set(__volume_get(), NULL);
    if (ret != ESP_OK) {
        return ret;
    }
    return ESP_OK;
}

static bool __is_wav(struct app_audio_player *p_audio_player, uint8_t *p_buf, size_t len) 
{
    esp_err_t ret = ESP_OK;

    if(len < sizeof(audio_wav_header_t)) {
        return false;
    }

    //may have other data? TODO
    audio_wav_header_t *wav_head = ( audio_wav_header_t *)p_buf;
    if((NULL == strstr((char *)wav_head->ChunkID, "RIFF")) ||
        (NULL == strstr((char *)wav_head->Format, "WAVE"))) {
        return false;
    }
    ESP_LOGI(TAG,"sample_rate=%d, channels=%d, bps=%d",
                wav_head->SampleRate,
                wav_head->NumChannels,
                wav_head->BitsPerSample);

    if( wav_head->SampleRate !=  p_audio_player->sample_rate || 
        wav_head->NumChannels != p_audio_player->channel ||
        wav_head->BitsPerSample != p_audio_player->bits_per_sample) {

        ESP_LOGI(TAG, "need change fs");
        __data_lock(p_audio_player);
        p_audio_player->sample_rate = wav_head->SampleRate;
        p_audio_player->channel = wav_head->NumChannels;
        p_audio_player->bits_per_sample = wav_head->BitsPerSample;
        __data_unlock(p_audio_player);

        ret = __audio_player_set_fs(p_audio_player->sample_rate, p_audio_player->bits_per_sample, p_audio_player->channel);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "set fs fail %d!", ret);
        }
    }
        
    return true;
}

#if defined(CONFIG_AUDIO_PLAYER_ENABLE_MP3_STREAM)
typedef enum {
    MP3_DECODE_STATUS_CONTINUE,         /*< data remaining, call decode again */
    MP3_DECODE_STATUS_NO_DATA_CONTINUE, /*< data remaining but none in this call */
    MP3_DECODE_STATUS_DONE,             /*< no data remaining to decode */
    MP3_DECODE_STATUS_ERROR             /*< unrecoverable error */
} MP3_DECODE_STATUS;

static bool __is_mp3(struct app_audio_player *p_audio_player, uint8_t *p_buf, size_t len)
{
    bool is_mp3_file = false;

    if( len < 3) {
        return false;
    }

    uint8_t *magic = p_buf;
    if((magic[0] == 0xFF) &&
        (magic[1] == 0xFB))
    {
        is_mp3_file = true;
    } else if((magic[0] == 0xFF) &&
                (magic[1] == 0xF3))
    {
        is_mp3_file = true;
    } else if((magic[0] == 0xFF) &&
                (magic[1] == 0xF2))
    {
        is_mp3_file = true;
    } else if((magic[0] == 0x49) &&
                (magic[1] == 0x44) &&
                (magic[2] == 0x33)) /* 'ID3' */
    {
        if( len >   sizeof(audio_mp3_id3_header_v2_t) ) {
            /* Get ID3 head */
            audio_mp3_id3_header_v2_t *p_tag = (audio_mp3_id3_header_v2_t *) p_buf;
            if (memcmp("ID3", (const void *) p_tag, sizeof(p_tag->header)) == 0) {
                is_mp3_file = true;
            }
        }
    }
    return is_mp3_file;
}

static MP3_DECODE_STATUS __stream_mp3_decode_then_play(struct app_audio_player *p_audio_player) 
{
    esp_err_t ret = ESP_OK;

    audio_mp3_instance *pInstance = &p_audio_player->mp3_data;
    HMP3Decoder mp3_decoder = p_audio_player->mp3_decoder;
    MP3FrameInfo frame_info;
    uint8_t *p_data = NULL;
    size_t recv_len = 0;
    size_t data_write = 0; // useless

    size_t unread_bytes = pInstance->bytes_in_data_buf - (pInstance->read_ptr - pInstance->data_buf);

    if(unread_bytes == 0 && pInstance->eof_reached ) {
        return MP3_DECODE_STATUS_DONE;
    }

    /* somewhat arbitrary trigger to refill buffer - should always be enough for a full frame */
    if (unread_bytes < 1.25 * MAINBUF_SIZE && !pInstance->eof_reached) {
        uint8_t *write_ptr = pInstance->data_buf + unread_bytes;
        size_t free_space = pInstance->data_buf_size - unread_bytes;

    	/* move last, small chunk from end of buffer to start,
           then fill with new data */
        memmove(pInstance->data_buf, pInstance->read_ptr, unread_bytes);

        size_t nRead = 0;
        p_data = xRingbufferReceiveUpTo( p_audio_player->rb_handle, &recv_len, pdMS_TO_TICKS(100), free_space);
        if(p_data != NULL) {

            ESP_LOGI(TAG, "xRingbuffer get: %d", recv_len);
            nRead = recv_len;
            memcpy(write_ptr, p_data, recv_len);
            vRingbufferReturnItem(p_audio_player->rb_handle, p_data);

            __data_lock(p_audio_player);
            p_audio_player->stream_play_len += recv_len;
            __data_unlock(p_audio_player);

        }  else {
            //maybe finished
            nRead = 0;
            if( p_audio_player->stream_finished) {
                ESP_LOGI(TAG, "stream finished: %d", p_audio_player->stream_play_len);
                pInstance->eof_reached = true;
            }
        }

        pInstance->bytes_in_data_buf = unread_bytes + nRead;
        pInstance->read_ptr = pInstance->data_buf;

        ESP_LOGV(TAG,"nRead %d, eof %d",  nRead, pInstance->eof_reached);

        unread_bytes = pInstance->bytes_in_data_buf;
    }

    ESP_LOGV(TAG,"data_buf 0x%p, read 0x%p", pInstance->data_buf, pInstance->read_ptr);

    if(unread_bytes == 0) {
        ESP_LOGD(TAG, "unread_bytes == 0, status done");
        return MP3_DECODE_STATUS_NO_DATA_CONTINUE;
    }


    /* Find MP3 sync word from read buffer */
    int offset = MP3FindSyncWord(pInstance->read_ptr, unread_bytes);

    ESP_LOGV(TAG,"unread %d, total %d, offset 0x%x(%d)",
            unread_bytes, pInstance->bytes_in_data_buf, offset, offset);

    if (offset >= 0) {
        uint8_t *read_ptr = pInstance->read_ptr + offset; /*!< Data start point */
        unread_bytes -= offset;
        ESP_LOGV(TAG,"read 0x%p, unread %d", read_ptr, unread_bytes);
        int mp3_dec_err = MP3Decode(mp3_decoder, &read_ptr, (int*)&unread_bytes, p_audio_player->p_mp3_decode_buf, 0);

        pInstance->read_ptr = read_ptr;

        if(mp3_dec_err == ERR_MP3_NONE) {
            /* Get MP3 frame info */
            MP3GetLastFrameInfo(mp3_decoder, &frame_info);

            int sample_rate = frame_info.samprate;
            uint32_t bits_per_sample = frame_info.bitsPerSample;
            uint32_t channels = frame_info.nChans;
            size_t frame_count = (frame_info.outputSamps / frame_info.nChans);
            size_t bytes_to_write = frame_count * channels * (bits_per_sample / 8);


            if( sample_rate !=  p_audio_player->sample_rate || 
                bits_per_sample != p_audio_player->bits_per_sample ||
                channels != p_audio_player->channel) {

                ESP_LOGI(TAG, "need change fs");
                __data_lock(p_audio_player);
                p_audio_player->sample_rate = sample_rate;
                p_audio_player->channel = channels;
                p_audio_player->bits_per_sample = bits_per_sample;
                __data_unlock(p_audio_player);

                ret = __audio_player_set_fs(p_audio_player->sample_rate, p_audio_player->bits_per_sample, p_audio_player->channel);
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "set fs fail %d!", ret);
                }
            }
            ESP_LOGV(TAG, "decode frame %d, %d, %d, %d, %d", frame_count, bytes_to_write, sample_rate, bits_per_sample, channels);
            bsp_i2s_write(p_audio_player->p_mp3_decode_buf, bytes_to_write, &data_write, 0); 

        } else {
            if (pInstance->eof_reached) {
                ESP_LOGE(TAG, "status error %d, but EOF", mp3_dec_err);
                return MP3_DECODE_STATUS_DONE;
            } else if (mp3_dec_err == ERR_MP3_MAINDATA_UNDERFLOW) {
                // underflow indicates MP3Decode should be called again
                ESP_LOGD(TAG, "underflow read ptr is 0x%p", read_ptr);
                return MP3_DECODE_STATUS_NO_DATA_CONTINUE;
            } else {
                // NOTE: some mp3 files result in misdetection of mp3 frame headers
                // and during decode these misdetected frames cannot be
                // decoded
                //
                // Rather than give up on the file by returning
                // MP3_DECODE_STATUS_ERROR, we ask the caller
                // to continue to call us, by returning MP3_DECODE_STATUS_NO_DATA_CONTINUE.
                //
                // The invalid frame data is skipped over as a search for the next frame
                // on the subsequent call to this function will start searching
                // AFTER the misdetected frmame header, dropping the invalid data.
                //
                // We may want to consider a more sophisticated approach here at a later time.
                ESP_LOGE(TAG, "status error %d", mp3_dec_err);
                return MP3_DECODE_STATUS_NO_DATA_CONTINUE;
            }
        }
    } else {
        // drop an even count of words
        size_t words_to_drop = unread_bytes / 2;
        size_t bytes_to_drop = words_to_drop * 2;

        // if the unread bytes is less than BYTES_IN_WORD, we should drop any unread bytes
        // to avoid the situation where the file could have a few extra bytes at the end
        // of the file that isn't at least BYTES_IN_WORD and decoding would get stuck
        if(unread_bytes < 2) {
            bytes_to_drop = unread_bytes;
        }

        // shift the read_ptr to drop the bytes in the buffer
        pInstance->read_ptr += bytes_to_drop;

        /* Sync word not found in frame. Drop data that was read until a word boundary */
        ESP_LOGE(TAG, "MP3 sync word not found, dropping %d bytes", bytes_to_drop);
    }

    return MP3_DECODE_STATUS_CONTINUE;
}
#endif

static void app_audio_player_task(void *p_arg)
{
    struct app_audio_player *p_audio_player = (struct app_audio_player *)p_arg;
    EventBits_t bits = 0;
    while(1) {
        bits = xEventGroupWaitBits(p_audio_player->event_group, 
                            EVENT_STREAM_START, pdTRUE, pdTRUE, pdMS_TO_TICKS(5000));
        if(bits & EVENT_STREAM_START) {
            ESP_LOGI(TAG, "EVENT_STREAM_START");

            UBaseType_t available_bytes = 0;
            uint8_t *p_data = NULL;
            size_t recv_len = 0;
            size_t data_write = 0; // useless
            bool is_play_done = false;

            while(1) {
                bits = xEventGroupWaitBits(p_audio_player->event_group, 
                                        EVENT_STREAM_STOP, pdTRUE, pdTRUE, pdMS_TO_TICKS(0));
                if(bits & EVENT_STREAM_STOP) {
                    ESP_LOGI(TAG, "EVENT_STREAM_STOP");
                    break;
                }
                switch (p_audio_player->audio_type)
                {
                    case AUDIO_TYPE_WAV: {
                        // xRingbufferReceiveUpTo and vRingbufferReturnItem can be interrupted by other tasks.
                        p_data = xRingbufferReceiveUpTo( p_audio_player->rb_handle, &recv_len, pdMS_TO_TICKS(500), AUDIO_PLAYER_RINGBUF_CHUNK_SIZE);
                        if(p_data != NULL) {
                            bsp_i2s_write(p_data, recv_len, &data_write, 0); // maybe take 500ms to write data
                            vRingbufferReturnItem(p_audio_player->rb_handle, p_data);

                            __data_lock(p_audio_player);
                            p_audio_player->stream_play_len += recv_len;
                            __data_unlock(p_audio_player);

                        } else {
                            //maybe finished
                            __data_lock(p_audio_player);
                            if( p_audio_player->stream_finished) {
                                p_audio_player->status = AUDIO_PLAYER_STATUS_IDLE;
                                is_play_done = true;
                            }
                            __data_unlock(p_audio_player);
                        }
                        break;
                    }  
                    case AUDIO_TYPE_MP3: {
                        MP3_DECODE_STATUS status = 0;
                        status = __stream_mp3_decode_then_play(p_audio_player);
                        if(  status == MP3_DECODE_STATUS_DONE)  {
                            __data_lock(p_audio_player);
                            p_audio_player->status = AUDIO_PLAYER_STATUS_IDLE;
                            is_play_done = true;
                            __data_unlock(p_audio_player);
                        }
                        break;
                    }  
                    default:
                        __data_lock(p_audio_player);
                        if( p_audio_player->stream_finished) {
                            p_audio_player->status = AUDIO_PLAYER_STATUS_IDLE;
                            is_play_done = true;
                        }
                        __data_unlock(p_audio_player);
                        vTaskDelay(pdMS_TO_TICKS(100));
                        break;
                }


                if(is_play_done) {  
                    ESP_LOGI(TAG, "play done");
                    break;
                }
            }
        }
    }
}


static void __audio_player_cb(audio_player_cb_ctx_t *ctx)
{   
    struct app_audio_player *p_audio_player = (struct app_audio_player *)ctx->user_ctx;
    switch (ctx->audio_event) {
    case AUDIO_PLAYER_CALLBACK_EVENT_IDLE:
        ESP_LOGI(TAG, "Player IDLE");
        __data_lock(p_audio_player);
        if( p_audio_player->mem_need_free && p_audio_player->p_mem_buf != NULL) {
            ESP_LOGI(TAG, "free mem");
            free(p_audio_player->p_mem_buf);
            p_audio_player->mem_need_free = false;
            p_audio_player->p_mem_buf = NULL;
        }
        p_audio_player->status = AUDIO_PLAYER_STATUS_IDLE;
        __data_unlock(p_audio_player);
        
        xEventGroupSetBits(p_audio_player->event_group, EVENT_FILE_MEM_DONE);

        break;
    case AUDIO_PLAYER_CALLBACK_EVENT_COMPLETED_PLAYING_NEXT:
        ESP_LOGI(TAG, "Player NEXT");
        break;
    case AUDIO_PLAYER_CALLBACK_EVENT_PLAYING:
        ESP_LOGI(TAG, "Player PLAYING");
        break;
    case AUDIO_PLAYER_CALLBACK_EVENT_PAUSE:
        ESP_LOGI(TAG, "Player PAUSE");
        break;
    case AUDIO_PLAYER_CALLBACK_EVENT_SHUTDOWN:
        ESP_LOGI(TAG, "Player SHUTDOWN");
        break;
    default:
        break;
    }
}

/*************************************************************************
 * API
 ************************************************************************/

esp_err_t app_audio_player_init(void)
{
#if CONFIG_ENABLE_FACTORY_FW_DEBUG_LOG
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
#endif

    esp_err_t ret = ESP_OK;
    struct app_audio_player * p_audio_player = NULL;
    gp_audio_player = (struct app_audio_player *) psram_malloc(sizeof(struct app_audio_player));
    if (gp_audio_player == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    p_audio_player = gp_audio_player;
    memset(p_audio_player, 0, sizeof( struct app_audio_player ));
    
    p_audio_player->sem_handle = xSemaphoreCreateMutex();
    ESP_GOTO_ON_FALSE(NULL != p_audio_player->sem_handle, ESP_ERR_NO_MEM, err, TAG, "Failed to create semaphore");

    p_audio_player->p_rb_storage = (uint8_t *)psram_malloc(AUDIO_PLAYER_RINGBUF_SIZE);
    ESP_GOTO_ON_FALSE(NULL != p_audio_player->p_rb_storage, ESP_ERR_NO_MEM, err, TAG, "Failed to create rb storage");

    p_audio_player->rb_handle = xRingbufferCreateStatic(AUDIO_PLAYER_RINGBUF_SIZE, RINGBUF_TYPE_BYTEBUF, p_audio_player->p_rb_storage, &p_audio_player->rb_ins);
    ESP_GOTO_ON_FALSE(NULL != p_audio_player->rb_handle, ESP_ERR_NO_MEM, err, TAG, "Failed to create rb");

    p_audio_player->event_group = xEventGroupCreate();
    ESP_GOTO_ON_FALSE(NULL != p_audio_player->event_group, ESP_ERR_NO_MEM, err, TAG, "Failed to create event_group");

    p_audio_player->p_task_stack_buf = (StackType_t *)psram_malloc(AUDIO_PLAYER_TASK_STACK_SIZE);
    ESP_GOTO_ON_FALSE(NULL != p_audio_player->p_task_stack_buf, ESP_ERR_NO_MEM, err, TAG, "Failed to malloc task stack");

    p_audio_player->p_task_buf =  heap_caps_malloc(sizeof(StaticTask_t),  MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    ESP_GOTO_ON_FALSE(NULL != p_audio_player->p_task_buf, ESP_ERR_NO_MEM, err, TAG, "Failed to malloc task TCB");

    p_audio_player->task_handle = xTaskCreateStaticPinnedToCore(app_audio_player_task,
                                                                "app_audio_player",
                                                                AUDIO_PLAYER_TASK_STACK_SIZE,
                                                                (void *)p_audio_player,
                                                                AUDIO_PLAYER_TASK_PRIO,
                                                                p_audio_player->p_task_stack_buf,
                                                                p_audio_player->p_task_buf,
                                                                AUDIO_PLAYER_TASK_CORE);
    ESP_GOTO_ON_FALSE(p_audio_player->task_handle, ESP_FAIL, err, TAG, "Failed to create task");


    audio_player_config_t config = { .mute_fn = __audio_player_mute_set,
                                    .write_fn = bsp_i2s_write,
                                    .clk_set_fn = __audio_player_set_fs,
                                    .priority = 13
                                };
    ret = audio_player_new(config);
    ESP_GOTO_ON_FALSE(ret==ESP_OK, ESP_FAIL, err, TAG, "Failed to create audio player");
    audio_player_callback_register(__audio_player_cb, (void *)p_audio_player);

#if defined(CONFIG_AUDIO_PLAYER_ENABLE_MP3_STREAM)
/** See https://github.com/ultraembedded/libhelix-mp3/blob/0a0e0673f82bc6804e5a3ddb15fb6efdcde747cd/testwrap/main.c#L74 */
    p_audio_player->mp3_decode_buf_len = MAX_NCHAN * MAX_NGRAN * MAX_NSAMP;
    p_audio_player->p_mp3_decode_buf = psram_malloc(p_audio_player->mp3_decode_buf_len);
    ESP_GOTO_ON_FALSE(NULL != p_audio_player->p_mp3_decode_buf, ESP_ERR_NO_MEM, err, TAG, "Failed allocate mp3 decode buffer");

    p_audio_player->mp3_data.data_buf_size = MAINBUF_SIZE * 3;
    p_audio_player->mp3_data.data_buf = psram_malloc(MAINBUF_SIZE * 3);
    ESP_GOTO_ON_FALSE(NULL != p_audio_player->mp3_data.data_buf, ESP_ERR_NO_MEM, err, TAG, "Failed allocate mp3 data buffer");

    p_audio_player->mp3_decoder = MP3InitDecoder();
    ESP_GOTO_ON_FALSE(NULL != p_audio_player->mp3_decoder, ESP_ERR_NO_MEM, err,TAG, "Failed create MP3 decoder");


#endif

    return ESP_OK;

err:

#if defined(CONFIG_AUDIO_PLAYER_ENABLE_MP3_STREAM)
    if(p_audio_player->mp3_decoder) {
        MP3FreeDecoder(p_audio_player->mp3_decoder);
        p_audio_player->mp3_decoder = NULL;
    }
    if(p_audio_player->mp3_data.data_buf) {
        free(p_audio_player->mp3_data.data_buf);
        p_audio_player->mp3_data.data_buf = NULL;
    }
    if( p_audio_player->p_mp3_decode_buf ) {
        free(p_audio_player->p_mp3_decode_buf);
        p_audio_player->p_mp3_decode_buf = NULL;
    }
#endif
    if(p_audio_player->task_handle ) {
        vTaskDelete(p_audio_player->task_handle);
        p_audio_player->task_handle = NULL;
    }
    if( p_audio_player->p_task_stack_buf ) {
        free(p_audio_player->p_task_stack_buf);
        p_audio_player->p_task_stack_buf = NULL;
    }
    if( p_audio_player->p_task_buf ) {   
        free(p_audio_player->p_task_buf);
        p_audio_player->p_task_buf = NULL;
    }
    if (p_audio_player->event_group) {
        vEventGroupDelete(p_audio_player->event_group);
        p_audio_player->event_group = NULL;
    }
    if( p_audio_player->rb_handle ) {
        vRingbufferDelete(p_audio_player->rb_handle);
        p_audio_player->rb_handle = NULL;
    }
    if( p_audio_player->p_rb_storage ) {
        free(p_audio_player->p_rb_storage);
        p_audio_player->p_rb_storage = NULL;
    }   
    if (p_audio_player->sem_handle) {
        vSemaphoreDelete(p_audio_player->sem_handle);
        p_audio_player->sem_handle = NULL;
    }
    if (p_audio_player) {
        free(p_audio_player);
        gp_audio_player = NULL;
    }
    ESP_LOGE(TAG, "app_audio_player_init fail %d!", ret);
    return ret;
}

int app_audio_player_status_get(void)
{
    struct app_audio_player * p_audio_player = gp_audio_player;
    if( p_audio_player == NULL) {
        return AUDIO_PLAYER_STATUS_IDLE;
    }
    return p_audio_player->status;
}

esp_err_t app_audio_player_stop(void)
{
    struct app_audio_player * p_audio_player = gp_audio_player;
    if( p_audio_player == NULL) {
        return ESP_FAIL;
    }
    __data_lock(p_audio_player);
    if( p_audio_player->status != AUDIO_PLAYER_STATUS_IDLE) {
        xEventGroupSetBits(p_audio_player->event_group, EVENT_STREAM_STOP); // TODO
    }
    p_audio_player->status = AUDIO_PLAYER_STATUS_IDLE;
    __data_unlock(p_audio_player);
    return ESP_OK;
}

esp_err_t app_audio_player_stream_init(size_t len)
{ 
    struct app_audio_player * p_audio_player = gp_audio_player;
    if( p_audio_player == NULL) {
        return ESP_FAIL;
    }

    esp_err_t ret = ESP_OK;
    void *tmp = NULL;
    size_t tmp_len = 0;

    //clear the ringbuffer
    while ((tmp = xRingbufferReceiveUpTo(p_audio_player->rb_handle, &tmp_len, 0, AUDIO_PLAYER_RINGBUF_SIZE))) {
        vRingbufferReturnItem(p_audio_player->rb_handle, tmp);
    }

    ret = __audio_player_set_fs(DRV_AUDIO_SAMPLE_RATE, DRV_AUDIO_SAMPLE_BITS, DRV_AUDIO_CHANNELS );
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "set fs fail %d!", ret);
    }

    __data_lock(p_audio_player);
    p_audio_player->status = AUDIO_PLAYER_STATUS_PLAYING_STREAM;
    p_audio_player->audio_type = AUDIO_TYPE_UNKNOWN;
    p_audio_player->sample_rate = DRV_AUDIO_SAMPLE_RATE;
    p_audio_player->bits_per_sample = DRV_AUDIO_SAMPLE_BITS;
    p_audio_player->channel = DRV_AUDIO_CHANNELS;
    p_audio_player->stream_finished = false;
    p_audio_player->stream_total_len = len;
    p_audio_player->stream_play_len = 0;
    p_audio_player->stream_need_cache = true; // start need cache
    __data_unlock(p_audio_player);

    return ret;
}


esp_err_t app_audio_player_stream_start(void)
{
    struct app_audio_player * p_audio_player = gp_audio_player;
    if( p_audio_player == NULL) {
        return ESP_FAIL;
    }
    xEventGroupClearBits(p_audio_player->event_group, EVENT_STREAM_STOP);
    xEventGroupSetBits(p_audio_player->event_group, EVENT_STREAM_START);
    return ESP_OK;
}

esp_err_t app_audio_player_stream_send(uint8_t *p_buf, 
                                        size_t len, 
                                        TickType_t xTicksToWait)
{
    struct app_audio_player * p_audio_player = gp_audio_player;
    if( p_audio_player == NULL) {
        return ESP_FAIL;
    }
    switch (p_audio_player->audio_type)
    {
        case AUDIO_TYPE_UNKNOWN: {

            if( __is_wav(p_audio_player, p_buf, len) ) {
                ESP_LOGI(TAG, "WAV audio stream");
                p_audio_player->audio_type = AUDIO_TYPE_WAV;
                int header_len = sizeof(audio_wav_header_t);
                if(xRingbufferSend(p_audio_player->rb_handle, p_buf + header_len, len - header_len, xTicksToWait) == pdTRUE) {
                    return ESP_FAIL;
                }
#if defined(CONFIG_AUDIO_PLAYER_ENABLE_MP3_STREAM)
            } else if( __is_mp3(p_audio_player, p_buf, len)){
                ESP_LOGI(TAG, "mp3 audio stream");
                p_audio_player->audio_type = AUDIO_TYPE_MP3;

                p_audio_player->mp3_data.bytes_in_data_buf = 0;
                p_audio_player->mp3_data.read_ptr = p_audio_player->mp3_data.data_buf;
                p_audio_player->mp3_data.eof_reached = false;

                if(xRingbufferSend(p_audio_player->rb_handle, p_buf, len, xTicksToWait) == pdTRUE) {
                    return ESP_FAIL;
                }
#endif
            } else {
                ESP_LOGE(TAG, "unsupport audio stream");
            }
            break;
        }
        case AUDIO_TYPE_WAV: {
            if(xRingbufferSend(p_audio_player->rb_handle, p_buf, len, xTicksToWait) != pdTRUE) {
                ESP_LOGE(TAG, "WAV send ringbuffer fail");
                return ESP_FAIL;
            }
            break;
        }  
        case AUDIO_TYPE_MP3: {
            if(xRingbufferSend(p_audio_player->rb_handle, p_buf, len, xTicksToWait) != pdTRUE) {
                ESP_LOGE(TAG, "MP3 send ringbuffer fail");
                return ESP_FAIL;
            }
            break;
        }  
        default:
            break;
    }
    return ESP_OK;
}

esp_err_t app_audio_player_stream_finish(void)
{
    struct app_audio_player * p_audio_player = gp_audio_player;
    if( p_audio_player == NULL) {
        return ESP_FAIL;
    }
    __data_lock(p_audio_player);
    p_audio_player->stream_finished = true;
    __data_unlock(p_audio_player);

    return ESP_OK;
}

esp_err_t app_audio_player_stream_stop(void)
{
    struct app_audio_player * p_audio_player = gp_audio_player;
    if( p_audio_player == NULL) {
        return ESP_FAIL;
    }

    __data_lock(p_audio_player);
    p_audio_player->status = AUDIO_PLAYER_STATUS_IDLE;
    __data_unlock(p_audio_player);

    xEventGroupSetBits(p_audio_player->event_group, EVENT_STREAM_STOP);
    // xEventGroupWaitBits(p_audio_player->event_group, EVENT_STREAM_STOP_DONE, 1, 1, pdMS_TO_TICKS(1000));

    //clear the ringbuffer
    void *tmp = NULL;
    size_t len = 0;
    while ((tmp = xRingbufferReceiveUpTo(p_audio_player->rb_handle, &len, 0, AUDIO_PLAYER_RINGBUF_SIZE))) {
        vRingbufferReturnItem(p_audio_player->rb_handle, tmp);
    }
#ifndef CONFIG_ENABLE_VI_SR
    bsp_codec_dev_stop(); //TODO
#endif
    return ESP_OK;
}

int app_audio_player_stream_time_get(int len)
{
    struct app_audio_player * p_audio_player = gp_audio_player;
    if( p_audio_player == NULL || len == 0) {
        return 0;
    }
    int ms = 0;
    switch (p_audio_player->audio_type)
    {
        case AUDIO_TYPE_WAV: {
            ms = (len * 1000) / (p_audio_player->sample_rate * p_audio_player->bits_per_sample * p_audio_player->channel / 8);
            break;
        }  
        case AUDIO_TYPE_MP3: {
            ms = (len * 1725) / 3456; // Inaccurate calculations. TODO 
            break;
        }  
        default:
            break;
    }
    return ms;
}
esp_err_t app_audio_player_file(void *p_filepath)
{
    struct app_audio_player * p_audio_player = gp_audio_player;
    if( p_audio_player == NULL) {
        return ESP_FAIL;
    }
    FILE *fp = NULL;
    storage_file_open(p_filepath, &fp);
    // fp = fopen(p_filepath, "r");
    if( fp ) {
        esp_err_t status  = audio_player_play(fp);
        if( status == ESP_OK ) {
            __data_lock(p_audio_player);
            p_audio_player->status = AUDIO_PLAYER_STATUS_PLAYING_FILE;
            __data_unlock(p_audio_player);
            ESP_LOGI(TAG, "play file %s", p_filepath);
            return ESP_OK;
        }
    } else {
        ESP_LOGE(TAG, "open file %s fail", p_filepath);
    }
    return ESP_FAIL;
}

esp_err_t app_audio_player_file_block(void *p_filepath, TickType_t xTicksToWait)
{
    struct app_audio_player * p_audio_player = gp_audio_player;
    if( p_audio_player == NULL) {
        return ESP_FAIL;
    }
    xEventGroupClearBits(p_audio_player->event_group, EVENT_FILE_MEM_DONE);
    esp_err_t ret = app_audio_player_file(p_filepath);
    if( ret == ESP_OK ) {
        EventBits_t bits = xEventGroupWaitBits(p_audio_player->event_group, 
                            EVENT_FILE_MEM_DONE, pdTRUE, pdTRUE, xTicksToWait);
        if( !(bits & EVENT_FILE_MEM_DONE)) {
            ESP_LOGW(TAG, "play timeout");
            audio_player_stop();
            __data_lock(p_audio_player);
            p_audio_player->status = AUDIO_PLAYER_STATUS_IDLE;
            __data_unlock(p_audio_player); 
        }
    }
    return ret;
}

esp_err_t app_audio_player_mem(uint8_t *p_buf, size_t len, bool is_need_free)
{
    struct app_audio_player * p_audio_player = gp_audio_player;
    if( p_audio_player == NULL) {
        return ESP_FAIL;
    }
    FILE *fp = NULL;
    fp = fmemopen((void *)p_buf, len, "rb");
    if( fp ) {
        esp_err_t status  = audio_player_play(fp);
        if( status == ESP_OK ) {
            __data_lock(p_audio_player);
            p_audio_player->status = AUDIO_PLAYER_STATUS_PLAYING_MEM;
            p_audio_player->mem_need_free = is_need_free;
            p_audio_player->p_mem_buf = p_buf;
            __data_unlock(p_audio_player);
            ESP_LOGI(TAG, "play mem: %d", len);
            return ESP_OK;
        }
    } else {
        ESP_LOGE(TAG, "open mem fail");
    }
    return ESP_FAIL;
}

esp_err_t app_audio_player_mem_block(uint8_t *p_buf, size_t len, bool is_need_free, TickType_t xTicksToWait)
{
    struct app_audio_player * p_audio_player = gp_audio_player;
    if( p_audio_player == NULL) {
        return ESP_FAIL;
    }
    xEventGroupClearBits(p_audio_player->event_group, EVENT_FILE_MEM_DONE);
    esp_err_t ret = app_audio_player_mem(p_buf,len, is_need_free);
    if( ret == ESP_OK ) {
        EventBits_t bits = xEventGroupWaitBits(p_audio_player->event_group, 
                            EVENT_FILE_MEM_DONE, pdTRUE, pdTRUE, xTicksToWait);
        if( !(bits & EVENT_FILE_MEM_DONE)) {
            ESP_LOGW(TAG, "play timeout");
            audio_player_stop();
            
            __data_lock(p_audio_player);
            if( p_audio_player->mem_need_free && p_audio_player->p_mem_buf != NULL) {
                free(p_audio_player->p_mem_buf);
                p_audio_player->mem_need_free = false;
                p_audio_player->p_mem_buf = NULL;
            }
            p_audio_player->status = AUDIO_PLAYER_STATUS_IDLE;
            __data_unlock(p_audio_player);
        }
    }
    return ret;
}
