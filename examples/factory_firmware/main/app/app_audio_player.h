#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/ringbuf.h"
#include "sensecap-watcher.h"
#include "mp3dec.h"

#define AUDIO_PLAYER_TASK_STACK_SIZE  5*1024
#define AUDIO_PLAYER_TASK_PRIO        13
#define AUDIO_PLAYER_TASK_CORE        1

// sample rate: 16000, bit depth: 16, channels: 1; 32000 size per second;
// 10*32000  can cache 10s of audio. 
#define AUDIO_PLAYER_RINGBUF_SIZE         8*32000      
#define AUDIO_PLAYER_RINGBUF_CACHE_SIZE   1*32000  //If the audio content is large, maybe cache 2s
#define AUDIO_PLAYER_RINGBUF_CHUNK_SIZE   16000

#define CONFIG_AUDIO_PLAYER_ENABLE_MP3_STREAM

typedef struct {
    // The "RIFF" chunk descriptor
    uint8_t ChunkID[4];// Indicates the file as "RIFF" file
    int32_t ChunkSize;// The total size of the entire file, excluding the "RIFF" and the header itself, which is the file size minus 8 bytes.
    uint8_t Format[4];// File format header indicating a "WAVE" file.
    // The "fmt" sub-chunk
    uint8_t Subchunk1ID[4];// Format identifier for the "fmt" sub-chunk.
    int32_t Subchunk1Size;// The length of the fmt sub-chunk (subchunk1) excluding the Subchunk1 ID and Subchunk1 Size fields. It is typically 16, but a value greater than 16 indicates the presence of an extended area. Optional values for the length include 16, 18, 20, 40, etc.
    int16_t AudioFormat;// Audio encoding format, which represents the compression format. A value of 0x01 indicates PCM format, which is uncompressed. Please refer to table 3 for more details.
    int16_t NumChannels;// Number of audio channels
    int32_t SampleRate;// Sample rate, for example, "44100" represents a sampling rate of 44100 Hz.
    int32_t ByteRate;// Bit rate: Sample rate x bit depth x number of channels / 8. For example, the bit rate for a stereo (2 channels) audio with a sample rate of 44.1 kHz and 16-bit depth would be 176400 bits per second.
    int16_t BlockAlign;// Memory size occupied by one sample: Bit depth x number of channels / 8.
    int16_t BitsPerSample;//Sample depth, also known as bit depth.
    // The "data" sub-chunk
    uint8_t Subchunk2ID[4];// Total length of the audio data, which is the file size minus the length of the WAV file header.
    int32_t Subchunk2Size;// Length of the data section, referring to the size of the audio data excluding the header.
} audio_wav_header_t;


#if defined(CONFIG_AUDIO_PLAYER_ENABLE_MP3_STREAM)
typedef struct {
    char header[3];     /*!< Always "TAG" */
    char title[30];     /*!< Audio title */
    char artist[30];    /*!< Audio artist */
    char album[30];     /*!< Album name */
    char year[4];       /*!< Char array of year */
    char comment[30];   /*!< Extra comment */
    char genre;         /*!< See "https://en.wikipedia.org/wiki/ID3" */
} __attribute__((packed)) audio_mp3_id3_header_v1_t;

typedef struct {
    char header[3];     /*!< Always "ID3" */
    char ver;           /*!< Version, equals to3 if ID3V2.3 */
    char revision;      /*!< Revision, should be 0 */
    char flag;          /*!< Flag byte, use Bit[7..5] only */
    char size[4];       /*!< TAG size */
} __attribute__((packed)) audio_mp3_id3_header_v2_t;

typedef struct {
    // Constants below
    uint8_t *data_buf;

    /** number of bytes in data_buf */
    size_t data_buf_size;

    // Values that change at runtime are below

    /**
     * Total bytes in data_buf,
     * not the number of bytes remaining after the read_ptr
     */
    size_t bytes_in_data_buf;

    /** Pointer to read location in data_buf */
    uint8_t *read_ptr;

    // set to true if the end of file has been reached
    bool eof_reached;
} audio_mp3_instance;
#endif

enum app_audio_player_status {
    AUDIO_PLAYER_STATUS_IDLE = 0,
    AUDIO_PLAYER_STATUS_PLAYING_FILE,
    AUDIO_PLAYER_STATUS_PLAYING_MEM,
    AUDIO_PLAYER_STATUS_PLAYING_STREAM,
};
enum app_audio_player_type {
    AUDIO_TYPE_UNKNOWN = 0,
    AUDIO_TYPE_WAV,
    AUDIO_TYPE_MP3,
};


struct app_audio_player {
    SemaphoreHandle_t sem_handle;
    RingbufHandle_t rb_handle;
    StaticRingbuffer_t rb_ins;
    uint8_t * p_rb_storage;
    EventGroupHandle_t event_group;
    TaskHandle_t task_handle;
    StaticTask_t *p_task_buf;
    StackType_t *p_task_stack_buf;
    enum app_audio_player_status status;
    enum app_audio_player_type audio_type;
    uint32_t sample_rate;
    uint8_t  bits_per_sample;
    i2s_slot_mode_t  channel;
    size_t stream_total_len;
    size_t stream_play_len;
    bool stream_finished;
    bool stream_need_cache;
    bool mem_need_free;
    void *p_mem_buf;
#if defined(CONFIG_AUDIO_PLAYER_ENABLE_MP3_STREAM)
    HMP3Decoder mp3_decoder;
    audio_mp3_instance mp3_data;
    uint8_t *p_mp3_decode_buf;
    size_t mp3_decode_buf_len;
#endif
};

esp_err_t app_audio_player_init(void);

int app_audio_player_status_get(void);

esp_err_t app_audio_player_stop(void);

esp_err_t app_audio_player_stream_init(size_t len);

// will decode audio data from p_buf, and send to ring buffer
esp_err_t app_audio_player_stream_send(uint8_t *p_buf, 
                                        size_t len, 
                                        TickType_t xTicksToWait);

// start play stream from ring buffer
esp_err_t app_audio_player_stream_start(void);

esp_err_t app_audio_player_stream_finish(void);

esp_err_t app_audio_player_stream_stop(void);

//return ms
int app_audio_player_stream_time_get(int len);

esp_err_t app_audio_player_file(void *p_filepath);
esp_err_t app_audio_player_file_block(void *p_filepath, TickType_t xTicksToWait);

esp_err_t app_audio_player_mem(uint8_t *p_buf, size_t len, bool is_need_free);
esp_err_t app_audio_player_mem_block(uint8_t *p_buf, size_t len, bool is_need_free, TickType_t xTicksToWait);
