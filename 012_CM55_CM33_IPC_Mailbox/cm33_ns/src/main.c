#include <stdint.h>

#include <psa/client.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

int main(void)
{
	uint32_t version = psa_framework_version();

	printk("OK CORE=CM33-NS ROLE=RELAY_HOST PSA_VERSION=0x%08x\n", version);
	printk("INFO CM33-NS boots CM55; CM55 PSA calls are relayed here by TF-M\n");

	for (;;) {
		k_sleep(K_SECONDS(1));
	}
}
