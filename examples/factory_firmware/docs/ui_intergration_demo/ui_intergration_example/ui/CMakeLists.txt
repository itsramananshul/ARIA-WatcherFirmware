SET(SOURCES screens/ui_Page_example.c
    ui.c
    components/ui_comp_hook.c
    ui_helpers.c)

add_library(ui ${SOURCES})
