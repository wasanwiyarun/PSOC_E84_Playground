cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(logger_secure_relay)
target_sources(app PRIVATE ../../012_CM55_CM33_IPC_Mailbox/cm33_ns/src/main.c)
set_property(TARGET zephyr_property_target APPEND PROPERTY TFM_CMAKE_OPTIONS
  -DCMAKE_PROJECT_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/tfm_display.cmake
  -DPROJECT_CONFIG_HEADER_FILE=${CMAKE_CURRENT_SOURCE_DIR}/config_tfm.h)
include(../../common/encrypted_files/tfm_config.cmake)
