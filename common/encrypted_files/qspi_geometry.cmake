# Zephyr v4.5.0-rc1-231's Infineon QSPI driver chooses the FIRST global
# mapped partition. With CM33 NS that is internal RRAM, so its reported QSPI
# capacity/page geometry is wrong. Scope this compatibility fix to these apps;
# never mutate the user's Zephyr checkout. Remove when upstream is corrected.
set(qspi_source "${ZEPHYR_BASE}/drivers/flash/flash_infineon_serial_memory_qspi.c")
file(READ "${qspi_source}" qspi_text)
set(old_selection "#if defined(CONFIG_DT_HAS_FIXED_PARTITIONS_ENABLED)\n#define SOC_NV_FLASH_NODE DT_PARENT(DT_INST(0, fixed_partitions))\n#else\n#define SOC_NV_FLASH_NODE DT_MEM_FROM_MAPPED_PARTITION(DT_INST(0, zephyr_mapped_partition))\n#endif")
string(FIND "${qspi_text}" "${old_selection}" selection_index)
if(selection_index LESS 0)
  message(FATAL_ERROR "QSPI driver changed; review/remove encrypted-files geometry compatibility fix")
endif()
string(REPLACE "${old_selection}" "#define SOC_NV_FLASH_NODE DT_CHOSEN(zephyr_flash)" qspi_fixed "${qspi_text}")
set(qspi_generated "${CMAKE_CURRENT_BINARY_DIR}/qspi_geometry_fixed.c")
file(WRITE "${qspi_generated}" "${qspi_fixed}")
get_target_property(flash_sources drivers__flash SOURCES)
list(FILTER flash_sources EXCLUDE REGEX "flash_infineon_serial_memory_qspi\\.c$")
set_property(TARGET drivers__flash PROPERTY SOURCES "${flash_sources}")
target_sources(drivers__flash PRIVATE "${qspi_generated}")
