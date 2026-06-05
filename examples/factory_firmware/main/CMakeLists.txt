set(APP_DIR ./app)
file(GLOB_RECURSE APP_SRCS ${APP_DIR}/*.c)

set(SENSOR_DIR ./sensor)
file(GLOB_RECURSE SENSOR_SRCS ${SENSOR_DIR}/*.c)

set(VIEW_DIR ./view)
file(GLOB_RECURSE VIEW_SRCS ${VIEW_DIR}/*.c)

set(UTIL_DIR ./util)
file(GLOB_RECURSE UTIL_SRCS ${UTIL_DIR}/*.c)

set(TASK_FLOW_ENGINE_DIR ./task_flow_engine)
file(GLOB_RECURSE TASK_FLOW_ENGINE_SRCS ${TASK_FLOW_ENGINE_DIR}/src/*.c)

set(TASK_FLOW_MODULE_DIR ./task_flow_module)
file(GLOB_RECURSE TASK_FLOW_MODULE_SRCS ${TASK_FLOW_MODULE_DIR}/*.c)
file(GLOB_RECURSE TASK_FLOW_MODULE_COMMON_SRCS ${TASK_FLOW_MODULE_DIR}/common/*.c)


idf_component_register(
    SRCS
        "main.c"
        ${APP_SRCS}
        ${SENSOR_SRCS}
        ${VIEW_SRCS}
        ${UTIL_SRCS}
        ${TASK_FLOW_ENGINE_SRCS}
        ${TASK_FLOW_MODULE_SRCS}
        ${TASK_FLOW_MODULE_COMMON_SRCS}
    INCLUDE_DIRS
        "."
        ${APP_DIR}
        ${SENSOR_DIR}
        ${VIEW_DIR}
        ${UTIL_DIR}
        ${TASK_FLOW_ENGINE_DIR}/include
        ${TASK_FLOW_MODULE_DIR}
        ${TASK_FLOW_MODULE_DIR}/common
)

if (CONFIG_ENABLE_FACTORY_FW_DEBUG_LOG)
    # to override the CONFIG_LOG_MAXIMUM_LEVEL for 'main'
    # still need to call esp_log_level_set() for specific module.
    # The ESP-IDF doc is obscure for this.
    target_compile_definitions(${COMPONENT_LIB} PUBLIC "-DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG")
endif()

spiffs_create_partition_image(storage ../spiffs FLASH_IN_PROJECT)
