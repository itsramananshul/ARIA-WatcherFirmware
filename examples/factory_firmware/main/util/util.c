#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "freertos/FreeRTOS.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "cJSON.h">

#include "util.h"

int wifi_rssi_level_get(int rssi)
{
    //    0    rssi<=-100
    //    1    (-100, -88]
    //    2    (-88, -77]
    //    3    (-66, -55]
    //    4    rssi>=-55
    if( rssi > -55 ) {
        return 4;
    } else if( rssi > -66 ) {
        return 3;
    } else if( rssi > -88) {
        return 2;
    } else {
        return 1;
    }
}

time_t util_get_timestamp_ms(void)
{
    time_t now_ms;

    struct timeval tv_now;
    gettimeofday(&tv_now, NULL);
    now_ms = (time_t)((int64_t)tv_now.tv_sec * 1000 + (int64_t)tv_now.tv_usec/1000);

    return now_ms;
}

void byte_array_to_hex_string(const uint8_t *byteArray, size_t byteArraySize, char *hexString)
{
    for (size_t i = 0; i < byteArraySize; ++i)
    {
        sprintf(&hexString[2 * i], "%02X", byteArray[i]);
    }
}

void string_to_byte_array(const char *str, uint8_t *byte_array, size_t length)
{
    for (size_t i = 0; i < length; i++)
    {
        sscanf(str + 2 * i, "%2hhx", &byte_array[i]);
    }
}

void *psram_malloc(size_t sz)
{
    return heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
}

void *psram_calloc(size_t n, size_t sz)
{
    return heap_caps_calloc(n, sz, MALLOC_CAP_SPIRAM);
}

void *psram_realloc(void *ptr, size_t new_sz)
{
    return heap_caps_realloc(ptr, new_sz, MALLOC_CAP_SPIRAM);
}

// copied from task.c
static char * WriteNameToBuffer( char * pcBuffer, const char * pcTaskName )
{
    size_t x;

    /* Start by copying the entire string. */
    strcpy( pcBuffer, pcTaskName );

    /* Pad the end of the string with spaces to ensure columns line up when
        * printed out. */
    for( x = strlen( pcBuffer ); x < ( size_t ) ( configMAX_TASK_NAME_LEN - 1 ); x++ )
    {
        pcBuffer[ x ] = ' ';
    }

    /* Terminate. */
    pcBuffer[ x ] = ( char ) 0x00;

    /* Return the new end of string. */
    return &( pcBuffer[ x ] );
}

void util_print_task_stats(char *pcWriteBuffer)
{
#if (CONFIG_FREERTOS_USE_TRACE_FACILITY == 1)
    char *bufferOrig = pcWriteBuffer;
    TaskStatus_t * pxTaskStatusArray;
    UBaseType_t uxArraySize, x;
    char cStatus;
    configRUN_TIME_COUNTER_TYPE ulTotalTime, ulStatsAsPercentage;

    /* Make sure the write buffer does not contain a string. */
    *pcWriteBuffer = ( char ) 0x00;

    /* Take a snapshot of the number of tasks in case it changes while this
        * function is executing. */
    uxArraySize = uxTaskGetNumberOfTasks();

    /* Allocate an array index for each task.  NOTE!  if
        * configSUPPORT_DYNAMIC_ALLOCATION is set to 0 then pvPortMalloc() will
        * equate to NULL. */
    pxTaskStatusArray = psram_calloc( uxArraySize, sizeof( TaskStatus_t ) );

    if( pxTaskStatusArray != NULL )
    {
        /* Generate the (binary) data. */
        uxArraySize = uxTaskGetSystemState( pxTaskStatusArray, uxArraySize, &ulTotalTime );

        ulTotalTime /= 100UL;

        /* Create a human readable table from the binary data. */
        for( x = 0; x < uxArraySize; x++ )
        {
            /**
                #define tskRUNNING_CHAR      ( 'X' )
                #define tskBLOCKED_CHAR      ( 'B' )
                #define tskREADY_CHAR        ( 'R' )
                #define tskDELETED_CHAR      ( 'D' )
                #define tskSUSPENDED_CHAR    ( 'S' )
            */
            switch( pxTaskStatusArray[ x ].eCurrentState )
            {
                case eRunning:
                    cStatus = 'X';
                    break;

                case eReady:
                    cStatus = 'R';
                    break;

                case eBlocked:
                    cStatus = 'B';
                    break;

                case eSuspended:
                    cStatus = 'S';
                    break;

                case eDeleted:
                    cStatus = 'D';
                    break;

                case eInvalid: /* Fall through. */
                default:       /* Should not get here, but it is included
                                * to prevent static checking errors. */
                    cStatus = ( char ) 0x00;
                    break;
            }

            if (ulTotalTime > 0) {
                ulStatsAsPercentage = pxTaskStatusArray[ x ].ulRunTimeCounter / ulTotalTime;
            } else {
                ulStatsAsPercentage = 0;
            }

            /* Write the task name to the string, padding with spaces so it
                * can be printed in tabular form more easily. */
            pcWriteBuffer = WriteNameToBuffer( pcWriteBuffer, pxTaskStatusArray[ x ].pcTaskName );

            /* Write the rest of the string. */
#if ( CONFIG_FREERTOS_VTASKLIST_INCLUDE_COREID == 1 )
                int coreid = ( unsigned int ) pxTaskStatusArray[ x ].xCoreID;
                if (!(coreid == 0 || coreid == 1)) coreid = -1;
                sprintf( pcWriteBuffer, "\t%c\t%u\t%u\t%u\t%-12lu %3u%%\t%d\r\n", cStatus, ( unsigned int ) pxTaskStatusArray[ x ].uxCurrentPriority, ( unsigned int ) pxTaskStatusArray[ x ].usStackHighWaterMark, ( unsigned int ) pxTaskStatusArray[ x ].xTaskNumber, pxTaskStatusArray[ x ].ulRunTimeCounter, ulStatsAsPercentage, coreid ); /*lint !e586 sprintf() allowed as this is compiled with many compilers and this is a utility function only - not part of the core kernel implementation. */
#else /* configTASKLIST_INCLUDE_COREID == 1 */
                sprintf( pcWriteBuffer, "\t%c\t%u\t%u\t%u\t%-12lu %3u%%\r\n", cStatus, ( unsigned int ) pxTaskStatusArray[ x ].uxCurrentPriority, ( unsigned int ) pxTaskStatusArray[ x ].usStackHighWaterMark, ( unsigned int ) pxTaskStatusArray[ x ].xTaskNumber, pxTaskStatusArray[ x ].ulRunTimeCounter, ulStatsAsPercentage ); /*lint !e586 sprintf() allowed as this is compiled with many compilers and this is a utility function only - not part of the core kernel implementation. */
#endif /* configTASKLIST_INCLUDE_COREID == 1 */
            pcWriteBuffer += strlen( pcWriteBuffer ); /*lint !e9016 Pointer arithmetic ok on char pointers especially as in this case where it best denotes the intent of the code. */
        }

        /* Free the array again.  NOTE!  If configSUPPORT_DYNAMIC_ALLOCATION
            * is 0 then vPortFree() will be #defined to nothing. */
        free( pxTaskStatusArray );

#if ( CONFIG_FREERTOS_VTASKLIST_INCLUDE_COREID == 1 )
        ESP_LOGI("task stats", "\nTask Name       Status  Prio    HWM     Task#   Abs Time      %%time     Core\n%s\n", bufferOrig);
#else
        ESP_LOGI("task stats", "\nTask Name       Status  Prio    HWM     Task#   Abs Time      %%time\n%s\n", bufferOrig);
#endif
    }
#endif
}


bool cJSON_IsGeneralBool(const cJSON * const item)
{
    return cJSON_IsBool(item) || cJSON_IsNumber(item);
}

bool cJSON_IsGeneralTrue(const cJSON * const item)
{
    if (cJSON_IsBool(item)) return cJSON_IsTrue(item);
    else if (cJSON_IsNumber(item)) return (item->valueint != 0);
    else return false;
}

char *strdup_psram(const char *s)
{
    size_t len = strlen(s) + 1;
    void *new = heap_caps_calloc(1, len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (new == NULL)
        return NULL;
    return (char *)memcpy(new, s, len);
}
