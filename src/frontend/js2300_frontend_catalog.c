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
   "/psx/", "/ps1/", "/playstation/",
};
static const char *const frontend_gba_suffixes[] = { ".gba" };
static const char *const frontend_gba_folders[] = {
   "/gba/", "/game boy advance/",
};
static const char *const frontend_gb_suffixes[] = { ".gb", ".gbc" };
static const char *const frontend_gb_folders[] = {
   "/gb/", "/gbc/", "/gbb/", "/game boy/",
};
static const char *const frontend_nes_suffixes[] = { ".nes" };
static const char *const frontend_nes_folders[] = { "/nes/", "/famicom/" };
static const char *const frontend_fds_suffixes[] = { ".fds" };
static const char *const frontend_snes_suffixes[] = { ".sfc", ".smc" };
static const char *const frontend_snes_folders[] = {
   "/snes/", "/super nintendo/",
};
static const char *const frontend_genesis_suffixes[] = {
   ".md", ".gen", ".smd", ".32x", ".sms", ".gg", ".sg",
};
static const char *const frontend_genesis_folders[] = {
   "/genesis/", "/megadrive/", "/mega drive/", "/sms/", "/gg/",
};
static const char *const frontend_pce_suffixes[] = { ".pce", ".sgx" };
static const char *const frontend_pce_folders[] = {
   "/pce/", "/pc engine/", "/turbografx/",
};
static const char *const frontend_psx_suffixes[] = {
   ".cue", ".iso", ".img", ".pbp",
};
static const char *const frontend_avi_suffixes[] = { ".avi" };
static const char *const frontend_avi_folders[] = {
   "/video/", "/videos/", "/media/",
};

/* Keep this order in sync with frontend/main.js coreCatalog. */
static const struct frontend_catalog_entry frontend_catalog[] = {
   { "Game Boy Advance", "gpsp",
      frontend_gba_suffixes, FRONTEND_ARRAY_SIZE(frontend_gba_suffixes),
      frontend_gba_folders, FRONTEND_ARRAY_SIZE(frontend_gba_folders) },
   { "Game Boy / Color", "gambatte",
      frontend_gb_suffixes, FRONTEND_ARRAY_SIZE(frontend_gb_suffixes),
      frontend_gb_folders, FRONTEND_ARRAY_SIZE(frontend_gb_folders) },
   { "Nintendo (NES)", "quicknes",
      frontend_nes_suffixes, FRONTEND_ARRAY_SIZE(frontend_nes_suffixes),
      frontend_nes_folders, FRONTEND_ARRAY_SIZE(frontend_nes_folders) },
   { "Nintendo (NES)", "fceumm",
      frontend_fds_suffixes, FRONTEND_ARRAY_SIZE(frontend_fds_suffixes),
      frontend_nes_folders, FRONTEND_ARRAY_SIZE(frontend_nes_folders) },
   { "Super Nintendo", "snes9x2005",
      frontend_snes_suffixes, FRONTEND_ARRAY_SIZE(frontend_snes_suffixes),
      frontend_snes_folders, FRONTEND_ARRAY_SIZE(frontend_snes_folders) },
   { "Sega Genesis", "picodrive",
      frontend_genesis_suffixes, FRONTEND_ARRAY_SIZE(frontend_genesis_suffixes),
      frontend_genesis_folders, FRONTEND_ARRAY_SIZE(frontend_genesis_folders) },
   { "PC Engine", "pce-fast",
      frontend_pce_suffixes, FRONTEND_ARRAY_SIZE(frontend_pce_suffixes),
      frontend_pce_folders, FRONTEND_ARRAY_SIZE(frontend_pce_folders) },
   { "PlayStation", "qpsx",
      frontend_psx_suffixes, FRONTEND_ARRAY_SIZE(frontend_psx_suffixes),
      frontend_psx_folders, FRONTEND_ARRAY_SIZE(frontend_psx_folders) },
   { "Media", "pmp-video",
      frontend_avi_suffixes, FRONTEND_ARRAY_SIZE(frontend_avi_suffixes),
      frontend_avi_folders, FRONTEND_ARRAY_SIZE(frontend_avi_folders) },
};

static char frontend_ascii_lower(char c)
{
   if (c >= 'A' && c <= 'Z')
      return (char)(c - 'A' + 'a');
   return c;
}

static int frontend_contains_ci(const char *text, const char *part)
{
   size_t part_len;

   if (!text || !part)
      return 0;
   part_len = strlen(part);
   if (part_len == 0)
      return 1;

   while (*text) {
      size_t i;

      for (i = 0; i < part_len; i++) {
         if (!text[i] ||
             frontend_ascii_lower(text[i]) != frontend_ascii_lower(part[i]))
            break;
      }
      if (i == part_len)
         return 1;
      text++;
   }
   return 0;
}

static int frontend_has_folder_hint(const char *path,
   const char *const *folders, unsigned folder_count)
{
   char with_slash[JS2300_FRONTEND_MAX_PATH + 2u];
   size_t len;

   if (!path)
      return 0;
   unifrog_text_copy(with_slash, sizeof(with_slash), path);
   len = strlen(with_slash);
   if (len > 0 && with_slash[len - 1u] != '/' &&
       len + 1u < sizeof(with_slash)) {
      with_slash[len++] = '/';
      with_slash[len] = '\0';
   }
   for (unsigned i = 0; i < folder_count; i++) {
      if (frontend_contains_ci(with_slash, folders[i]))
         return 1;
   }
   return 0;
}

static int frontend_has_suffixes(const char *text, const char *const *suffixes,
   unsigned suffix_count)
{
   for (unsigned i = 0; i < suffix_count; i++) {
      if (unifrog_text_ends_with_ci(text, suffixes[i]))
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

const struct frontend_catalog_entry *frontend_catalog_for_path(
   const char *path)
{
   char detect[JS2300_FRONTEND_MAX_PATH];

   frontend_catalog_name(path, detect, sizeof(detect));
   for (unsigned i = 0; i < FRONTEND_ARRAY_SIZE(frontend_catalog); i++) {
      if (frontend_has_suffixes(detect, frontend_catalog[i].suffixes,
          frontend_catalog[i].suffix_count))
         return &frontend_catalog[i];
   }
   for (unsigned i = 0; i < FRONTEND_ARRAY_SIZE(frontend_catalog); i++) {
      if (frontend_has_folder_hint(path, frontend_catalog[i].folders,
          frontend_catalog[i].folder_count))
         return &frontend_catalog[i];
   }
   return NULL;
}

int frontend_is_game_name(const char *name)
{
   char detect[JS2300_FRONTEND_MAX_PATH];

   if (frontend_is_legacy_stub_name(name))
      return 0;
   if (frontend_is_compressed_wrapper(name))
      return 1;
   frontend_catalog_name(name, detect, sizeof(detect));
   for (unsigned i = 0; i < FRONTEND_ARRAY_SIZE(frontend_catalog); i++) {
      if (frontend_has_suffixes(detect, frontend_catalog[i].suffixes,
          frontend_catalog[i].suffix_count))
         return 1;
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
      frontend_has_folder_hint(dir, frontend_psx_folders,
         FRONTEND_ARRAY_SIZE(frontend_psx_folders));
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

int frontend_index_scan_dir(const char *dir, unsigned depth,
   struct frontend_index_scan *scan)
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
             frontend_index_scan_dir(full, depth + 1u, scan) != 0) {
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
      if (frontend_is_game_name(entry->d_name)) {
         const struct frontend_catalog_entry *catalog =
            frontend_catalog_for_path(full);

         if (catalog) {
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
         }
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

