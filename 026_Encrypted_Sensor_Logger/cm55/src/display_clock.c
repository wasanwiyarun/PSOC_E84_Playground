/* TF-M keeps SRSS_MAIN secure. Supply setup belongs to the secure BSP. */
#include <cy_pdl.h>

/* The secure BSP has already configured and checked the MIPI supply. The
 * library's direct non-secure MISCLDO write is forbidden by TF-M. Only accept
 * the identical, immutable configuration used by this paired secure image. */
cy_en_syspm_status_t __wrap_Cy_SysPm_MiscLdoConfigure(cy_stc_syspm_miscldo_params_t *p)
{
    if(!p || !p->miscLdoEnable || p->miscLdoMode!=CY_SYSPM_MISCLDO_VCCACT ||
       p->miscLdoVoltSel!=CY_SYSPM_MISCLDO_VOLTAGE_0_90V ||
       p->miscLdoVaccActTrimSel!=CY_SYSPM_MISCLDO_VCCACT_TRIM_0)
        return CY_SYSPM_BAD_PARAM;
    return CY_SYSPM_SUCCESS;
}
