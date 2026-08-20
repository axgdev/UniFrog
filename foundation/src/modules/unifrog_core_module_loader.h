#ifndef UNIFROG_CORE_MODULE_LOADER_H
#define UNIFROG_CORE_MODULE_LOADER_H

#include <stddef.h>
#include <stdint.h>

#include <unifrog/core_module.h>

struct unifrog_core_module_loaded {
   struct unifrog_core_module_header header;
   const struct unifrog_core_module_exports *exports;
   enum unifrog_core_module_load_error error;
   uintptr_t load_addr;
   size_t image_size;
   size_t memory_size;
   uintptr_t gp_addr;
};

const char *unifrog_core_module_load_error_name(
   enum unifrog_core_module_load_error error);

int unifrog_core_module_load_file(const char *path, const char *expected_id,
   struct unifrog_core_module_loaded *loaded);
void unifrog_core_module_unload(struct unifrog_core_module_loaded *loaded);

#endif
