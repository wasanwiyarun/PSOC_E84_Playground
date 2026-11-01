/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

int main(void)
{
	/* Let a host serial monitor attach after reset before printing the result. */
	k_sleep(K_SECONDS(2));

	printk("Hello from the PSOC Edge84 Zephyr playground!\n");

	return 0;
}
