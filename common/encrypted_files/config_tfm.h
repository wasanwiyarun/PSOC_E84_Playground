/* All examples deliberately share one secure service configuration, so
 * switching firmware does not change the persistent-storage layout. */
#define PSA_WANT_ALG_GCM 1
#define PSA_WANT_KEY_TYPE_AES 1
#define PSA_WANT_ALG_CHACHA20_POLY1305 1
#define PSA_WANT_KEY_TYPE_CHACHA20 1
#define CRYPTO_ASYM_ENCRYPT_MODULE_ENABLED 0

