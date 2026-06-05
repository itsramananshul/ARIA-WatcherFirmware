#ifndef APP_TIME_H
#define APP_TIME_H

#ifdef __cplusplus
extern "C" {
#endif

#include <time.h>
#include "data_defs.h"
//ntp sync
int app_time_init(void);

// set TZ
int app_time_net_zone_set( char *p);

void app_time_cfg_get(struct view_data_time_cfg *cfg);

#ifdef __cplusplus
}
#endif

#endif
