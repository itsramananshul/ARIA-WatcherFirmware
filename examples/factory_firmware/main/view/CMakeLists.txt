SET(SOURCES 
    view_alarm.c
    view_image_preview.c
    view_pages.c
    )

add_subdirectory(ui)

add_subdirectory(ui_manager)

add_library(view ${SOURCES})
