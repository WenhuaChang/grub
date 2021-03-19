#ifdef __linux__
#include "linux/efi_removable_fallback.c"
#else
#include "basic/efi_removable_fallback.c"
#endif
