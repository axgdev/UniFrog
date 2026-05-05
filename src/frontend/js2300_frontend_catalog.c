#include "js2300_frontend_internal.h"

int is_video_file(const char *name)
{
   static const char *suffixes[] = {
      ".mp4", ".mov", ".mkv", ".avi", ".ts",
      ".m2ts", ".mpg", ".mpeg", ".h264", ".264",
      ".mp3", ".wav", ".flac", ".ogg", ".opus", ".aac", ".m4a",
      ".jpg", ".jpeg", ".png", ".gif", ".bmp",
   };

   for (unsigned i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); i++) {
      if (unifrog_text_ends_with_ci(name, suffixes[i]))
         return 1;
   }
   return 0;
}

static const char *const frontend_psx_folders[] = {
   "psx", "ps", "ps1", "playstation", "playstation1", "sony playstation",
};
static const char *const frontend_gba_folders[] = {
   "gba", "gbadvance", "gameboyadvance", "game boy advance",
};
static const char *const frontend_gb_folders[] = {
   "gb", "gbc", "gbb", "gameboy", "game boy", "gameboycolor",
   "game boy color",
};
static const char *const frontend_nes_folders[] = {
   "nes", "fc", "fds", "famicom", "nintendo", "nintendo entertainment system",
};
static const char *const frontend_snes_folders[] = {
   "snes", "sfc", "super nintendo", "super nintendo entertainment system",
   "super famicom",
};
static const char *const frontend_genesis_folders[] = {
   "genesis", "megadrive", "mega drive", "md", "sms", "mastersystem",
   "master system", "gg", "gamegear", "game gear", "sg", "sg1000",
};
static const char *const frontend_pce_folders[] = {
   "pce", "pcengine", "pc engine", "tg16", "turbografx", "turbografx16",
   "turbografx-16", "sgx", "supergrafx",
};
static const char *const frontend_media_folders[] = {
   "video", "videos", "media", "movies",
};

/* Keep this order in sync with frontend/main.js coreCatalog. */
static const struct frontend_catalog_entry frontend_catalog[] = {
   { "Game Boy Advance", "gpsp",
      frontend_gba_folders, FRONTEND_ARRAY_SIZE(frontend_gba_folders) },
   { "Game Boy / Color", "gambatte",
      frontend_gb_folders, FRONTEND_ARRAY_SIZE(frontend_gb_folders) },
   { "Nintendo (NES)", "quicknes",
      frontend_nes_folders, FRONTEND_ARRAY_SIZE(frontend_nes_folders) },
   { "Super Nintendo", "snes9x2005",
      frontend_snes_folders, FRONTEND_ARRAY_SIZE(frontend_snes_folders) },
   { "Sega Genesis", "picodrive",
      frontend_genesis_folders, FRONTEND_ARRAY_SIZE(frontend_genesis_folders) },
   { "PC Engine", "pce-fast",
      frontend_pce_folders, FRONTEND_ARRAY_SIZE(frontend_pce_folders) },
   { "PlayStation", "qpsx",
      frontend_psx_folders, FRONTEND_ARRAY_SIZE(frontend_psx_folders) },
   { "Media", "pmp-video",
      frontend_media_folders, FRONTEND_ARRAY_SIZE(frontend_media_folders) },
};

static char frontend_ascii_lower(char c)
{
   if (c >= 'A' && c <= 'Z')
      return (char)(c - 'A' + 'a');
   return c;
}

static int frontend_equals_ci(const char *a, const char *b)
{
   if (!a || !b)
      return 0;
   while (*a && *b) {
      if (frontend_ascii_lower(*a) != frontend_ascii_lower(*b))
         return 0;
      a++;
      b++;
   }
   return *a == '\0' && *b == '\0';
}

static int frontend_is_alias(const char *name, const char *const *folders,
   unsigned folder_count)
{
   for (unsigned i = 0; i < folder_count; i++) {
      if (frontend_equals_ci(name, folders[i]))
         return 1;
   }
   return 0;
}

static int frontend_is_compressed_wrapper(const char *name)
{
   return unifrog_text_ends_with_ci(name, ".zip") ||
      unifrog_text_ends_with_ci(name, ".lz4") ||
      unifrog_text_ends_with_ci(name, ".zst") ||
      unifrog_text_ends_with_ci(name, ".zstd");
}

static int frontend_is_legacy_stub_name(const char *name)
{
   const char *semi = name ? strchr(name, ';') : NULL;

   return semi && semi > name;
}

static void frontend_strip_one_extension(const char *name, char *out,
   size_t out_size)
{
   const char *dot = name ? strrchr(name, '.') : NULL;
   size_t len;

   if (!out || out_size == 0)
      return;
   if (!name || !dot || dot == name) {
      unifrog_text_copy(out, out_size, name ? name : "");
      return;
   }
   len = (size_t)(dot - name);
   if (len >= out_size)
      len = out_size - 1u;
   memcpy(out, name, len);
   out[len] = '\0';
}

static void frontend_catalog_name(const char *name, char *out, size_t out_size)
{
   if (frontend_is_compressed_wrapper(name))
      frontend_strip_one_extension(name, out, out_size);
   else
      unifrog_text_copy(out, out_size, name ? name : "");
}

const struct frontend_catalog_entry *frontend_catalog_for_dir_name(
   const char *name)
{
   for (unsigned i = 0; i < FRONTEND_ARRAY_SIZE(frontend_catalog); i++) {
      if (frontend_is_alias(name, frontend_catalog[i].folders,
          frontend_catalog[i].folder_count))
         return &frontend_catalog[i];
   }
   return NULL;
}

const struct frontend_catalog_entry *frontend_catalog_for_path(
   const char *path)
{
   char part[JS2300_FRONTEND_MAX_PATH];
   size_t len = 0;

   if (!path)
      return NULL;
   for (const char *p = path; ; p++) {
      if (*p == '/' || *p == '\0') {
         if (len > 0) {
            part[len] = '\0';
            const struct frontend_catalog_entry *entry =
               frontend_catalog_for_dir_name(part);

            if (entry)
               return entry;
            len = 0;
         }
         if (*p == '\0')
            break;
      } else if (len + 1u < sizeof(part)) {
         part[len++] = *p;
      }
   }
   return NULL;
}

static int frontend_has_psx_folder_hint(const char *dir)
{
   char part[JS2300_FRONTEND_MAX_PATH];
   size_t len = 0;

   if (!dir)
      return 0;
   for (const char *p = dir; ; p++) {
      if (*p == '/' || *p == '\0') {
         if (len > 0) {
            part[len] = '\0';
            if (frontend_is_alias(part, frontend_psx_folders,
                FRONTEND_ARRAY_SIZE(frontend_psx_folders)))
               return 1;
            len = 0;
         }
         if (*p == '\0')
            break;
      } else if (len + 1u < sizeof(part)) {
         part[len++] = *p;
      }
   }
   return 0;
}

int frontend_should_hide_file(const char *name, const char *dir)
{
   char detect[JS2300_FRONTEND_MAX_PATH];

   if (frontend_is_legacy_stub_name(name))
      return 1;
   frontend_catalog_name(name, detect, sizeof(detect));
   return unifrog_text_ends_with_ci(detect, ".bin") &&
      frontend_has_psx_folder_hint(dir);
}

static int frontend_skip_index_dir(const char *name, const char *full)
{
   if (!name || name[0] == '.')
      return 1;
   return strcmp(full, "/media/mmcblk0/unifrog") == 0 ||
      strcmp(full, "/media/mmcblk0/bios") == 0 ||
      strcmp(full, "/media/mmcblk0/firmware") == 0 ||
      strcmp(full, "/media/mmcblk0/System Volume Information") == 0;
}

int frontend_path_join_checked(char *dst, size_t dst_size,
   const char *base, const char *name)
{
   size_t base_len;
   size_t name_len;
   size_t slash_len;

   if (!dst || dst_size == 0 || !base || !name)
      return -1;
   base_len = strlen(base);
   name_len = strlen(name);
   slash_len = (base_len > 0 && base[base_len - 1u] != '/') ? 1u : 0u;
   if (base_len + slash_len + name_len + 1u > dst_size)
      return -1;
   unifrog_path_join(dst, dst_size, base, name);
   return 0;
}

int frontend_dirent_is_dot(const struct dirent *entry)
{
   return entry && entry->d_name[0] == '.' &&
      (entry->d_name[1] == '\0' ||
       (entry->d_name[1] == '.' && entry->d_name[2] == '\0'));
}

int frontend_dirent_is_dir(const struct dirent *entry, const char *full)
{
#if defined(DT_DIR) && defined(DT_UNKNOWN)
   if (entry->d_type == DT_DIR)
      return 1;
   if (entry->d_type != DT_UNKNOWN)
      return 0;
#endif
   {
      struct stat st;

      if (stat(full, &st) != 0)
         return -1;
      return S_ISDIR(st.st_mode) ? 1 : 0;
   }
}

static int frontend_index_note_bytes(struct frontend_index_scan *scan,
   uint32_t *bytes, int written)
{
   if (written < 0)
      return -1;
   *bytes += (uint32_t)written;
   if (*bytes >= JS2300_FRONTEND_INDEX_MAX_BYTES) {
      scan->result->truncated = 1;
      return 1;
   }
   return 0;
}

static int frontend_index_write_game(struct frontend_index_scan *scan,
   const struct frontend_catalog_entry *catalog, const char *path,
   const char *label)
{
   int written;

   if (scan->game_bytes >= JS2300_FRONTEND_INDEX_MAX_BYTES) {
      scan->result->truncated = 1;
      return 2;
   }
   written = fprintf(scan->game_file, "game|%s|%s|%s|%s\n",
      catalog->system, catalog->core, path, label);
   return frontend_index_note_bytes(scan, &scan->game_bytes, written);
}

static int frontend_index_write_media(struct frontend_index_scan *scan,
   const char *path, const char *label)
{
   int written;

   if (scan->media_bytes >= JS2300_FRONTEND_INDEX_MAX_BYTES) {
      scan->result->truncated = 1;
      return 2;
   }
   written = fprintf(scan->media_file, "media|Media|video|%s|%s\n",
      path, label);
   return frontend_index_note_bytes(scan, &scan->media_bytes, written);
}

static int frontend_index_scan_system_dir(const char *dir, unsigned depth,
   struct frontend_index_scan *scan,
   const struct frontend_catalog_entry *catalog)
{
   DIR *handle;
   struct dirent *entry;
   int ret = 0;

   if (depth > JS2300_FRONTEND_INDEX_MAX_DEPTH ||
       scan->result->dirs >= JS2300_FRONTEND_INDEX_MAX_DIRS) {
      scan->result->truncated = 1;
      return 0;
   }

   handle = opendir(dir);
   if (!handle)
      return 0;
   scan->result->dirs++;

   while ((entry = readdir(handle)) != NULL) {
      char full[JS2300_FRONTEND_MAX_PATH];
      int is_dir;

      if (frontend_dirent_is_dot(entry))
         continue;
      if (frontend_path_join_checked(full, sizeof(full), dir,
          entry->d_name) != 0) {
         scan->result->truncated = 1;
         continue;
      }
      is_dir = frontend_dirent_is_dir(entry, full);
      if (is_dir < 0)
         continue;
      if (is_dir) {
         if (!frontend_skip_index_dir(entry->d_name, full) &&
             frontend_index_scan_system_dir(full, depth + 1u, scan,
                catalog) != 0) {
            ret = -1;
            break;
         }
         continue;
      }

      if (scan->result->files >= JS2300_FRONTEND_INDEX_MAX_FILES) {
         scan->result->truncated = 1;
         continue;
      }
      scan->result->files++;
      if (frontend_should_hide_file(entry->d_name, dir))
         continue;
      if (catalog) {
         if (strcmp(catalog->system, "Media") == 0) {
            if (is_video_file(entry->d_name)) {
               int write_ret = frontend_index_write_media(scan, full,
                  entry->d_name);

               if (write_ret < 0) {
                  ret = -1;
                  break;
               }
               if (write_ret != 2)
                  scan->result->media++;
            }
            continue;
         }
         int write_ret = frontend_index_write_game(scan, catalog, full,
            entry->d_name);

         if (write_ret < 0) {
            ret = -1;
            break;
         }
         if (write_ret != 2)
            scan->result->games++;
         if (write_ret > 0)
            continue;
      } else if (is_video_file(entry->d_name)) {
         int write_ret = frontend_index_write_media(scan, full, entry->d_name);

         if (write_ret < 0) {
            ret = -1;
            break;
         }
         if (write_ret != 2)
            scan->result->media++;
         if (write_ret > 0)
            continue;
      }
   }

   if (closedir(handle) != 0)
      ret = -1;
   return ret;
}

static int frontend_index_scan_root(const char *root,
   struct frontend_index_scan *scan)
{
   DIR *handle;
   struct dirent *entry;
   int ret = 0;

   handle = opendir(root);
   if (!handle)
      return 0;
   scan->result->dirs++;
   while ((entry = readdir(handle)) != NULL) {
      char full[JS2300_FRONTEND_MAX_PATH];
      int is_dir;
      const struct frontend_catalog_entry *catalog;

      if (frontend_dirent_is_dot(entry) || frontend_skip_index_dir(entry->d_name,
          root))
         continue;
      if (frontend_path_join_checked(full, sizeof(full), root,
          entry->d_name) != 0) {
         scan->result->truncated = 1;
         continue;
      }
      is_dir = frontend_dirent_is_dir(entry, full);
      if (is_dir <= 0)
         continue;
      catalog = frontend_catalog_for_dir_name(entry->d_name);
      if (!catalog)
         continue;
      if (frontend_index_scan_system_dir(full, 1u, scan, catalog) != 0) {
         ret = -1;
         break;
      }
   }
   if (closedir(handle) != 0)
      ret = -1;
   return ret;
}

static void frontend_expand_root(char *out, size_t out_size, const char *root,
   size_t root_len)
{
   if (root_len == 1u && root[0] == '/') {
      unifrog_text_copy(out, out_size, "/media/mmcblk0");
      return;
   }
   if (root_len >= strlen("/media/mmcblk0") &&
       memcmp(root, "/media/mmcblk0", strlen("/media/mmcblk0")) == 0) {
      size_t len = root_len < out_size - 1u ? root_len : out_size - 1u;

      memcpy(out, root, len);
      out[len] = '\0';
      return;
   }
   unifrog_text_copy(out, out_size, "/media/mmcblk0");
   if (root_len > 0 && root[0] != '/') {
      size_t used = strlen(out);

      if (used + 1u < out_size) {
         out[used++] = '/';
         out[used] = '\0';
      }
   }
   {
      size_t used = strlen(out);
      size_t copy = root_len;

      if (used + copy >= out_size)
         copy = out_size - used - 1u;
      memcpy(out + used, root, copy);
      out[used + copy] = '\0';
   }
}

int frontend_index_scan_roots(const char *roots, struct frontend_index_scan *scan)
{
   const char *start;

   if (!roots || !roots[0])
      roots = "/ROMS|/";
   start = roots;
   for (const char *p = roots; ; p++) {
      if (*p == '|' || *p == ',' || *p == '\n' || *p == '\0') {
         const char *end = p;

         while (start < p && (*start == ' ' || *start == '\t'))
            start++;
         while (end > start && (end[-1] == ' ' || end[-1] == '\t'))
            end--;
         if (end > start) {
            char expanded[JS2300_FRONTEND_MAX_PATH];

            frontend_expand_root(expanded, sizeof(expanded), start,
               (size_t)(end - start));
            if (frontend_index_scan_root(expanded, scan) != 0)
               return -1;
         }
         if (*p == '\0')
            break;
         start = p + 1;
      }
   }
   return 0;
}
