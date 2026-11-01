# Extend only this example's secure CM33 companion, not the global Zephyr tree.
if(NOT CONFIG_CPU_CORTEX_M33 OR NOT CONFIG_SOC_PSE84_M55_ENABLE)
  message(FATAL_ERROR "display_clock_startup requires the secure CM33 boot companion")
endif()

set(IFX_PDL ${ZEPHYR_HAL_INFINEON_MODULE_DIR}/mtb-dsl-pse8xxgp/pdl)
set(IFX_BSP ${ZEPHYR_HAL_INFINEON_MODULE_DIR}/zephyr-ifx-cycfg/pse84/kit_pse84_ai)
zephyr_library()
zephyr_library_sources(startup.c ${IFX_BSP}/cycfg_clocks.c ${IFX_PDL}/drivers/source/cy_wdt.c)
zephyr_library_compile_definitions(PSE846GPS2DBZC4A COMPONENT_CM33 COMPONENT_SECURE_DEVICE)
zephyr_library_compile_options("SHELL:-include cy_pdl.h" "SHELL:-include system_edge.h")
zephyr_link_libraries(-Wl,--wrap=ifx_pse84_cm55_startup)
