cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(encrypted_files_2)
include(../common/encrypted_files/qspi_geometry.cmake)
target_sources(app PRIVATE ../common/encrypted_files/main.c)
target_compile_definitions(app PRIVATE EF_ALGORITHM=2 EF_KEY_BITS=128 EF_NAME="AES128_GCM")
set_property(TARGET zephyr_property_target APPEND PROPERTY TFM_CMAKE_OPTIONS
  -DPROJECT_CONFIG_HEADER_FILE=${CMAKE_CURRENT_SOURCE_DIR}/../common/encrypted_files/config_tfm.h)
include(../common/encrypted_files/tfm_config.cmake)
