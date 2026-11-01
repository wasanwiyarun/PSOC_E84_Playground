cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(psoc_e84_radar_ai LANGUAGES C CXX)
set(RADAR_REFERENCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../Temp/Radar" CACHE PATH "Licensed reference project")
set(MTB_TFLM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../Temp/mtb_shared/ml-tflite-micro/release-v3.4.0" CACHE PATH "Infineon TFLM 3.4.0")
set(TFLM_INCLUDE ${MTB_TFLM_ROOT}/COMPONENT_ML_TFLM/include)
set(TFLM_LIB ${MTB_TFLM_ROOT}/COMPONENT_ML_TFLM/COMPONENT_U55/TOOLCHAIN_GCC_ARM/libtensorflow-microlite.a)
if(NOT EXISTS ${TFLM_LIB})
  message(FATAL_ERROR "Install ml-tflite-micro 3.4.0 through the reference project's Library Manager; see README.md")
endif()
file(SHA256 ${TFLM_LIB} TFLM_SHA256)
if(NOT TFLM_SHA256 STREQUAL "c05c9ddc87932a13f6ec6a24490e8734127a43246404d0cbd6a707a202447d5c")
  message(FATAL_ERROR "Unexpected TFLM archive: review its ABI, headers and model compatibility before changing the pin")
endif()
set(GENERATED ${CMAKE_CURRENT_BINARY_DIR}/model-generated)
execute_process(COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/prepare_model.py
  ${RADAR_REFERENCE_ROOT} ${GENERATED} COMMAND_ERROR_IS_FATAL ANY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS prepare_model.py
  ${RADAR_REFERENCE_ROOT}/proj_cm55/model/model.c ${RADAR_REFERENCE_ROOT}/proj_cm55/model/model.h)
set(IFX_PDL ${ZEPHYR_HAL_INFINEON_MODULE_DIR}/mtb-dsl-pse8xxgp/pdl)
set(RADAR_VENDOR ${CMAKE_CURRENT_SOURCE_DIR}/../021_Radar_BGT60TR13C/vendor/bgt60trxx)
target_sources(app PRIVATE src/main.c src/radar.c src/ai_runtime.cpp
  ${GENERATED}/model.c ${RADAR_VENDOR}/xensiv_bgt60trxx.c ${IFX_PDL}/drivers/source/cy_scb_spi.c)
target_include_directories(app PRIVATE src ${GENERATED} ${RADAR_VENDOR})
target_include_directories(app SYSTEM PRIVATE ${TFLM_INCLUDE} ${TFLM_INCLUDE}/flatbuffers/include
  ${TFLM_INCLUDE}/gemmlowp ${TFLM_INCLUDE}/ruy)
target_compile_definitions(app PRIVATE PSE846GPS2DBZC4A COMPONENT_55500
  COMPONENT_55500A1 COMPONENT_CM55 COMPONENT_CM55_0 COMPONENT_NON_SECURE_DEVICE
  COMPONENT_ML_TFLM COMPONENT_ML_FLOAT32 TF_LITE_STATIC_MEMORY)
# Generated FFT intentionally uses double constants; retain its arithmetic.
set_source_files_properties(${GENERATED}/model.c PROPERTIES COMPILE_OPTIONS "-O3;-Wno-double-promotion")
# Match the supplied archive's enum ABI, only inside the C++ TFLM boundary.
set_source_files_properties(src/ai_runtime.cpp PROPERTIES COMPILE_OPTIONS "-O3;-fshort-enums")
zephyr_link_libraries(${TFLM_LIB})
