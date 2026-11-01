#include <cybsp.h>
#include <cy_pdl.h>
cy_rslt_t __real_cybsp_init(void);
cy_rslt_t __wrap_cybsp_init(void)
{
    cy_rslt_t ret=__real_cybsp_init();
    if(ret!=CY_RSLT_SUCCESS) return ret;
    /* Same fixed supply configuration as cy_mipi_dsi_dphy_init(). */
    cy_stc_syspm_miscldo_params_t p={true,CY_SYSPM_MISCLDO_VCCACT,
        CY_SYSPM_MISCLDO_VOLTAGE_0_90V,CY_SYSPM_MISCLDO_VCCACT_TRIM_0};
    (void)Cy_SysPm_MiscLdoConfigure(&p);
    Cy_SysLib_Delay(1);
    return Cy_SysPm_MiscLdoStatus()==CY_SYSPM_SUCCESS?CY_RSLT_SUCCESS:1;
}
