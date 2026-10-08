/* Additional algorithms enabled inside the TF-M secure Crypto partition. */
#define PSA_WANT_ALG_GCM 1
#define PSA_WANT_KEY_TYPE_AES 1
/* This board has no RSA encryption accelerator; keep that unused module off. */
#define CRYPTO_ASYM_ENCRYPT_MODULE_ENABLED 0
