#include "../../common/encrypted_files/config_tfm.h"
/* Existing secure CM33 code-SRAM allocation, accessed through its system alias.
 * The scoped TF-M linker/copy tables relocate .logger_ram at secure boot. */
#define S_RAM_CODE_START 0x34030000
#define S_RAM_CODE_SIZE  0x00008000
