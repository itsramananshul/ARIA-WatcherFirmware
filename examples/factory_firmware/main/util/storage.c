#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "nvs_flash.h"
#include "esp_check.h"
#include "esp_err.h"

#include "storage.h"
#include "event_loops.h"

#define STORAGE_NAMESPACE "UserCfg"

ESP_EVENT_DEFINE_BASE(STORAGE_EVENT_BASE);

enum {
    EVENT_STG_WRITE,
    EVENT_STG_READ,
    EVENT_STG_ERASE,
    EVENT_STG_FILE_WRITE,
    EVENT_STG_FILE_READ,
    EVENT_STG_FILE_SIZE_GET,
    EVENT_STG_FILE_REMOVE,
    EVENT_STG_FILE_OPEN,
};

typedef struct
{
    SemaphoreHandle_t sem;
    char *key;
    void *data;
    size_t len;
    esp_err_t err;
} storage_event_data_t;

static esp_err_t __storage_write(char *p_key, void *p_data, size_t len)
{
    nvs_handle_t my_handle;
    esp_err_t err;
    err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK)
        return err;

    err = nvs_set_blob(my_handle, p_key, p_data, len);
    if (err != ESP_OK)
    {
        nvs_close(my_handle);
        return err;
    }
    err = nvs_commit(my_handle);
    if (err != ESP_OK)
    {
        nvs_close(my_handle);
        return err;
    }
    nvs_close(my_handle);
    return ESP_OK;
}

static esp_err_t __storage_read(char *p_key, void *p_data, size_t *p_len)
{
    nvs_handle_t my_handle;
    esp_err_t err;

    err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK)
        return err;

    err = nvs_get_blob(my_handle, p_key, p_data, p_len);
    if (err != ESP_OK)
    {
        nvs_close(my_handle);
        return err;
    }
    nvs_close(my_handle);
    return ESP_OK;
}

static esp_err_t __storage_file_open(char *file, FILE **pp_fp)
{
    *pp_fp = fopen(file, "r");
    return ESP_OK;
}

static esp_err_t __storage_file_write(char *file, void *p_data, size_t len)
{
    FILE *fp = fopen(file, "w");
    if( fp != NULL ) {
        fwrite(p_data,len, 1, fp);
        fclose(fp);
        return ESP_OK;
    } else {
        return ESP_FAIL;
    }
}

static esp_err_t __storage_file_read(char *file, void *p_data, size_t *p_len)
{
    FILE *fp = fopen(file, "r");
    if( fp != NULL ) {
        fseek(fp, 0, SEEK_END);
        int len = ftell(fp);
        len = len > *p_len ? *p_len : len; //check len
        fseek(fp, 0, SEEK_SET);
        fread(p_data, len, 1, fp);
        *p_len = len;
        fclose(fp);
        return ESP_OK;
    } else {
        return ESP_FAIL;
    }
}

static esp_err_t __storage_file_size_get(char *file, size_t *p_len)
{
    FILE *fp = fopen(file, "r");
    if( fp != NULL ) {
        fseek(fp, 0, SEEK_END);
        int len = ftell(fp);
        *p_len = len;
        fclose(fp);
        return ESP_OK;
    } else {
        return ESP_FAIL;
    }
}

static esp_err_t __storage_file_remove(char *file)
{
    int ret;
    ret = remove(file);
   if(ret == 0) {
        return ESP_OK;
    } else  {
        return ESP_FAIL;
    }
}



static void __storage_event_handler(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    storage_event_data_t *evtdata = *(storage_event_data_t **)event_data;

    switch (id)
    {
        case EVENT_STG_WRITE:
            evtdata->err = __storage_write(evtdata->key, evtdata->data, evtdata->len);
            xSemaphoreGive(evtdata->sem);
            break;
        case EVENT_STG_READ:
            evtdata->err = __storage_read(evtdata->key, evtdata->data, &(evtdata->len));
            xSemaphoreGive(evtdata->sem);
            break;
        case EVENT_STG_ERASE:
             evtdata->err = nvs_flash_erase();
            xSemaphoreGive(evtdata->sem);
            break;
        case EVENT_STG_FILE_WRITE:
            evtdata->err = __storage_file_write(evtdata->key, evtdata->data, evtdata->len);
            xSemaphoreGive(evtdata->sem);
            break;
        case EVENT_STG_FILE_READ:
            evtdata->err = __storage_file_read(evtdata->key, evtdata->data, &(evtdata->len));
            xSemaphoreGive(evtdata->sem);
            break;
        case EVENT_STG_FILE_SIZE_GET:
            evtdata->err = __storage_file_size_get(evtdata->key, &(evtdata->len));
            xSemaphoreGive(evtdata->sem);
            break;
        case EVENT_STG_FILE_REMOVE:
            evtdata->err = __storage_file_remove(evtdata->key);
            xSemaphoreGive(evtdata->sem);
            break;
        case EVENT_STG_FILE_OPEN:
            FILE *fp = NULL;
            evtdata->err = __storage_file_open(evtdata->key, &fp);
            evtdata->data = (void *)fp;
            xSemaphoreGive(evtdata->sem);
            break;
        default:
        
            break;
    }
}

int storage_init(void)
{
    // ESP_ERROR_CHECK(nvs_flash_erase());
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    ESP_ERROR_CHECK(esp_event_handler_register_with(app_event_loop_handle, STORAGE_EVENT_BASE, ESP_EVENT_ANY_ID, __storage_event_handler, NULL));

    return ret;
}

esp_err_t storage_write(char *p_key, void *p_data, size_t len)
{
    TaskHandle_t h = xTaskGetCurrentTaskHandle();
    if (strcmp(pcTaskGetName(h), "app_eventloop") == 0)
    {
        return __storage_write(p_key, p_data, len);
    }

    storage_event_data_t evtdata = { .sem = xSemaphoreCreateBinary(), .key = p_key, .data = p_data, .len = len, .err = ESP_OK };
    storage_event_data_t *pevtdata = &evtdata;

    esp_event_post_to(app_event_loop_handle, STORAGE_EVENT_BASE, EVENT_STG_WRITE, &pevtdata, sizeof(storage_event_data_t *), pdMS_TO_TICKS(10000));
    xSemaphoreTake(evtdata.sem, portMAX_DELAY);
    vSemaphoreDelete(evtdata.sem);

    return evtdata.err;
}

esp_err_t storage_read(char *p_key, void *p_data, size_t *p_len)
{
    TaskHandle_t h = xTaskGetCurrentTaskHandle();
    if (strcmp(pcTaskGetName(h), "app_eventloop") == 0)
    {
        return __storage_read(p_key, p_data, p_len);
    }

    storage_event_data_t evtdata = { .sem = xSemaphoreCreateBinary(), .key = p_key, .data = p_data, .len = *p_len, .err = ESP_OK };
    storage_event_data_t *pevtdata = &evtdata;

    esp_event_post_to(app_event_loop_handle, STORAGE_EVENT_BASE, EVENT_STG_READ, &pevtdata, sizeof(storage_event_data_t *), pdMS_TO_TICKS(10000));
    xSemaphoreTake(evtdata.sem, portMAX_DELAY);
    vSemaphoreDelete(evtdata.sem);

    *p_len = evtdata.len;

    return evtdata.err;
}

esp_err_t storage_erase()
{
    TaskHandle_t h = xTaskGetCurrentTaskHandle();
    if (strcmp(pcTaskGetName(h), "app_eventloop") == 0)
    {
        return nvs_flash_erase();
    }

    storage_event_data_t evtdata = {
        .sem = xSemaphoreCreateBinary(),
    };
    storage_event_data_t *pevtdata = &evtdata;

    esp_event_post_to(app_event_loop_handle, STORAGE_EVENT_BASE, EVENT_STG_ERASE, &pevtdata, sizeof(storage_event_data_t *), pdMS_TO_TICKS(10000));
    xSemaphoreTake(evtdata.sem, portMAX_DELAY);
    vSemaphoreDelete(evtdata.sem);

    return evtdata.err;
}

esp_err_t storage_file_write(char *file, void *p_data, size_t len)
{
    TaskHandle_t h = xTaskGetCurrentTaskHandle();
    if (strcmp(pcTaskGetName(h), "app_eventloop") == 0)
    {
        return __storage_file_write(file, p_data, len);
    }

    storage_event_data_t evtdata = { .sem = xSemaphoreCreateBinary(), .key = file, .data = p_data, .len = len, .err = ESP_OK };
    storage_event_data_t *pevtdata = &evtdata;

    esp_event_post_to(app_event_loop_handle, STORAGE_EVENT_BASE, EVENT_STG_FILE_WRITE, &pevtdata, sizeof(storage_event_data_t *), pdMS_TO_TICKS(10000));
    xSemaphoreTake(evtdata.sem, portMAX_DELAY);
    vSemaphoreDelete(evtdata.sem);

    return evtdata.err;
}

esp_err_t storage_file_read(char *file, void *p_data, size_t *p_len)
{
    TaskHandle_t h = xTaskGetCurrentTaskHandle();
    if (strcmp(pcTaskGetName(h), "app_eventloop") == 0)
    {
        return __storage_file_read(file, p_data, p_len);
    }

    storage_event_data_t evtdata = { .sem = xSemaphoreCreateBinary(), .key = file, .data = p_data, .len = *p_len, .err = ESP_OK };
    storage_event_data_t *pevtdata = &evtdata;

    esp_event_post_to(app_event_loop_handle, STORAGE_EVENT_BASE, EVENT_STG_FILE_READ, &pevtdata, sizeof(storage_event_data_t *), pdMS_TO_TICKS(10000));
    xSemaphoreTake(evtdata.sem, portMAX_DELAY);
    vSemaphoreDelete(evtdata.sem);

    *p_len = evtdata.len;

    return evtdata.err;
}

esp_err_t storage_file_size_get(char *file, size_t *p_len)
{
    TaskHandle_t h = xTaskGetCurrentTaskHandle();
    if (strcmp(pcTaskGetName(h), "app_eventloop") == 0)
    {
        return __storage_file_size_get(file, p_len);
    }

    storage_event_data_t evtdata = { .sem = xSemaphoreCreateBinary(), .key = file, .err = ESP_OK };
    storage_event_data_t *pevtdata = &evtdata;

    esp_event_post_to(app_event_loop_handle, STORAGE_EVENT_BASE, EVENT_STG_FILE_SIZE_GET, &pevtdata, sizeof(storage_event_data_t *), pdMS_TO_TICKS(10000));
    xSemaphoreTake(evtdata.sem, portMAX_DELAY);
    vSemaphoreDelete(evtdata.sem);

    *p_len = evtdata.len;

    return evtdata.err;
}

esp_err_t storage_file_remove(char *file)
{
    TaskHandle_t h = xTaskGetCurrentTaskHandle();
    if (strcmp(pcTaskGetName(h), "app_eventloop") == 0)
    {
        return __storage_file_remove(file);
    }

    storage_event_data_t evtdata = { .sem = xSemaphoreCreateBinary(), .key = file, .err = ESP_OK };
    storage_event_data_t *pevtdata = &evtdata;

    esp_event_post_to(app_event_loop_handle, STORAGE_EVENT_BASE, EVENT_STG_FILE_REMOVE, &pevtdata, sizeof(storage_event_data_t *), pdMS_TO_TICKS(10000));
    xSemaphoreTake(evtdata.sem, portMAX_DELAY);
    vSemaphoreDelete(evtdata.sem);

    return evtdata.err;
}

esp_err_t storage_file_open(char *file, FILE **pp_fp)
{
    TaskHandle_t h = xTaskGetCurrentTaskHandle();
    if (strcmp(pcTaskGetName(h), "app_eventloop") == 0)
    {
        return __storage_file_open(file, pp_fp);
    }

    storage_event_data_t evtdata = { .sem = xSemaphoreCreateBinary(), .key = file, .err = ESP_OK };
    storage_event_data_t *pevtdata = &evtdata;

    esp_event_post_to(app_event_loop_handle, STORAGE_EVENT_BASE, EVENT_STG_FILE_OPEN, &pevtdata, sizeof(storage_event_data_t *), pdMS_TO_TICKS(10000));
    xSemaphoreTake(evtdata.sem, portMAX_DELAY);
    vSemaphoreDelete(evtdata.sem);

    *pp_fp = (FILE *)evtdata.data;

    return evtdata.err;
}
