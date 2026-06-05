#include "tf_util.h"
#include "esp_heap_caps.h"
#include "cJSON.h"

void *tf_malloc(size_t sz)
{
    return heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

void tf_free(void *ptr)
{
    free(ptr);
}

bool tf_cJSON_IsGeneralBool(const cJSON * const item)
{
    return cJSON_IsBool(item) || cJSON_IsNumber(item);
}

bool tf_cJSON_IsGeneralTrue(const cJSON * const item)
{
    if (cJSON_IsBool(item)) return cJSON_IsTrue(item);
    else if (cJSON_IsNumber(item)) return (item->valueint != 0);
    else return false;
}

char *tf_strdup(const char *s)
{
    size_t len = strlen(s) + 1;
    void *new = heap_caps_calloc(1, len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (new == NULL)
        return NULL;
    return (char *)memcpy(new, s, len);
}
