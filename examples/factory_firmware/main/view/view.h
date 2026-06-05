#ifndef VIEW_H
#define VIEW_H

#include "data_defs.h"
#include "lvgl.h"
#include "ui/ui.h"

#ifdef __cplusplus
extern "C" {
#endif

int view_init(void);

/**
 * @brief Render the screen in black.
 * 
 * Lock the LVGL port, perform the operation to render the screen in black, and then unlock the LVGL port.
 */
void view_render_black(void);

#ifdef __cplusplus
}
#endif

#endif
