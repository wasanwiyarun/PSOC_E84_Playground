cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(psoc_e84_kalman_horizon)

# Reuse the proven display pipeline from Chapter 017 and FT5406 driver from
# Chapter 018.  This chapter owns only the animated LVGL application.
set(DISPLAY_CHAPTER ${CMAKE_CURRENT_SOURCE_DIR}/../017_LVGL_Waveshare_4_3_Display)
set(TOUCH_CHAPTER ${CMAKE_CURRENT_SOURCE_DIR}/../018_LVGL_Waveshare_Touch_Input)
set(IFX_PDL ${ZEPHYR_HAL_INFINEON_MODULE_DIR}/mtb-dsl-pse8xxgp/pdl)
set(IFX_GFX ${IFX_PDL}/drivers/third_party/COMPONENT_GFXSS)

target_sources(app PRIVATE
  src/main.c
  src/attitude.c
  src/flight_math.c
  src/horizon_draw.c
  src/magnetometer.c
  src/sensor_bus_recovery.c
  vendor/bmm350/bmm350.c
  ${IFX_PDL}/drivers/source/cy_i3c.c
  ${TOUCH_CHAPTER}/src/ft5406_touch.c
  ${DISPLAY_CHAPTER}/src/waveshare_4p3_display.c
  ${IFX_PDL}/drivers/source/cy_mipidsi.c
  ${IFX_GFX}/cy_graphics.c
  ${IFX_GFX}/vsi/dcnano8000/DCUser/rtos/viv_dc_interface.c
  ${IFX_GFX}/vsi/dcnano8000/DCUser/rtos/viv_dc_os.c
  ${IFX_GFX}/vsi/dcnano8000/DCUser/viv_dc_setting.c
  ${IFX_GFX}/vsi/dcnano8000/DCUser/viv_dc_tools.c
  ${IFX_GFX}/vsi/dcnano8000/DCUser/viv_dc_util.c
  ${IFX_GFX}/vsi/dcnano8000/DCKernel/rtos/viv_dc_platform.c
  ${IFX_GFX}/vsi/dcnano8000/DCKernel/hardware/8000Nano/viv_dc_hardware.c
  ${IFX_GFX}/vsi/dcnano8000/DCKernel/viv_dc_core.c
  ${IFX_GFX}/vsi/dcnano8000/DCKernel/viv_dc_database.c
  ${IFX_GFX}/vsi/dcnano8000/DCKernel/dc_layer_feature.c
  ${IFX_GFX}/vsi/dcnano8000/DCKernel/viv_dc_lcd.c
  ${IFX_GFX}/vsi/dcnano8000/DCKernel/viv_dc_shell.c)

zephyr_include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}/vendor/bmm350
  ${TOUCH_CHAPTER}/src
  ${DISPLAY_CHAPTER}/src
  ${IFX_PDL}/devices/include
  ${IFX_PDL}/devices/include/ip
  ${IFX_PDL}/drivers/include
  ${IFX_GFX}
  ${IFX_GFX}/vsi/dcnano8000/include
  ${IFX_GFX}/vsi/dcnano8000/DCKernel
  ${IFX_GFX}/vsi/dcnano8000/DCKernel/hardware/8000Nano
  ${IFX_GFX}/vsi/dcnano8000/DCKernel/rtos
  ${IFX_GFX}/vsi/dcnano8000/DCUser
  ${IFX_GFX}/vsi/dcnano8000/DCUser/rtos)

target_compile_definitions(app PRIVATE
  PSE846GPS2DBZC4A COMPONENT_55500 COMPONENT_55500A1 COMPONENT_CM55
  COMPONENT_CM55_0 COMPONENT_GFXSS COMPONENT_NON_SECURE_DEVICE _BAREMETAL=0)
target_compile_options(app PRIVATE -include cy_pdl.h)
