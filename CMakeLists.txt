# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

set(EXTRA_COMPONENT_DIRS  components)

add_compile_options(-fdiagnostics-color=always -w)

project(trimet_station_display)

# LVGL objects, styles and buffers in PSRAM instead of internal RAM (plain
# malloc puts anything under CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL there).
idf_component_get_property(lvgl_lib lvgl COMPONENT_LIB)
target_compile_definitions(${lvgl_lib} PRIVATE
    "LV_MEM_CUSTOM_ALLOC(size)=heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)"
    "LV_MEM_CUSTOM_REALLOC(p, size)=heap_caps_realloc(p, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)"
    "LV_MEM_CUSTOM_FREE(p)=heap_caps_free(p)")
