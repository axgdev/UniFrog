#ifndef UNIFROG_CORE_REGISTRY_H
#define UNIFROG_CORE_REGISTRY_H

#include <stddef.h>

#include <unifrog/core_module.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UNIFROG_CORE_REGISTRY_MAX 64u
#define UNIFROG_CORE_REGISTRY_PATH_MAX 256u

enum unifrog_core_registry_format {
   UNIFROG_CORE_REGISTRY_MODULE = 0,
   UNIFROG_CORE_REGISTRY_NATIVE,
};

struct unifrog_core_registry_entry {
   struct unifrog_core_module_header header;
   char path[UNIFROG_CORE_REGISTRY_PATH_MAX];
   enum unifrog_core_registry_format format;
};

struct unifrog_core_registry {
   struct unifrog_core_registry_entry entries[UNIFROG_CORE_REGISTRY_MAX];
   unsigned count;
};

void unifrog_core_registry_init(struct unifrog_core_registry *registry);
int unifrog_core_registry_read_header(const char *path,
   struct unifrog_core_module_header *header);
int unifrog_core_registry_header_valid(
   const struct unifrog_core_module_header *header);
int unifrog_core_registry_scan(struct unifrog_core_registry *registry,
   const char *root);
const struct unifrog_core_registry_entry *unifrog_core_registry_find(
   const struct unifrog_core_registry *registry, const char *core_id);
const struct unifrog_core_registry_entry *unifrog_core_registry_find_path(
   const struct unifrog_core_registry *registry, const char *path);
int unifrog_core_registry_entry_supports_path(
   const struct unifrog_core_registry_entry *entry, const char *path);
unsigned unifrog_core_registry_collect_path(
   const struct unifrog_core_registry *registry, const char *path,
   char ids[][UNIFROG_CORE_MODULE_ID_MAX], unsigned count, unsigned max_ids);
unsigned unifrog_core_registry_collect_all(
   const struct unifrog_core_registry *registry,
   char ids[][UNIFROG_CORE_MODULE_ID_MAX], unsigned count, unsigned max_ids);
unsigned unifrog_core_registry_collect_family(
   const struct unifrog_core_registry *registry, const char *core_id,
   char ids[][UNIFROG_CORE_MODULE_ID_MAX], unsigned count, unsigned max_ids);

#ifdef __cplusplus
}
#endif

#endif
