#include <unifrog/core_registry.h>

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define CORE_MODULE_TRAILING_PAD_MAX 64u

#ifdef UNIFROG_CORE_REGISTRY_NATIVE_DLOPEN
#include <dlfcn.h>
#include <libretro.h>
#endif

static int ends_with_ci(const char *text, const char *suffix)
{
   size_t text_len;
   size_t suffix_len;

   if (!text || !suffix)
      return 0;
   text_len = strlen(text);
   suffix_len = strlen(suffix);
   return text_len >= suffix_len &&
      strcasecmp(text + text_len - suffix_len, suffix) == 0;
}

static int core_registry_path_join(char *dst, size_t dst_size, const char *root,
   const char *name)
{
   int written;

   if (!dst || dst_size == 0 || !root || !name)
      return -1;
   written = snprintf(dst, dst_size, "%s%s%s", root,
      root[0] && root[strlen(root) - 1u] == '/' ? "" : "/", name);
   return written < 0 || (size_t)written >= dst_size ? -1 : 0;
}

int unifrog_core_registry_header_valid(
   const struct unifrog_core_module_header *header)
{
   return unifrog_core_module_header_layout_valid(header);
}

int unifrog_core_registry_read_header(const char *path,
   struct unifrog_core_module_header *header)
{
   FILE *file;
   size_t got;
   long file_bytes;
   uintptr_t image_bytes;

   if (!path || !path[0] || !header)
      return -1;
   memset(header, 0, sizeof(*header));
   file = fopen(path, "rb");
   if (!file)
      return -1;
   got = fread(header, 1, sizeof(*header), file);
   if (got != sizeof(*header) ||
       fseek(file, 0, SEEK_END) != 0 ||
       (file_bytes = ftell(file)) < 0) {
      fclose(file);
      return -1;
   }
   fclose(file);
   if (!unifrog_core_registry_header_valid(header))
      return -1;
   image_bytes = header->file_end_addr - header->load_addr;
   return (uint64_t)(unsigned long)file_bytes +
      CORE_MODULE_TRAILING_PAD_MAX >= (uint64_t)image_bytes ? 0 : -1;
}

static unsigned add_id(char ids[][UNIFROG_CORE_MODULE_ID_MAX], unsigned count,
   unsigned max_ids, const char *id)
{
   if (!id || !id[0] || count >= max_ids)
      return count;
   for (unsigned i = 0; i < count; i++) {
      if (strcmp(ids[i], id) == 0)
         return count;
   }
   snprintf(ids[count], UNIFROG_CORE_MODULE_ID_MAX, "%s", id);
   return count + 1u;
}

void unifrog_core_registry_init(struct unifrog_core_registry *registry)
{
   if (registry)
      memset(registry, 0, sizeof(*registry));
}

#ifdef UNIFROG_CORE_REGISTRY_NATIVE_DLOPEN
static int native_core_id(char *id, size_t id_size, const char *name)
{
   static const char suffix[] = "_libretro.so";
   static const char prefix[] = "libretro_";
   size_t len;

   if (!id || id_size == 0 || !name || !ends_with_ci(name, ".so"))
      return -1;
   if (strncasecmp(name, prefix, sizeof(prefix) - 1u) == 0)
      name += sizeof(prefix) - 1u;
   len = strlen(name);
   if (len > sizeof(suffix) - 1u &&
       strcasecmp(name + len - (sizeof(suffix) - 1u), suffix) == 0)
      len -= sizeof(suffix) - 1u;
   else
      len -= 3u;
   if (len == 0 || len >= id_size)
      return -1;
   memcpy(id, name, len);
   id[len] = '\0';
   return 0;
}

static int read_native_core(const char *path, const char *name,
   struct unifrog_core_registry_entry *out)
{
   typedef void (*get_system_info_fn)(struct retro_system_info *);
   struct retro_system_info info;
   get_system_info_fn get_system_info;
   void *handle;

   memset(out, 0, sizeof(*out));
   if (native_core_id(out->header.core_id,
       sizeof(out->header.core_id), name) != 0)
      return -1;
   handle = dlopen(path, RTLD_LAZY | RTLD_LOCAL);
   if (!handle)
      return -1;
   get_system_info = (get_system_info_fn)dlsym(handle,
      "retro_get_system_info");
   if (!get_system_info) {
      dlclose(handle);
      return -1;
   }
   memset(&info, 0, sizeof(info));
   get_system_info(&info);
   if (info.valid_extensions)
      snprintf(out->header.extensions, sizeof(out->header.extensions), "%s",
         info.valid_extensions);
   dlclose(handle);
   snprintf(out->path, sizeof(out->path), "%s", path);
   out->format = UNIFROG_CORE_REGISTRY_NATIVE;
   return 0;
}
#endif

static int registry_has_id(const struct unifrog_core_registry *registry,
   const char *id)
{
   for (unsigned i = 0; i < registry->count; i++) {
      if (strcmp(registry->entries[i].header.core_id, id) == 0)
         return 1;
   }
   return 0;
}

static int scan_root(struct unifrog_core_registry *registry, const char *root)
{
   DIR *dir;
   struct dirent *entry;

   if (!registry || !root || !root[0])
      return -1;
   dir = opendir(root);
   if (!dir)
      return -1;
   while (registry->count < UNIFROG_CORE_REGISTRY_MAX &&
          (entry = readdir(dir)) != NULL) {
      struct unifrog_core_registry_entry *out;
      char path[UNIFROG_CORE_REGISTRY_PATH_MAX];

      if (entry->d_name[0] == '.' ||
          core_registry_path_join(path, sizeof(path), root, entry->d_name) != 0)
         continue;
      out = &registry->entries[registry->count];
      if (ends_with_ci(entry->d_name, ".bin")) {
#ifdef UNIFROG_CORE_REGISTRY_NATIVE_DLOPEN
         continue;
#else
         if (unifrog_core_registry_read_header(path, &out->header) != 0)
            continue;
         snprintf(out->path, sizeof(out->path), "%s", path);
         out->format = UNIFROG_CORE_REGISTRY_MODULE;
#endif
#ifdef UNIFROG_CORE_REGISTRY_NATIVE_DLOPEN
      } else if (ends_with_ci(entry->d_name, ".so")) {
         if (read_native_core(path, entry->d_name, out) != 0)
            continue;
#endif
      } else {
         continue;
      }
      if (registry_has_id(registry, out->header.core_id))
         continue;
      registry->count++;
   }
   closedir(dir);
   return 0;
}

int unifrog_core_registry_scan(struct unifrog_core_registry *registry,
   const char *root)
{
   int found;

   if (!registry || !root)
      return -1;
   unifrog_core_registry_init(registry);
   found = scan_root(registry, root) == 0;
#ifdef UNIFROG_CORE_REGISTRY_NATIVE_DLOPEN
   {
      static const char *const system_roots[] = {
         "/usr/lib/libretro",
         "/usr/lib64/libretro",
         "/usr/local/lib/libretro",
      };
      const char *env_root = getenv("UNIFROG_LINUX_CORE_ROOT");

      if (env_root && strcmp(env_root, root) != 0)
         found |= scan_root(registry, env_root) == 0;
      for (unsigned i = 0; i < sizeof(system_roots) / sizeof(system_roots[0]);
           i++) {
         if (strcmp(system_roots[i], root) != 0 &&
             (!env_root || strcmp(system_roots[i], env_root) != 0))
            found |= scan_root(registry, system_roots[i]) == 0;
      }
   }
#endif
   return found ? 0 : -1;
}

const struct unifrog_core_registry_entry *unifrog_core_registry_find_path(
   const struct unifrog_core_registry *registry, const char *path)
{
   if (!registry || !path)
      return NULL;
   for (unsigned i = 0; i < registry->count; i++) {
      if (strcmp(registry->entries[i].path, path) == 0)
         return &registry->entries[i];
   }
   return NULL;
}

const struct unifrog_core_registry_entry *unifrog_core_registry_find(
   const struct unifrog_core_registry *registry, const char *core_id)
{
   if (!registry || !core_id)
      return NULL;
   for (unsigned i = 0; i < registry->count; i++) {
      if (strcmp(registry->entries[i].header.core_id, core_id) == 0)
         return &registry->entries[i];
   }
   return NULL;
}

int unifrog_core_registry_entry_supports_path(
   const struct unifrog_core_registry_entry *entry, const char *path)
{
   const char *cursor;
   const char *extension;
   size_t extension_len;

   if (!entry || !path || !entry->header.extensions[0])
      return 0;
   extension = strrchr(path, '.');
   if (!extension || !extension[1])
      return 0;
   extension++;
   extension_len = strlen(extension);
   cursor = entry->header.extensions;
   while (*cursor) {
      const char *end = strchr(cursor, '|');
      size_t len = end ? (size_t)(end - cursor) : strlen(cursor);

      if (extension_len == len && strncasecmp(extension, cursor, len) == 0)
         return 1;
      if (!end)
         break;
      cursor = end + 1u;
   }
   return 0;
}

unsigned unifrog_core_registry_collect_path(
   const struct unifrog_core_registry *registry, const char *path,
   char ids[][UNIFROG_CORE_MODULE_ID_MAX], unsigned count, unsigned max_ids)
{
   if (!registry || !ids)
      return count;
   for (unsigned i = 0; i < registry->count; i++) {
      if (unifrog_core_registry_entry_supports_path(&registry->entries[i],
          path))
         count = add_id(ids, count, max_ids,
            registry->entries[i].header.core_id);
   }
   return count;
}

unsigned unifrog_core_registry_collect_all(
   const struct unifrog_core_registry *registry,
   char ids[][UNIFROG_CORE_MODULE_ID_MAX], unsigned count, unsigned max_ids)
{
   if (!registry || !ids)
      return count;
   for (unsigned i = 0; i < registry->count; i++)
      count = add_id(ids, count, max_ids, registry->entries[i].header.core_id);
   return count;
}

unsigned unifrog_core_registry_collect_family(
   const struct unifrog_core_registry *registry, const char *core_id,
   char ids[][UNIFROG_CORE_MODULE_ID_MAX], unsigned count, unsigned max_ids)
{
   const struct unifrog_core_registry_entry *base =
      unifrog_core_registry_find(registry, core_id);

   if (!base)
      return count;
   for (unsigned i = 0; i < registry->count; i++) {
      /*
       * Families intentionally require an identical declared extension set.
       * A loose overlap would group unrelated CD cores through generic
       * extensions such as cue, chd, or iso.
       */
      if (strcasecmp(base->header.extensions,
          registry->entries[i].header.extensions) == 0)
         count = add_id(ids, count, max_ids,
            registry->entries[i].header.core_id);
   }
   return count;
}
