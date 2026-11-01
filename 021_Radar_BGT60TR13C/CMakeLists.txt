cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(psoc_e84_radar)
set(IFX_PDL ${ZEPHYR_HAL_INFINEON_MODULE_DIR}/mtb-dsl-pse8xxgp/pdl)
target_sources(app PRIVATE src/main.c src/radar.c vendor/bgt60trxx/xensiv_bgt60trxx.c
  ${IFX_PDL}/drivers/source/cy_scb_spi.c)
target_include_directories(app PRIVATE src vendor/bgt60trxx)
target_compile_definitions(app PRIVATE PSE846GPS2DBZC4A COMPONENT_55500
  COMPONENT_55500A1 COMPONENT_CM55 COMPONENT_CM55_0 COMPONENT_NON_SECURE_DEVICE)
