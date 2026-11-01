cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(psoc_e84_secure_crypto_services)
target_sources(app PRIVATE src/main.c)

# Enable AES-GCM in the TF-M secure Crypto partition. The board's default
# profile intentionally omits GCM to minimize the secure-image footprint.
set_property(TARGET zephyr_property_target APPEND PROPERTY TFM_CMAKE_OPTIONS
  -DPROJECT_CONFIG_HEADER_FILE=${CMAKE_CURRENT_SOURCE_DIR}/src/configs/config_tfm.h)
zephyr_include_directories(${APPLICATION_SOURCE_DIR}/src/configs)
