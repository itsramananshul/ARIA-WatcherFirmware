cmake_minimum_required(VERSION 3.5)

# set(EXTRA_COMPONENT_DIRS ../../components)
# set(COMPONENTS main rlottie indoor_ai_camera)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

add_compile_options(-Wno-unused-value -fdiagnostics-color=always)

project(rlottie_example)