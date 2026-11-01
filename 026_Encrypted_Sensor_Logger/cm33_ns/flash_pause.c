/* Both CPUs execute from the flash being modified. The platform service parks
 * CM33 in secure SRAM with interrupts masked until CM55 finishes the operation.
 * No key material, arbitrary register access, or secure addresses are exposed.
 */
#include <stdint.h>
#include <string.h>
#include "cmsis.h"
#include "tfm_platform_api.h"
#include "ifx_platform_spe_config.h"

#if !IFX_PSA_ROT_PRIVILEGED
#error "Logger flash pause requires a privileged platform service to mask interrupts"
#endif

__attribute__((section(".logger_ram"), noinline))
static void park_in_ram(volatile uint32_t *request, volatile uint32_t *ack)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    __DSB();
    *ack = 1;
    __DSB();
    while (*request == 1) {
        __NOP();
    }
    __DSB();
    *ack = 0;
    __DSB();
    __set_PRIMASK(mask);
}

enum tfm_platform_err_t ifx_platform_hal_ioctl(tfm_platform_ioctl_req_t request,
                                               psa_invec *in, psa_outvec *out)
{
    (void)out;
    uint32_t address;
    if (request != 0x40000000 || !in || !in[0].base || in[0].len != sizeof(address))
        return TFM_PLATFORM_ERR_INVALID_PARAM;
    memcpy(&address, in[0].base, sizeof(address));
    /* Fixed application-owned non-secure SRAM, never secure RAM or MMIO.
     * Cache-line-separated words are maintained explicitly by CM55. */
    if (address < 0x26100000 || address > 0x2613ff80 || (address & 63))
        return TFM_PLATFORM_ERR_INVALID_PARAM;
    volatile uint32_t *req = (volatile uint32_t *)address;
    if (*req != 1)
        return TFM_PLATFORM_ERR_INVALID_PARAM;
    park_in_ram(req, req + 16);
    return TFM_PLATFORM_ERR_SUCCESS;
}
