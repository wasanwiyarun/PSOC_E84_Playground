/* CM33 non-secure client for TF-M PSA Crypto services. */
#include <string.h>

#include <psa/crypto.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

static bool aes_gcm_test(void)
{
	static const uint8_t key_bytes[16] = {
		0x60, 0x3d, 0xeb, 0x10, 0x15, 0xca, 0x71, 0xbe,
		0x2b, 0x73, 0xae, 0xf0, 0x85, 0x7d, 0x77, 0x81,
	};
	static const uint8_t nonce[12] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
	static const uint8_t aad[] = "pse84-aad";
	static const uint8_t plain[] = "secure-aes-gcm";
	uint8_t cipher[sizeof(plain) + PSA_AEAD_TAG_MAX_SIZE];
	uint8_t recovered[sizeof(plain)];
	size_t cipher_len, plain_len;
	psa_key_attributes_t attrs = PSA_KEY_ATTRIBUTES_INIT;
	psa_key_id_t key = PSA_KEY_ID_NULL;
	psa_status_t status;

	psa_set_key_type(&attrs, PSA_KEY_TYPE_AES);
	psa_set_key_bits(&attrs, 128);
	psa_set_key_usage_flags(&attrs, PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT);
	psa_set_key_algorithm(&attrs, PSA_ALG_GCM);
	status = psa_import_key(&attrs, key_bytes, sizeof(key_bytes), &key);
	psa_reset_key_attributes(&attrs);
	if (status != PSA_SUCCESS) {
		printk("FAIL AES IMPORT=%d\n", status);
		return false;
	}
	status = psa_aead_encrypt(key, PSA_ALG_GCM, nonce, sizeof(nonce), aad,
				 sizeof(aad), plain, sizeof(plain), cipher, sizeof(cipher), &cipher_len);
	if (status == PSA_SUCCESS) {
		status = psa_aead_decrypt(key, PSA_ALG_GCM, nonce, sizeof(nonce), aad,
					  sizeof(aad), cipher, cipher_len, recovered,
					  sizeof(recovered), &plain_len);
	}
	if (status == PSA_SUCCESS && (plain_len != sizeof(plain) ||
				  memcmp(plain, recovered, sizeof(plain)) != 0)) {
		status = PSA_ERROR_CORRUPTION_DETECTED;
	}
	if (status == PSA_SUCCESS) {
		cipher[cipher_len - 1] ^= 1;
		status = psa_aead_decrypt(key, PSA_ALG_GCM, nonce, sizeof(nonce), aad,
					  sizeof(aad), cipher, cipher_len, recovered,
					  sizeof(recovered), &plain_len);
		if (status == PSA_ERROR_INVALID_SIGNATURE) {
			printk("OK AES_GCM encrypt_decrypt=1 tamper_rejected=1\n");
			status = PSA_SUCCESS;
		} else {
			printk("FAIL AES TAMPER_STATUS=%d\n", status);
		}
	}
	(void)psa_destroy_key(key);
	if (status != PSA_SUCCESS) {
		printk("FAIL AES STATUS=%d\n", status);
	}
	return status == PSA_SUCCESS;
}

static psa_status_t make_p256_key(psa_key_id_t *key, psa_key_usage_t usage,
				  psa_algorithm_t algorithm)
{
	psa_key_attributes_t attrs = PSA_KEY_ATTRIBUTES_INIT;
	psa_set_key_type(&attrs, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
	psa_set_key_bits(&attrs, 256);
	psa_set_key_usage_flags(&attrs, usage);
	psa_set_key_algorithm(&attrs, algorithm);
	psa_status_t status = psa_generate_key(&attrs, key);
	psa_reset_key_attributes(&attrs);
	return status;
}

static bool ecdh_test(void)
{
	psa_key_id_t alice = PSA_KEY_ID_NULL, bob = PSA_KEY_ID_NULL;
	uint8_t alice_pub[65], bob_pub[65], alice_secret[32], bob_secret[32];
	size_t alice_pub_len, bob_pub_len, alice_len, bob_len;
	psa_status_t status;

	status = make_p256_key(&alice, PSA_KEY_USAGE_DERIVE, PSA_ALG_ECDH);
	if (status == PSA_SUCCESS) status = make_p256_key(&bob, PSA_KEY_USAGE_DERIVE, PSA_ALG_ECDH);
	if (status == PSA_SUCCESS) status = psa_export_public_key(alice, alice_pub, sizeof(alice_pub), &alice_pub_len);
	if (status == PSA_SUCCESS) status = psa_export_public_key(bob, bob_pub, sizeof(bob_pub), &bob_pub_len);
	if (status == PSA_SUCCESS) status = psa_raw_key_agreement(PSA_ALG_ECDH, alice, bob_pub, bob_pub_len,
							   alice_secret, sizeof(alice_secret), &alice_len);
	if (status == PSA_SUCCESS) status = psa_raw_key_agreement(PSA_ALG_ECDH, bob, alice_pub, alice_pub_len,
							   bob_secret, sizeof(bob_secret), &bob_len);
	if (status == PSA_SUCCESS && (alice_len != bob_len || memcmp(alice_secret, bob_secret, alice_len))) {
		status = PSA_ERROR_CORRUPTION_DETECTED;
	}
	(void)psa_destroy_key(alice);
	(void)psa_destroy_key(bob);
	if (status == PSA_SUCCESS) {
		printk("OK ECDH curve=P-256 shared_secret_match=1\n");
	} else {
		printk("FAIL ECDH STATUS=%d\n", status);
	}
	return status == PSA_SUCCESS;
}

static bool ecdsa_test(void)
{
	static const uint8_t message[] = "PSoC E84 secure signature";
	uint8_t hash[PSA_HASH_LENGTH(PSA_ALG_SHA_256)], signature[80];
	size_t hash_len, signature_len;
	psa_key_id_t key = PSA_KEY_ID_NULL;
	psa_status_t status;

	status = make_p256_key(&key, PSA_KEY_USAGE_SIGN_HASH | PSA_KEY_USAGE_VERIFY_HASH,
			       PSA_ALG_ECDSA(PSA_ALG_SHA_256));
	if (status == PSA_SUCCESS) status = psa_hash_compute(PSA_ALG_SHA_256, message, sizeof(message),
									hash, sizeof(hash), &hash_len);
	if (status == PSA_SUCCESS) status = psa_sign_hash(key, PSA_ALG_ECDSA(PSA_ALG_SHA_256), hash, hash_len,
								signature, sizeof(signature), &signature_len);
	if (status == PSA_SUCCESS) status = psa_verify_hash(key, PSA_ALG_ECDSA(PSA_ALG_SHA_256), hash, hash_len,
								  signature, signature_len);
	if (status == PSA_SUCCESS) {
		hash[0] ^= 1;
		status = psa_verify_hash(key, PSA_ALG_ECDSA(PSA_ALG_SHA_256), hash, hash_len,
								  signature, signature_len);
		if (status == PSA_ERROR_INVALID_SIGNATURE) {
			printk("OK ECDSA curve=P-256 sign_verify=1 tamper_rejected=1\n");
			status = PSA_SUCCESS;
		} else {
			printk("FAIL ECDSA TAMPER_STATUS=%d\n", status);
		}
	}
	(void)psa_destroy_key(key);
	if (status != PSA_SUCCESS) printk("FAIL ECDSA STATUS=%d\n", status);
	return status == PSA_SUCCESS;
}

static const struct device *const console = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

static void handle_command(char *line)
{
	if (strcmp(line, "crypto info") == 0) {
		printk("OK CRYPTO COMMANDS=aes,ecdh,ecdsa,all,info\n");
	} else if (strcmp(line, "crypto aes") == 0) {
		if (aes_gcm_test()) printk("PASS CRYPTO AES_GCM\n");
	} else if (strcmp(line, "crypto ecdh") == 0) {
		if (ecdh_test()) printk("PASS CRYPTO ECDH\n");
	} else if (strcmp(line, "crypto ecdsa") == 0) {
		if (ecdsa_test()) printk("PASS CRYPTO ECDSA\n");
	} else if (strcmp(line, "crypto all") == 0) {
		bool aes = aes_gcm_test();
		bool ecdh = ecdh_test();
		bool ecdsa = ecdsa_test();
		if (aes && ecdh && ecdsa) printk("PASS SECURE_CRYPTO AES_GCM ECDH ECDSA\n");
		else printk("FAIL SECURE_CRYPTO\n");
	} else {
		printk("ERR command: crypto aes|ecdh|ecdsa|all|info\n");
	}
}

int main(void)
{
	char line[48];
	size_t length = 0;
	unsigned char ch;
	psa_status_t status = psa_crypto_init();

	if (!device_is_ready(console)) return 0;
	printk("OK CORE=CM33-NS SERVICE=TFM_PSA_CRYPTO INIT=%d\n", status);
	if (status != PSA_SUCCESS) return 0;
	printk("OK READY=1\n");
	while (true) {
		if (uart_poll_in(console, &ch) != 0) {
			k_msleep(5);
			continue;
		}
		if (ch == '\r' || ch == '\n') {
			if (length) {
				line[length] = '\0';
				handle_command(line);
				length = 0;
			}
		} else if (length + 1 < sizeof(line)) {
			line[length++] = (char)ch;
		}
	}
}
