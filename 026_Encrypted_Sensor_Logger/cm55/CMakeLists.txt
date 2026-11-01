cmake_minimum_required(VERSION 3.20.0)
list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../018_LVGL_Waveshare_Touch_Input)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(encrypted_sensor_logger)
include(../../common/encrypted_files/qspi_geometry.cmake)
set(DISPLAY_CHAPTER ${CMAKE_CURRENT_SOURCE_DIR}/../../017_LVGL_Waveshare_4_3_Display)
set(TOUCH_CHAPTER ${CMAKE_CURRENT_SOURCE_DIR}/../../018_LVGL_Waveshare_Touch_Input)
set(IFX_PDL ${ZEPHYR_HAL_INFINEON_MODULE_DIR}/mtb-dsl-pse8xxgp/pdl)
set(IFX_GFX ${IFX_PDL}/drivers/third_party/COMPONENT_GFXSS)
set(DC ${IFX_GFX}/vsi/dcnano8000)
target_sources(app PRIVATE
  src/logger.c src/store.c src/flash_guard.c
  src/display_clock.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../020_Kalman_Artificial_Horizon/src/sensor_bus_recovery.c
  ${TOUCH_CHAPTER}/src/ft5406_touch.c
  ${DISPLAY_CHAPTER}/src/waveshare_4p3_display.c
  ${IFX_PDL}/drivers/source/cy_mipidsi.c
  ${IFX_GFX}/cy_graphics.c
  ${DC}/DCUser/rtos/viv_dc_interface.c ${DC}/DCUser/rtos/viv_dc_os.c
  ${DC}/DCUser/viv_dc_setting.c ${DC}/DCUser/viv_dc_tools.c ${DC}/DCUser/viv_dc_util.c
  ${DC}/DCKernel/rtos/viv_dc_platform.c ${DC}/DCKernel/hardware/8000Nano/viv_dc_hardware.c
  ${DC}/DCKernel/viv_dc_core.c ${DC}/DCKernel/viv_dc_database.c
  ${DC}/DCKernel/dc_layer_feature.c ${DC}/DCKernel/viv_dc_lcd.c ${DC}/DCKernel/viv_dc_shell.c
  ${ZEPHYR_TRUSTED_FIRMWARE_M_MODULE_DIR}/interface/src/tfm_crypto_api.c
  ${ZEPHYR_TRUSTED_FIRMWARE_M_MODULE_DIR}/interface/src/tfm_ps_api.c
  ${ZEPHYR_TRUSTED_FIRMWARE_M_MODULE_DIR}/interface/src/tfm_platform_api.c)
zephyr_include_directories(
  ${TOUCH_CHAPTER}/src ${DISPLAY_CHAPTER}/src
  ${IFX_PDL}/devices/include ${IFX_PDL}/devices/include/ip ${IFX_PDL}/drivers/include
  ${IFX_GFX} ${DC}/include ${DC}/DCKernel ${DC}/DCKernel/hardware/8000Nano
  ${DC}/DCKernel/rtos ${DC}/DCUser ${DC}/DCUser/rtos
  ${PSE84_CM33_BUILD_DIR}/tfm/api_ns/interface/include)
target_compile_definitions(app PRIVATE
  TFM_PSA_CRYPTO_CLIENT_ONLY
  TF_PSA_CRYPTO_CONFIG_FILE="${PSE84_CM33_BUILD_DIR}/tfm/api_ns/interface/include/mbedtls/tf_psa_crypto_config.h"
  PSE846GPS2DBZC4A COMPONENT_55500 COMPONENT_55500A1 COMPONENT_CM55
  COMPONENT_CM55_0 COMPONENT_GFXSS COMPONENT_NON_SECURE_DEVICE _BAREMETAL=0)
target_compile_options(app PRIVATE -include cy_pdl.h)
zephyr_link_libraries(-Wl,--wrap=Cy_SysPm_MiscLdoConfigure)
zephyr_link_libraries(-Wl,--wrap=mtb_serial_memory_write -Wl,--wrap=mtb_serial_memory_erase)
