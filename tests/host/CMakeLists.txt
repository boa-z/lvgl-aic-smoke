# SPDX-License-Identifier: Apache-2.0
# Host contracts for the smoke app's CAN OTA debug endpoint (ota/).
#   cmake -S tests/host -B <build> -G Ninja && cmake --build <build> && ctest --test-dir <build>
cmake_minimum_required(VERSION 3.20)
project(lvgl_aic_smoke_host LANGUAGES C)

set(SMOKE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
set(AIC_ROOT "${SMOKE_ROOT}/third_party/lvgl-aic" CACHE PATH "lvgl-aic component root")
set(LVGL_ROOT "${SMOKE_ROOT}/third_party/lvgl" CACHE PATH "LVGL v9.6.0 checkout")
set(CMAKE_C_STANDARD 11)
enable_testing()

# Update state machine and package guard: plain C, no LVGL.
add_executable(smoke_update_contract update_contract.c
    "${SMOKE_ROOT}/ota/update/meter_update.c" "${SMOKE_ROOT}/ota/update/meter_package.c")
target_include_directories(smoke_update_contract PRIVATE "${SMOKE_ROOT}/ota")
target_compile_options(smoke_update_contract PRIVATE -Wall -Wextra -Werror -UNDEBUG)
add_test(NAME smoke_update_contract COMMAND smoke_update_contract)
set_tests_properties(smoke_update_contract PROPERTIES TIMEOUT 60)

# Package inspector the host packer runs (tools/ota submodule: METER_OTA_INSPECTOR).
add_executable(meter-ota-inspect "${CMAKE_CURRENT_SOURCE_DIR}/package_inspector.c"
    "${SMOKE_ROOT}/ota/update/meter_package.c")
target_include_directories(meter-ota-inspect PRIVATE "${SMOKE_ROOT}/ota")

# Progress overlay: LVGL with the component's lv_conf.h, as on target.
set(LV_BUILD_CONF_PATH "${AIC_ROOT}/lv_conf.h" CACHE FILEPATH "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LV_BUILD_SET_CONFIG_OPTS OFF CACHE BOOL "" FORCE)
set(LV_FETCH_DEPENDENCIES OFF CACHE BOOL "" FORCE)
set(LV_USE_FIND_PACKAGE OFF CACHE BOOL "" FORCE)
set(LV_USE_PKG_CONFIG OFF CACHE BOOL "" FORCE)
add_subdirectory("${LVGL_ROOT}" lvgl EXCLUDE_FROM_ALL)
# The overlay labels use the built-in 14 px font (the target SConscript adds
# the same define with AIC_LVGL_SMOKE_CAN_OTA).
target_compile_definitions(lvgl PRIVATE LV_FONT_MONTSERRAT_14=1)

add_executable(smoke_ota_widget_contract ota_widget_contract.c
    "${SMOKE_ROOT}/ota/update/lv_aic_can_ota_widget.c"
    "${SMOKE_ROOT}/ota/update/meter_update.c")
target_compile_definitions(smoke_ota_widget_contract PRIVATE AIC_LVGL_SMOKE_CAN_OTA=1)
target_include_directories(smoke_ota_widget_contract PRIVATE "${SMOKE_ROOT}/ota"
    "${AIC_ROOT}/include" "${AIC_ROOT}/compat")
target_link_libraries(smoke_ota_widget_contract PRIVATE lvgl)
target_compile_options(smoke_ota_widget_contract PRIVATE -Wall -Wextra -Werror -UNDEBUG)
add_test(NAME smoke_ota_widget_contract COMMAND smoke_ota_widget_contract)
set_tests_properties(smoke_ota_widget_contract PROPERTIES TIMEOUT 60)
