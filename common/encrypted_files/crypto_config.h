/* Appended after TF-M's medium profile, which otherwise restricts AES to 128. */
#undef MBEDTLS_AES_ONLY_128_BIT_KEY_LENGTH
/* Retain the board's additional cipher/CMAC module configuration. */
#undef CRYPTO_CIPHER_MODULE_ENABLED
#define CRYPTO_CIPHER_MODULE_ENABLED 1
#undef PSA_WANT_ALG_CMAC
#define PSA_WANT_ALG_CMAC 1
