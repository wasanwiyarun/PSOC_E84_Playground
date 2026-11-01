/* SPDX-License-Identifier: Apache-2.0 */
#include <cy_pdl.h>

void init_cycfg_clocks(void);
void __real_ifx_pse84_cm55_startup(void);

/* Clock configuration must run on the secure core before CM55 starts.
 * Use the board's generated ECO/PLL configuration. The generic companion
 * otherwise leaves the internal-oscillator boot clocks in place, which
 * reproduced persistent DSI DPI payload underflow on the attached board.
 */
void __wrap_ifx_pse84_cm55_startup(void)
{
	init_cycfg_clocks();
	__real_ifx_pse84_cm55_startup();
}
