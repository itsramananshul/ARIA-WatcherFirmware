#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include "nvs.h"
#include "nvs_flash.h"

int storage_init(void);
esp_err_t storage_write(char *p_key, void *p_data, size_t len);
esp_err_t storage_read(char *p_key, void *p_data, size_t *p_len);  //p_len : inout
esp_err_t storage_erase();

//eg: file: /spiffs/test.text
esp_err_t storage_file_write(char *file, void *p_data, size_t len);
esp_err_t storage_file_read(char *file, void *p_data, size_t *p_len); //p_len : inout
esp_err_t storage_file_size_get(char *file, size_t *p_len);
esp_err_t storage_file_remove(char *file);
esp_err_t storage_file_open(char *file, FILE **pp_fp);

#ifdef __cplusplus
}
#endif
