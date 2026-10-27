# The SoC appends a board crypto override during late configure. Apply our
# superset afterwards, otherwise the board's AES-128-only profile wins.
function(efs_crypto_override)
  get_target_property(options zephyr_property_target TFM_CMAKE_OPTIONS)
  list(FILTER options EXCLUDE REGEX "^-DTFM_TF_PSA_CRYPTO_PLATFORM_EXTRA_CONFIG_PATH=")
  list(APPEND options "-DTFM_TF_PSA_CRYPTO_PLATFORM_EXTRA_CONFIG_PATH=${CMAKE_CURRENT_FUNCTION_LIST_DIR}/crypto_config.h")
  set_property(TARGET zephyr_property_target PROPERTY TFM_CMAKE_OPTIONS "${options}")
endfunction()
cmake_language(DEFER CALL efs_crypto_override)
