#include "frontend_internal.h"

/* Frontend content classification, core candidates, and open-with helpers. */
int is_media_file(const char *path)
{
   return unifrog_media_path_is_supported(path);
}

int is_reader_file(const char *path)
{
   return unifrog_reader_path_supported(path);
}

int media_path_is_audio(const char *path)
{
   return unifrog_media_path_is_audio(path);
}

int media_handler_route(const char *handler,
   enum unifrog_media_route *route)
{
   return unifrog_media_route_parse(handler, route) == 0;
}

int media_path_has_open_with_choices(const char *path)
{
   return unifrog_media_route_available(path, UNIFROG_MEDIA_ROUTE_NATIVE,
      UNIFROG_HCRTOS_MEDIA_FIRMWARE);
}

static int is_builtin_handler(const char *handler)
{
   enum unifrog_media_route route;

   return handler && (strcmp(handler, "reader") == 0 ||
      media_handler_route(handler, &route));
}

int is_asd_file(const char *path)
{
   return path && unifrog_text_ends_with_ci(path, ".asd");
}

int is_js_script_file(const char *path)
{
   return path && (unifrog_text_ends_with_ci(path, ".js") ||
      unifrog_text_ends_with_ci(path, ".mjs"));
}

int is_js_libretro_core_script(const char *path)
{
   FILE *file;
   char buf[256];
   size_t got;

   if (!path)
      return 0;
   file = fopen(path, "rb");
   if (!file)
      return 0;
   got = fread(buf, 1, sizeof(buf) - 1u, file);
   fclose(file);
   buf[got] = '\0';
   return strstr(buf, "unifrog: type=libretro-core") != NULL;
}

int is_zip_file(const char *path)
{
   return path && unifrog_text_ends_with_ci(path, ".zip");
}

int is_core_module_file(const char *path)
{
   return path && unifrog_text_ends_with_ci(path, ".bin");
}

int core_module_header_compatible(const struct unifrog_core_module_header *h)
{
   const struct unifrog_abi *abi = unifrog_abi_get();
   size_t required_size = h && h->required_abi_size ?
      h->required_abi_size : UNIFROG_ABI_CORE_MIN_SIZE;

   return unifrog_core_registry_header_valid(h) &&
      unifrog_abi_table_compatible(abi, h->required_abi_version,
         required_size);
}

void core_module_meta(char *dst, size_t dst_size,
   const struct unifrog_core_module_header *h)
{
   if (!dst || dst_size == 0)
      return;
   if (!unifrog_core_registry_header_valid(h)) {
      snprintf(dst, dst_size, "invalid");
      return;
   }
   snprintf(dst, dst_size, "%s abi %u.%u.%u",
      core_module_header_compatible(h) ? "ok" : "bad",
      (unsigned)UNIFROG_ABI_VERSION_GET_MAJOR(h->required_abi_version),
      (unsigned)UNIFROG_ABI_VERSION_GET_MINOR(h->required_abi_version),
      (unsigned)UNIFROG_ABI_VERSION_GET_PATCH(h->required_abi_version));
}

static unsigned add_core_candidate(char ids[][UNIFROG_CORE_MODULE_ID_MAX],
   unsigned count, const char *core);

static unsigned add_available_core_candidate(
   const struct frontend_state *fe,
   char ids[][UNIFROG_CORE_MODULE_ID_MAX], unsigned count, const char *core)
{
   if (fe && fe->core_registry.count &&
       !unifrog_core_registry_find(&fe->core_registry, core))
      return count;
   return add_core_candidate(ids, count, core);
}

static unsigned add_core_candidates_from_path_dirs(
   const struct frontend_state *fe, const char *path,
   char ids[][UNIFROG_CORE_MODULE_ID_MAX], unsigned count)
{
   char copy[FRONTEND_MAX_PATH];
   char *p;

   if (!path)
      return count;
   unifrog_text_copy(copy, sizeof(copy), path);
   p = copy;
   while (*p) {
      char *slash;
      const char *core;

      while (*p == '/')
         p++;
      if (!*p)
         break;
      slash = strchr(p, '/');
      if (slash)
         *slash = '\0';
      core = frontend_rom_system_mapped_core(fe, p);
      if (core)
         count = add_available_core_candidate(fe, ids, count, core);
      if (!slash)
         break;
      p = slash + 1;
   }
   return count;
}

static const char *core_from_path_dirs(const struct frontend_state *fe,
   const char *path)
{
   char copy[FRONTEND_MAX_PATH];
   char *p;

   if (!path)
      return NULL;
   unifrog_text_copy(copy, sizeof(copy), path);
   p = copy;
   while (*p) {
      char *slash;
      const char *core;

      while (*p == '/')
         p++;
      if (!*p)
         break;
      slash = strchr(p, '/');
      if (slash)
         *slash = '\0';
      core = frontend_rom_system_mapped_core(fe, p);
      if (core)
         return core;
      if (!slash)
         break;
      p = slash + 1;
   }
   return NULL;
}

const char *safe_core_for_path(const struct frontend_state *fe,
   const char *path, const char *core)
{
   const struct frontend_association *association;

   if (fe && unifrog_core_registry_find(&fe->core_registry, core))
      return core;
   association = frontend_association_for_path(fe, path);
   if (association && association->default_handler[0] &&
       !is_builtin_handler(association->default_handler) &&
       (!fe || !fe->core_registry.count ||
        unifrog_core_registry_find(&fe->core_registry,
           association->default_handler)))
      return association->default_handler;
   return "";
}

static unsigned add_core_candidate(char ids[][UNIFROG_CORE_MODULE_ID_MAX],
   unsigned count, const char *core)
{
   if (!core || !core[0])
      return count;
   for (unsigned i = 0; i < count; i++) {
      if (strcmp(ids[i], core) == 0)
         return count;
   }
   if (count >= FRONTEND_CORE_CANDIDATE_MAX)
      return count;
   unifrog_text_copy(ids[count], UNIFROG_CORE_MODULE_ID_MAX, core);
   return count + 1u;
}

unsigned collect_core_candidates(const struct frontend_state *fe,
   const char *path, char ids[][UNIFROG_CORE_MODULE_ID_MAX])
{
   const struct frontend_association *association =
      frontend_association_for_path(fe, path);
   unsigned count = 0;

   count = add_core_candidates_from_path_dirs(fe, path, ids, count);
   if (association) {
      for (unsigned i = 0; i < association->handler_count; i++) {
         const char *handler = association->handlers[i];

         if (!is_builtin_handler(handler))
            count = add_available_core_candidate(fe, ids, count, handler);
      }
   }
   count = unifrog_core_registry_collect_path(&fe->core_registry, path, ids,
      count, FRONTEND_CORE_CANDIDATE_MAX);
   if (is_zip_file(path)) {
      unsigned family_count = count;

      for (unsigned i = 0; i < family_count; i++)
         count = unifrog_core_registry_collect_family(&fe->core_registry,
            ids[i], ids, count, FRONTEND_CORE_CANDIDATE_MAX);
      if (!count)
         count = unifrog_core_registry_collect_all(&fe->core_registry, ids,
            count, FRONTEND_CORE_CANDIDATE_MAX);
   }
   return count;
}

unsigned collect_other_core_candidates(const struct frontend_state *fe,
   char ids[][UNIFROG_CORE_MODULE_ID_MAX])
{
   unsigned count = 0;

   if (fe)
      count = unifrog_core_registry_collect_all(&fe->core_registry, ids,
         count, FRONTEND_CORE_CANDIDATE_MAX);
   if (count)
      return count;
   for (unsigned i = 0; i < fe->association_count; i++) {
      const struct frontend_association *association = &fe->associations[i];

      for (unsigned j = 0; j < association->handler_count; j++) {
         if (!is_builtin_handler(association->handlers[j]))
            count = add_core_candidate(ids, count, association->handlers[j]);
      }
   }
   return count;
}

int is_content_file(const struct frontend_state *fe, const char *path)
{
   return frontend_association_for_path(fe, path) != NULL ||
      is_media_file(path) || is_reader_file(path);
}

const char *default_handler_for_path(const struct frontend_state *fe,
   const char *path)
{
   const struct frontend_association *association =
      frontend_association_for_path(fe, path);
   const char *core;

   if (association && association->default_handler[0] &&
       is_builtin_handler(association->default_handler))
      return association->default_handler;
   core = core_from_path_dirs(fe, path);
   if (core)
      return core;
   return association && association->default_handler[0] ?
      association->default_handler : "";
}
