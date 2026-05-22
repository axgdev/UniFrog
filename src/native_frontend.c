#include <unifrog/native_frontend.h>

#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/unistd.h>

#include <kernel/lib/console.h>
#include <kernel/lib/zlib.h>

#include <frontend/js2300_frontend.h>

#include <unifrog/backlight.h>
#include <unifrog/audio.h>
#include <unifrog/battery.h>
#include <unifrog/boot.h>
#include <unifrog/boot_logo.h>
#include <unifrog/build_info.h>
#include <unifrog/core_module.h>
#include <unifrog/diag.h>
#include <unifrog/exception_record.h>
#include <unifrog/gfx.h>
#include <unifrog/input.h>
#include <unifrog/libretro_host.h>
#include <unifrog/log.h>
#include <unifrog/media.h>
#include <unifrog/frontend_lvgl.h>
#include <unifrog/paths.h>
#include <unifrog/platform.h>
#include <unifrog/perf.h>
#include <unifrog/storage_probe.h>
#include <unifrog/text.h>
#include <unifrog/ui.h>

#define FRONTEND_ROOT UNIFROG_SD_ROOT
#define FRONTEND_ROMS_ROOT UNIFROG_ROMS_ROOT
#define FRONTEND_DIST_ROOT UNIFROG_DIST_ROOT
#define FRONTEND_DATA_ROOT UNIFROG_DATA_ROOT
#define FRONTEND_ARCHIVE_ROOT UNIFROG_ARCHIVE_ROOT
#define FRONTEND_STOCK_ARCHIVE_ROOT FRONTEND_ROOT "/ARCHIVE"
#define FRONTEND_SCRIPT_ROOT UNIFROG_SCRIPT_ROOT
#define FRONTEND_THEME_ROOT UNIFROG_THEME_ROOT
#define FRONTEND_LANGUAGE_ROOT UNIFROG_LANGUAGE_ROOT
#define FRONTEND_FIRMWARE_ROOT UNIFROG_USER_FIRMWARE_ROOT
#define FRONTEND_UPDATE_ROOT UNIFROG_UPDATE_ROOT
#define FRONTEND_VERSION_ROOT UNIFROG_VERSION_ROOT
#define FRONTEND_ACTIVE_VERSION_PATH UNIFROG_ACTIVE_VERSION_PATH
#define FRONTEND_SETTINGS_PATH UNIFROG_SETTINGS_PATH
#define FRONTEND_HISTORY_PATH UNIFROG_HISTORY_PATH
#define FRONTEND_FAVORITES_PATH UNIFROG_FAVORITES_PATH
#define FRONTEND_MAX_ITEMS 1024u
#define FRONTEND_MAX_PATH 256u
#define FRONTEND_MAX_LINE 384u
#define FRONTEND_ROWS 8u
#define FRONTEND_HISTORY_MAX 64u
#define FRONTEND_FAVORITES_MAX 256u
#define FRONTEND_NAV_MAX 16u
#define FRONTEND_NAV_LOG_STEP 16u
#define FRONTEND_JUMP_FALLBACK_STEP 50u
#define FRONTEND_ROM_ROOT_LABEL_MAX 48u
#define FRONTEND_I18N_MAX 80u
#define FRONTEND_SCHEME_MAX 96u
#define FRONTEND_LVGL_LIST_ROWS 8u
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

enum frontend_view {
   FRONTEND_VIEW_LAUNCH = 0,
   FRONTEND_VIEW_EXPLORE,
   FRONTEND_VIEW_HISTORY,
   FRONTEND_VIEW_FAVORITES,
   FRONTEND_VIEW_APPS,
   FRONTEND_VIEW_CONFIG,
   FRONTEND_VIEW_CONNECT,
   FRONTEND_VIEW_CUSTOM,
   FRONTEND_VIEW_VISUAL,
   FRONTEND_VIEW_LAUNCH_SETTINGS,
   FRONTEND_VIEW_POWER,
   FRONTEND_VIEW_STORAGE,
   FRONTEND_VIEW_STORAGE_MODE,
   FRONTEND_VIEW_STORAGE_CONFIRM,
   FRONTEND_VIEW_ROM_SYSTEMS,
   FRONTEND_VIEW_OPEN_WITH,
   FRONTEND_VIEW_THEME,
   FRONTEND_VIEW_LANGUAGE,
   FRONTEND_VIEW_FIRMWARE,
   FRONTEND_VIEW_SCRIPTS,
   FRONTEND_VIEW_UPDATES,
   FRONTEND_VIEW_CORES,
   FRONTEND_VIEW_CORE_INFO,
   FRONTEND_VIEW_PACKAGE_CHECK,
   FRONTEND_VIEW_INFO,
   FRONTEND_VIEW_SYSINFO,
};

enum frontend_item_kind {
   FRONTEND_ITEM_ACTION = 0,
   FRONTEND_ITEM_DIR,
   FRONTEND_ITEM_GAME,
   FRONTEND_ITEM_MEDIA,
   FRONTEND_ITEM_FIRMWARE,
   FRONTEND_ITEM_SCRIPT,
   FRONTEND_ITEM_THEME_ARCHIVE,
   FRONTEND_ITEM_UPDATE_ARCHIVE,
   FRONTEND_ITEM_VERSION,
   FRONTEND_ITEM_CORE_MODULE,
};

struct frontend_catalog {
   const char *core;
   const char *suffixes[8];
};

struct frontend_item {
   char name[96];
   char meta[64];
   char path[FRONTEND_MAX_PATH];
   char core[96];
   enum frontend_item_kind kind;
};

struct frontend_zip_entry {
   char name[FRONTEND_MAX_PATH];
   uint16_t flags;
   uint16_t method;
   uint32_t compressed_size;
   uint32_t uncompressed_size;
   uint32_t local_offset;
};

static int remove_tree(const char *path);

struct native_frontend {
   struct unifrog_ui ui;
   const struct unifrog_ui_theme *theme;
   struct unifrog_ui_theme active_theme;
   struct unifrog_ui_theme base_theme;
   struct unifrog_frontend_lvgl_style active_style;
   struct unifrog_frontend_lvgl_style base_style;
   struct unifrog_frontend_lvgl_style screen_style[UNIFROG_FRONTEND_LVGL_VISUAL + 1];
   struct unifrog_frontend_lvgl_style list_style;
   struct unifrog_frontend_lvgl_style view_style[FRONTEND_VIEW_SYSINFO + 1];
   uint8_t screen_style_valid[UNIFROG_FRONTEND_LVGL_VISUAL + 1];
   uint8_t view_style_valid[FRONTEND_VIEW_SYSINFO + 1];
   int dir_theme_loaded;
   enum frontend_view view;
   enum frontend_view parent_view;
   int has_parent_view;
   struct frontend_item items[FRONTEND_MAX_ITEMS];
   char item_glyph_path[FRONTEND_MAX_ITEMS][FRONTEND_MAX_PATH];
   const char *item_glyph[FRONTEND_MAX_ITEMS];
   uint8_t item_glyph_resolved[FRONTEND_MAX_ITEMS];
   unsigned item_generation;
   unsigned glyph_cache_generation;
   char glyph_cache_module[32];
   unsigned item_count;
   unsigned selected;
   unsigned scroll;
   char nav_path[FRONTEND_NAV_MAX][FRONTEND_MAX_PATH];
   unsigned nav_selected[FRONTEND_NAV_MAX];
   unsigned nav_count;
   enum frontend_view view_stack[FRONTEND_NAV_MAX];
   unsigned view_stack_selected[FRONTEND_NAV_MAX];
   unsigned view_stack_scroll[FRONTEND_NAV_MAX];
   unsigned view_stack_count;
   char title[64];
   char current_dir[FRONTEND_MAX_PATH];
   char last_path[FRONTEND_MAX_PATH];
   char last_core[24];
   struct frontend_item pending_open_item;
   int pending_open_valid;
   char rom_root[FRONTEND_MAX_PATH];
   char rom_root_label[FRONTEND_ROM_ROOT_LABEL_MAX];
   char status[96];
   struct unifrog_battery_status battery;
   uint32_t battery_ms;
   struct unifrog_libretro_run_options run_options;
   int sort_desc;
   int show_hidden;
   int folder_counts;
   int mixed_content;
   int display_empty_folder;
   int menu_counter_folder;
   int menu_counter_file;
   int content_collect;
   int content_history;
   int clock_enabled;
   int title_include_root;
   int theme_alternate;
   int boxart_hidden;
   int launch_splash;
   int sound_enabled;
   int log_flush_every;
   int language_index;
   char theme_name[48];
   char loaded_theme_name[48];
   char loaded_theme_language[48];
   char resource_cache_key[64];
   char language_name[48];
   char scheme_name[FRONTEND_SCHEME_MAX][32];
   unsigned scheme_count;
   char i18n_key[FRONTEND_I18N_MAX][40];
   char i18n_value[FRONTEND_I18N_MAX][72];
   unsigned i18n_count;
   char storage_profile[16];
   char storage_pending_profile[16];
   int running;
   int needs_draw;
   int last_draw_valid;
   int theme_loaded;
   int loaded_theme_alternate;
   uint32_t last_draw_signature;
   unsigned nav_log_last_selected;
   enum frontend_view nav_log_last_view;
   int applied_style_id;
};

static void show_launch(struct native_frontend *fe);
static void show_open_with(struct native_frontend *fe,
   const struct frontend_item *item);
static void draw(struct native_frontend *fe);
static void set_status(struct native_frontend *fe, const char *fmt, ...);
static void ensure_data_dirs(void);
static int frontend_path_has_dir_prefix(const char *path, const char *root);

static const char *const storage_config_profiles[] = {
   "boot", "wide1", "wide2", "wide4", "wide8", "wide10", "wide12",
   "wide14", "wide16", "wide18", "wide20", "wide22", "wide24", "wide25",
};

typedef void (*frontend_progress_cb)(void *userdata, const char *stage,
   unsigned done, unsigned total);

struct frontend_install_progress {
   struct native_frontend *fe;
   uint32_t start_ms;
   uint32_t last_draw_ms;
   unsigned last_percent;
   char title[32];
   char name[64];
};

static const struct unifrog_ui_theme frontend_theme = {
   UNIFROG_RGB565(8, 9, 12),
   UNIFROG_RGB565(20, 22, 29),
   UNIFROG_RGB565(52, 104, 132),
   UNIFROG_RGB565(238, 241, 232),
   UNIFROG_RGB565(151, 159, 157),
   UNIFROG_RGB565(238, 188, 70),
   UNIFROG_RGB565(214, 72, 77),
};

static uint32_t frontend_hash_u32(uint32_t hash, uint32_t value)
{
   hash ^= value;
   hash *= 16777619u;
   return hash;
}

static uint32_t frontend_hash_string(uint32_t hash, const char *text)
{
   const unsigned char *p = (const unsigned char *)(text ? text : "");

   while (*p) {
      hash ^= *p++;
      hash *= 16777619u;
   }
   return hash;
}

static void frontend_invalidate_draw(struct native_frontend *fe)
{
   if (fe)
      fe->last_draw_valid = 0;
}

static const struct frontend_catalog frontend_catalog[] = {
   { "gpsp", { ".gba" } },
   { "gambatte", { ".gb", ".gbc" } },
   { "quicknes", { ".nes" } },
   { "fceumm", { ".fds" } },
   { "snes9x2005", { ".sfc", ".smc" } },
   { "picodrive", { ".md", ".gen", ".smd", ".32x", ".sms", ".gg", ".sg" } },
   { "pce-fast", { ".pce", ".sgx" } },
   { "qpsx", { ".cue", ".iso", ".img", ".pbp" } },
   { "pmp-video", { ".avi" } },
};

static const char *const media_suffixes[] = {
   ".mp4", ".mov", ".mkv", ".avi", ".ts", ".m2ts", ".mpg", ".mpeg",
   ".h264", ".264", ".mp3", ".wav", ".flac", ".ogg", ".opus", ".aac",
   ".m4a", ".jpg", ".jpeg", ".png", ".gif", ".bmp",
};

static int frontend_sort_desc;

static int is_back_item(const struct frontend_item *item)
{
   return item && item->kind == FRONTEND_ITEM_DIR && item->path[0] == '\0';
}

static int ascii_is_digit(char c)
{
   return c >= '0' && c <= '9';
}

static char ascii_lower(char c)
{
   return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c;
}

static int natural_name_compare(const char *a, const char *b)
{
   const char *pa = a ? a : "";
   const char *pb = b ? b : "";

   while (*pa || *pb) {
      if (ascii_is_digit(*pa) && ascii_is_digit(*pb)) {
         const char *za = pa;
         const char *zb = pb;
         const char *ea;
         const char *eb;
         size_t la;
         size_t lb;

         while (*za == '0')
            za++;
         while (*zb == '0')
            zb++;
         ea = za;
         eb = zb;
         while (ascii_is_digit(*ea))
            ea++;
         while (ascii_is_digit(*eb))
            eb++;
         la = (size_t)(ea - za);
         lb = (size_t)(eb - zb);
         if (la != lb)
            return la < lb ? -1 : 1;
         for (size_t i = 0; i < la; i++) {
            if (za[i] != zb[i])
               return za[i] < zb[i] ? -1 : 1;
         }
         pa = ea;
         pb = eb;
         continue;
      }
      if (ascii_lower(*pa) != ascii_lower(*pb))
         return ascii_lower(*pa) < ascii_lower(*pb) ? -1 : 1;
      if (*pa)
         pa++;
      if (*pb)
         pb++;
   }
   return 0;
}

static const char *basename_const(const char *path)
{
   const char *slash = path ? strrchr(path, '/') : NULL;

   return slash ? slash + 1 : path ? path : "";
}

static void strip_eol(char *text)
{
   size_t len;

   if (!text)
      return;
   len = strlen(text);
   while (len > 0 && (text[len - 1u] == '\n' || text[len - 1u] == '\r' ||
          text[len - 1u] == ' ' || text[len - 1u] == '\t'))
      text[--len] = '\0';
}

static const char *read_key_value(const char *line, const char *key)
{
   size_t key_len;

   if (!line || !key)
      return NULL;
   key_len = strlen(key);
   if (strncmp(line, key, key_len) == 0 && line[key_len] == '=')
      return line + key_len + 1u;
   return NULL;
}

static int parse_int(const char *text, int fallback)
{
   char *end = NULL;
   long value;

   if (!text || !text[0])
      return fallback;
   value = strtol(text, &end, 10);
   if (!end || *end)
      return fallback;
   return (int)value;
}

static char *trim_ascii(char *text)
{
   char *end;

   if (!text)
      return text;
   while (*text == ' ' || *text == '\t')
      text++;
   end = text + strlen(text);
   while (end > text && (end[-1] == ' ' || end[-1] == '\t'))
      *--end = '\0';
   return text;
}

static uint8_t parse_alpha(const char *text, uint8_t fallback)
{
   int value = parse_int(text, fallback);

   if (value < 0)
      return 0;
   if (value > 255)
      return 255;
   return (uint8_t)value;
}

static int parse_rgb565_hex(const char *text, uint16_t *out)
{
   char *end = NULL;
   unsigned long value;

   if (!text || !text[0] || !out)
      return -1;
   value = strtoul(text, &end, 16);
   if (!end || *end || value > 0xfffful)
      return -1;
   *out = (uint16_t)value;
   return 0;
}

static uint16_t rgb888_to_rgb565(uint32_t color)
{
   unsigned r = (color >> 16) & 0xffu;
   unsigned g = (color >> 8) & 0xffu;
   unsigned b = color & 0xffu;

   return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

static int parse_theme_hex(const char *text, uint16_t *out)
{
   char *end = NULL;
   unsigned long value;

   if (!text || !text[0] || !out)
      return -1;
   while (*text == '#' || *text == ' ' || *text == '\t')
      text++;
   value = strtoul(text, &end, 16);
   if (!end || *end)
      return -1;
   if (value <= 0xfffful)
      *out = (uint16_t)value;
   else if (value <= 0xfffffful)
      *out = rgb888_to_rgb565((uint32_t)value);
   else
      return -1;
   return 0;
}

static int path_join(char *dst, size_t dst_size, const char *base,
   const char *name)
{
   size_t base_len;
   int need_slash;
   int wrote;

   if (!dst || dst_size == 0 || !base || !name)
      return -1;
   base_len = strlen(base);
   need_slash = base_len > 0 && base[base_len - 1u] != '/';
   wrote = snprintf(dst, dst_size, "%s%s%s", base, need_slash ? "/" : "",
      name);
   return wrote > 0 && (size_t)wrote < dst_size ? 0 : -1;
}

static int path_join_ini(char *dst, size_t dst_size, const char *base,
   const char *name)
{
   int wrote;

   if (!dst || dst_size == 0 || !base || !name || !name[0])
      return -1;
   wrote = snprintf(dst, dst_size, "%s/%s.ini", base, name);
   return wrote > 0 && (size_t)wrote < dst_size ? 0 : -1;
}

static int frontend_path_is_valid(const char *path)
{
   const char *p;

   if (!path || !path[0])
      return 0;
   if (strlen(path) >= FRONTEND_MAX_PATH)
      return 0;
   for (p = path; *p; p++) {
      unsigned char c = (unsigned char)*p;

      if (c < 32 || c == 127)
         return 0;
   }
   if (strstr(path, "/../") || strcmp(path, "..") == 0 ||
       unifrog_text_ends_with_ci(path, "/.."))
      return 0;
   return 1;
}

static int frontend_normalize_path(char *dst, size_t dst_size,
   const char *path)
{
   char tmp[FRONTEND_MAX_PATH];
   const char *src;
   size_t len;
   int wrote;

   if (!dst || dst_size == 0 || !path)
      return -1;
   unifrog_text_copy(tmp, sizeof(tmp), path);
   src = trim_ascii(tmp);
   if (!frontend_path_is_valid(src))
      return -1;
   len = strlen(src);
   while (len > 1u && src[len - 1u] == '/')
      tmp[--len] = '\0';
   if (strcmp(src, "/") == 0) {
      wrote = snprintf(dst, dst_size, "%s", FRONTEND_ROOT);
      return wrote > 0 && (size_t)wrote < dst_size ? 0 : -1;
   }
   if (frontend_path_has_dir_prefix(src, FRONTEND_ROOT)) {
      wrote = snprintf(dst, dst_size, "%s", src);
      return wrote > 0 && (size_t)wrote < dst_size ? 0 : -1;
   }
   if (src[0] == '/')
      return path_join(dst, dst_size, FRONTEND_ROOT, src + 1u);
   return path_join(dst, dst_size, FRONTEND_ROOT, src);
}

static int file_exists(const char *path)
{
   return path && access(path, F_OK) == 0;
}

static int write_text_file(const char *path, const char *text)
{
   FILE *file;
   size_t len;

   if (!path || !text)
      return -1;
   file = fopen(path, "wb");
   if (!file)
      return -1;
   len = strlen(text);
   if (fwrite(text, 1, len, file) != len) {
      fclose(file);
      return -1;
   }
   return fclose(file) == 0 ? 0 : -1;
}

static int read_file_key(char *dst, size_t dst_size, const char *path,
   const char *key)
{
   FILE *file;
   char line[FRONTEND_MAX_LINE];
   int ret = -1;

   if (!dst || dst_size == 0 || !path || !key)
      return -1;
   dst[0] = '\0';
   file = fopen(path, "rb");
   if (!file)
      return -1;
   while (fgets(line, sizeof(line), file)) {
      const char *value;

      strip_eol(line);
      value = read_key_value(line, key);
      if (!value)
         continue;
      unifrog_text_copy(dst, dst_size, value);
      ret = 0;
      break;
   }
   fclose(file);
   return ret;
}

static void strip_ini_suffix(char *name)
{
   size_t len = name ? strlen(name) : 0;

   if (len > 4u && strcasecmp(name + len - 4u, ".ini") == 0)
      name[len - 4u] = '\0';
}

static void strip_known_suffix(char *name, const char *suffix)
{
   size_t name_len = name ? strlen(name) : 0;
   size_t suffix_len = suffix ? strlen(suffix) : 0;

   if (name_len > suffix_len &&
       strcasecmp(name + name_len - suffix_len, suffix) == 0)
      name[name_len - suffix_len] = '\0';
}

static int mkdir_p(const char *path)
{
   char tmp[FRONTEND_MAX_PATH];
   size_t len;

   if (!path || !path[0])
      return -1;
   unifrog_text_copy(tmp, sizeof(tmp), path);
   len = strlen(tmp);
   while (len > 1u && tmp[len - 1u] == '/')
      tmp[--len] = '\0';
   for (char *p = tmp + 1; *p; p++) {
      if (*p != '/')
         continue;
      *p = '\0';
      if (access(tmp, F_OK) != 0 && mkdir(tmp, 0777) != 0)
         return -1;
      *p = '/';
   }
   if (access(tmp, F_OK) != 0 && mkdir(tmp, 0777) != 0)
      return -1;
   return 0;
}

static int ensure_parent_dir(const char *path)
{
   char tmp[FRONTEND_MAX_PATH];
   char *slash;

   if (!path)
      return -1;
   unifrog_text_copy(tmp, sizeof(tmp), path);
   slash = strrchr(tmp, '/');
   if (!slash)
      return 0;
   *slash = '\0';
   return mkdir_p(tmp);
}

static int remove_tree(const char *path)
{
   DIR *dir;
   struct dirent *entry;
   int ret = 0;

   if (!path || strlen(path) < strlen(FRONTEND_THEME_ROOT) + 2u)
      return -1;
   dir = opendir(path);
   if (!dir)
      return errno == ENOENT ? 0 : -1;
   while ((entry = readdir(dir)) != NULL) {
      char child[FRONTEND_MAX_PATH];
      struct stat st;

      if (strcmp(entry->d_name, ".") == 0 ||
          strcmp(entry->d_name, "..") == 0)
         continue;
      if (path_join(child, sizeof(child), path, entry->d_name) != 0) {
         ret = -1;
         continue;
      }
      if (stat(child, &st) == 0 && S_ISDIR(st.st_mode)) {
         if (remove_tree(child) != 0)
            ret = -1;
      } else if (unlink(child) != 0 && errno != ENOENT) {
         ret = -1;
      }
   }
   if (closedir(dir) != 0)
      ret = -1;
   if (rmdir(path) != 0 && errno != ENOENT)
      ret = -1;
   return ret;
}

static int remove_tree_under(const char *path, const char *root)
{
   DIR *dir;
   struct dirent *entry;
   size_t root_len = root ? strlen(root) : 0;
   int ret = 0;

   if (!path || !root || root_len == 0 ||
       strncmp(path, root, root_len) != 0 ||
       path[root_len] != '/' || path[root_len + 1u] == '\0')
      return -1;
   dir = opendir(path);
   if (!dir)
      return errno == ENOENT ? 0 : -1;
   while ((entry = readdir(dir)) != NULL) {
      char child[FRONTEND_MAX_PATH];
      struct stat st;

      if (strcmp(entry->d_name, ".") == 0 ||
          strcmp(entry->d_name, "..") == 0)
         continue;
      if (path_join(child, sizeof(child), path, entry->d_name) != 0) {
         ret = -1;
         continue;
      }
      if (stat(child, &st) == 0 && S_ISDIR(st.st_mode)) {
         if (remove_tree_under(child, root) != 0)
            ret = -1;
      } else if (unlink(child) != 0 && errno != ENOENT) {
         ret = -1;
      }
   }
   if (closedir(dir) != 0)
      ret = -1;
   if (rmdir(path) != 0 && errno != ENOENT)
      ret = -1;
   return ret;
}

static int copy_file_path(const char *src, const char *dst)
{
   FILE *in;
   FILE *out;
   uint8_t buf[8192];
   int ret = -1;

   if (!src || !dst || ensure_parent_dir(dst) != 0)
      return -1;
   in = fopen(src, "rb");
   if (!in)
      return -1;
   out = fopen(dst, "wb");
   if (!out) {
      fclose(in);
      return -1;
   }
   for (;;) {
      size_t got = fread(buf, 1, sizeof(buf), in);

      if (got > 0 && fwrite(buf, 1, got, out) != got)
         goto out;
      if (got < sizeof(buf)) {
         if (ferror(in))
            goto out;
         break;
      }
   }
   ret = 0;

out:
   if (fclose(out) != 0)
      ret = -1;
   if (fclose(in) != 0)
      ret = -1;
   return ret;
}

static int copy_tree_merge(const char *src, const char *dst,
   int (*skip_name)(const char *name))
{
   DIR *dir;
   struct dirent *entry;
   int ret = 0;

   if (!src || !dst || mkdir_p(dst) != 0)
      return -1;
   dir = opendir(src);
   if (!dir)
      return -1;
   while ((entry = readdir(dir)) != NULL) {
      char src_child[FRONTEND_MAX_PATH];
      char dst_child[FRONTEND_MAX_PATH];
      struct stat st;

      if (strcmp(entry->d_name, ".") == 0 ||
          strcmp(entry->d_name, "..") == 0)
         continue;
      if (skip_name && skip_name(entry->d_name))
         continue;
      if (path_join(src_child, sizeof(src_child), src, entry->d_name) != 0 ||
          path_join(dst_child, sizeof(dst_child), dst, entry->d_name) != 0) {
         ret = -1;
         continue;
      }
      if (stat(src_child, &st) != 0) {
         ret = -1;
         continue;
      }
      if (S_ISDIR(st.st_mode)) {
         if (copy_tree_merge(src_child, dst_child, skip_name) != 0)
            ret = -1;
      } else if (S_ISREG(st.st_mode)) {
         if (copy_file_path(src_child, dst_child) != 0)
            ret = -1;
      }
   }
   if (closedir(dir) != 0)
      ret = -1;
   return ret;
}

static int ends_with_any(const char *name, const char *const *suffixes,
   unsigned count)
{
   for (unsigned i = 0; i < count; i++) {
      if (suffixes[i] && unifrog_text_ends_with_ci(name, suffixes[i]))
         return 1;
   }
   return 0;
}

static const struct frontend_catalog *catalog_for_path(const char *path)
{
   for (unsigned i = 0; i < ARRAY_SIZE(frontend_catalog); i++) {
      if (ends_with_any(path, frontend_catalog[i].suffixes,
          ARRAY_SIZE(frontend_catalog[i].suffixes)))
         return &frontend_catalog[i];
   }
   return NULL;
}

static int is_media_file(const char *path)
{
   return ends_with_any(path, media_suffixes, ARRAY_SIZE(media_suffixes));
}

static int media_path_has_native_wav(const char *path)
{
   return path && unifrog_text_ends_with_ci(path, ".wav");
}

static int media_path_has_open_with_choices(const char *path)
{
   if (!path || !is_media_file(path))
      return 0;
#if UNIFROG_HCRTOS_MEDIA_FIRMWARE
   return 1;
#else
   return media_path_has_native_wav(path);
#endif
}

static int is_asd_file(const char *path)
{
   return path && unifrog_text_ends_with_ci(path, ".asd");
}

static int is_js_script_file(const char *path)
{
   return path && unifrog_text_ends_with_ci(path, ".js");
}

static int is_zip_file(const char *path)
{
   return path && unifrog_text_ends_with_ci(path, ".zip");
}

static int is_core_module_file(const char *path)
{
   return path && unifrog_text_ends_with_ci(path, ".bin");
}

static int read_core_module_header(const char *path,
   struct unifrog_core_module_header *header)
{
   FILE *file;
   size_t got;

   if (!path || !header)
      return -1;
   memset(header, 0, sizeof(*header));
   file = fopen(path, "rb");
   if (!file)
      return -1;
   got = fread(header, 1, sizeof(*header), file);
   fclose(file);
   if (got != sizeof(*header))
      return -1;
   header->core_id[sizeof(header->core_id) - 1u] = '\0';
   header->extensions[sizeof(header->extensions) - 1u] = '\0';
   return 0;
}

static int core_module_header_valid(const struct unifrog_core_module_header *h)
{
   return h && h->magic == UNIFROG_CORE_MODULE_MAGIC &&
      h->header_size >= sizeof(*h) &&
      h->format_version == UNIFROG_CORE_MODULE_FORMAT_VERSION &&
      h->endian_mark == UNIFROG_CORE_MODULE_ENDIAN_MARK &&
      h->core_id[0] &&
      h->file_end_addr > h->load_addr &&
      h->memory_end_addr >= h->file_end_addr &&
      h->bss_end_addr >= h->bss_addr;
}

static int core_module_header_compatible(
   const struct unifrog_core_module_header *h)
{
   const struct unifrog_abi *abi = unifrog_abi_get();
   size_t required_size = h && h->required_abi_size ?
      h->required_abi_size : UNIFROG_ABI_CORE_MIN_SIZE;

   return core_module_header_valid(h) &&
      unifrog_abi_table_compatible(abi, h->required_abi_version,
         required_size);
}

static void core_module_meta(char *dst, size_t dst_size,
   const struct unifrog_core_module_header *h)
{
   if (!dst || dst_size == 0)
      return;
   if (!core_module_header_valid(h)) {
      snprintf(dst, dst_size, "invalid");
      return;
   }
   snprintf(dst, dst_size, "%s abi %u.%u.%u",
      core_module_header_compatible(h) ? "ok" : "bad",
      (unsigned)UNIFROG_ABI_VERSION_GET_MAJOR(h->required_abi_version),
      (unsigned)UNIFROG_ABI_VERSION_GET_MINOR(h->required_abi_version),
      (unsigned)UNIFROG_ABI_VERSION_GET_PATCH(h->required_abi_version));
}

static void sanitize_slot_name(char *name)
{
   int wrote = 0;

   if (!name)
      return;
   for (char *p = name; *p; p++) {
      char c = *p;

      if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')
         name[wrote++] = c;
      else if (wrote > 0 && name[wrote - 1] != '-')
         name[wrote++] = '-';
   }
   while (wrote > 0 && (name[wrote - 1] == '-' || name[wrote - 1] == '.'))
      wrote--;
   name[wrote] = '\0';
   if (!name[0])
      unifrog_text_copy(name, 16, "package");
}

static int sd_relative_path(char *dst, size_t dst_size, const char *path)
{
   size_t root_len = strlen(FRONTEND_ROOT);

   if (!dst || dst_size == 0 || !path)
      return -1;
   dst[0] = '\0';
   if (strncmp(path, FRONTEND_ROOT, root_len) != 0)
      return -1;
   if (path[root_len] == '\0')
      return -1;
   if (path[root_len] != '/')
      return -1;
   unifrog_text_copy(dst, dst_size, path + root_len + 1u);
   return dst[0] ? 0 : -1;
}

static const char *frontend_rom_root(const struct native_frontend *fe)
{
   return fe && fe->rom_root[0] ? fe->rom_root : FRONTEND_ROMS_ROOT;
}

static const char *frontend_rom_root_label(const struct native_frontend *fe)
{
   return fe && fe->rom_root_label[0] ? fe->rom_root_label : "ROMs";
}

static int frontend_path_has_dir_prefix(const char *path, const char *root)
{
   size_t root_len;

   if (!path || !root)
      return 0;
   root_len = strlen(root);
   return strncmp(path, root, root_len) == 0 &&
      (path[root_len] == '\0' || path[root_len] == '/');
}

static int frontend_path_is_rom_root(const struct native_frontend *fe,
   const char *path)
{
   return path && strcmp(path, frontend_rom_root(fe)) == 0;
}

static int frontend_path_is_inside_rom_root(const struct native_frontend *fe,
   const char *path)
{
   return frontend_path_has_dir_prefix(path, frontend_rom_root(fe));
}

static const char *frontend_rom_title(const struct native_frontend *fe,
   const char *path)
{
   if (frontend_path_is_rom_root(fe, path))
      return frontend_rom_root_label(fe);
   return basename_const(path);
}

static uint16_t read_le16(const uint8_t *p)
{
   return (uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8);
}

static uint32_t read_le32(const uint8_t *p)
{
   return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
      ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int file_size_fp(FILE *file, size_t *out)
{
   long end;
   long pos;

   if (!file || !out)
      return -1;
   pos = ftell(file);
   if (pos < 0 || fseek(file, 0, SEEK_END) != 0)
      return -1;
   end = ftell(file);
   if (end < 0 || fseek(file, pos, SEEK_SET) != 0)
      return -1;
   *out = (size_t)end;
   return 0;
}

static int zip_entry_name_is_dir(const char *name)
{
   size_t len = name ? strlen(name) : 0;

   return len > 0 && name[len - 1u] == '/';
}

static int zip_name_safe(const char *name)
{
   if (!name || !name[0] || name[0] == '/')
      return 0;
   if (strstr(name, "..") || strchr(name, '\\') || strchr(name, ':'))
      return 0;
   return 1;
}

static const char *theme_archive_rel(const char *name)
{
   const char *slash;

   if (!name || !name[0])
      return name;
   slash = strchr(name, '/');
   if (!slash)
      return name;
   if (slash > name + 2) {
      const char *x = name;
      int left = 0;
      int right = 0;

      while (*x >= '0' && *x <= '9') {
         left = 1;
         x++;
      }
      if (left && *x == 'x') {
         x++;
         while (*x >= '0' && *x <= '9') {
            right = 1;
            x++;
         }
         if (right && x == slash)
            return slash + 1;
      }
   }
   return name;
}

static int theme_archive_resolution_rank(const char *name)
{
   static const char *const prefixes[] = {
      "320x240/", "640x480/", "720x480/", "720x576/", "720x720/",
      "1024x768/", "1280x720/",
   };

   if (!name)
      return 0;
   for (unsigned i = 0; i < ARRAY_SIZE(prefixes); i++) {
      if (strncmp(name, prefixes[i], strlen(prefixes[i])) == 0)
         return (int)i + 1;
   }
   return 0;
}

static int theme_archive_has_lower_duplicate(const struct frontend_zip_entry *entries,
   unsigned count, const char *name)
{
   const char *rel = theme_archive_rel(name);
   int rank = theme_archive_resolution_rank(name);

   if (!entries || !name || rel == name || rank == 0)
      return 0;
   for (unsigned i = 0; i < count; i++) {
      int other_rank;

      if (!entries[i].name[0] || strcmp(entries[i].name, name) == 0)
         continue;
      if (strcmp(theme_archive_rel(entries[i].name), rel) != 0)
         continue;
      other_rank = theme_archive_resolution_rank(entries[i].name);
      if (other_rank == 0 && theme_archive_rel(entries[i].name) !=
          entries[i].name)
         continue;
      if (other_rank == 0 || other_rank < rank)
         return 1;
   }
   return 0;
}

static int theme_archive_entry_needed(const struct frontend_zip_entry *entries,
   unsigned count, const char *name)
{
   const char *rel;

   if (!name || !name[0])
      return 0;
   if (zip_entry_name_is_dir(name))
      return 0;
   rel = theme_archive_rel(name);
   if (theme_archive_has_lower_duplicate(entries, count, name))
      return 0;
   if (strcmp(rel, "version.txt") == 0 ||
       strcmp(rel, "credits.txt") == 0 ||
       strcmp(rel, "preview.png") == 0)
      return 1;
   if (strncmp(rel, "scheme/", 7) == 0 || strncmp(rel, "font/", 5) == 0 ||
       strncmp(rel, "glyph/", 6) == 0)
      return 1;
   if (strncmp(rel, "image/", 6) == 0 &&
       unifrog_text_ends_with_ci(rel, ".png"))
      return 1;
   if (rel == name && strchr(rel, '/'))
      return 0;
   return 0;
}

static int theme_archive_stamp(char *stamp, size_t stamp_size,
   const char *archive_path)
{
   struct stat st;
   int wrote;

   if (!stamp || stamp_size == 0 || !archive_path ||
       stat(archive_path, &st) != 0)
      return -1;
   wrote = snprintf(stamp, stamp_size, "extractor=4\nsize=%lu\nmtime=%lu\n",
      (unsigned long)st.st_size, (unsigned long)st.st_mtime);
   return wrote > 0 && (size_t)wrote < stamp_size ? 0 : -1;
}

static int theme_archive_stamp_matches(const char *dest_root,
   const char *archive_path)
{
   char stamp_path[FRONTEND_MAX_PATH];
   char expected[80];
   char actual[80];
   FILE *file;
   size_t got;

   if (!dest_root || !archive_path ||
       path_join(stamp_path, sizeof(stamp_path), dest_root,
          ".unifrog-muxthm-stamp") != 0 ||
       theme_archive_stamp(expected, sizeof(expected), archive_path) != 0)
      return 0;
   file = fopen(stamp_path, "rb");
   if (!file)
      return 0;
   got = fread(actual, 1, sizeof(actual) - 1u, file);
   fclose(file);
   actual[got] = '\0';
   return strcmp(actual, expected) == 0;
}

static void theme_archive_write_stamp(const char *dest_root,
   const char *archive_path)
{
   char stamp_path[FRONTEND_MAX_PATH];
   char stamp[80];

   if (!dest_root || !archive_path ||
       path_join(stamp_path, sizeof(stamp_path), dest_root,
          ".unifrog-muxthm-stamp") != 0 ||
       theme_archive_stamp(stamp, sizeof(stamp), archive_path) != 0)
      return;
   (void)write_text_file(stamp_path, stamp);
}

static int zip_find_eocd(const uint8_t *zip, size_t zip_size,
   size_t *eocd_offset)
{
   size_t min_pos;
   size_t pos;

   if (!zip || !eocd_offset || zip_size < 22u)
      return -1;
   min_pos = zip_size > (0xffffu + 22u) ? zip_size - (0xffffu + 22u) : 0;
   pos = zip_size - 22u;
   for (;;) {
      if (read_le32(zip + pos) == 0x06054b50u) {
         uint16_t comment_len = read_le16(zip + pos + 20);

         if (pos + 22u + comment_len == zip_size) {
            *eocd_offset = pos;
            return 0;
         }
      }
      if (pos == min_pos)
         break;
      pos--;
   }
   return -1;
}

static int zip_locate_entry_data(FILE *file, size_t zip_size,
   const struct frontend_zip_entry *entry, size_t *data_offset)
{
   uint8_t local[30];
   uint16_t local_name_len;
   uint16_t local_extra_len;
   size_t offset;

   if (!file || !entry || !data_offset ||
       (size_t)entry->local_offset + sizeof(local) > zip_size)
      return -1;
   if (fseek(file, (long)entry->local_offset, SEEK_SET) != 0 ||
       fread(local, 1, sizeof(local), file) != sizeof(local) ||
       read_le32(local) != 0x04034b50u)
      return -1;
   if (read_le16(local + 8) != entry->method)
      return -1;
   local_name_len = read_le16(local + 26);
   local_extra_len = read_le16(local + 28);
   offset = (size_t)entry->local_offset + sizeof(local) +
      local_name_len + local_extra_len;
   if (offset > zip_size || entry->compressed_size > zip_size - offset)
      return -1;
   *data_offset = offset;
   return 0;
}

static int zip_copy_stored(FILE *in, FILE *out, size_t size)
{
   uint8_t buf[4096];
   size_t remaining = size;

   while (remaining > 0) {
      size_t chunk = remaining < sizeof(buf) ? remaining : sizeof(buf);

      if (fread(buf, 1, chunk, in) != chunk ||
          fwrite(buf, 1, chunk, out) != chunk)
         return -1;
      remaining -= chunk;
   }
   return 0;
}

static int zip_inflate_to_file(FILE *in, FILE *out, size_t compressed_size,
   size_t uncompressed_size)
{
   uint8_t in_buf[4096];
   uint8_t out_buf[4096];
   z_stream stream;
   size_t remaining = compressed_size;
   int zret;
   int ret = -1;

   memset(&stream, 0, sizeof(stream));
   zret = inflateInit2(&stream, -MAX_WBITS);
   if (zret != Z_OK)
      return -1;
   while (remaining > 0) {
      size_t chunk = remaining < sizeof(in_buf) ? remaining : sizeof(in_buf);

      if (fread(in_buf, 1, chunk, in) != chunk)
         goto out;
      remaining -= chunk;
      stream.next_in = in_buf;
      stream.avail_in = (uInt)chunk;
      do {
         stream.next_out = out_buf;
         stream.avail_out = sizeof(out_buf);
         zret = inflate(&stream, Z_NO_FLUSH);
         if (zret != Z_OK && zret != Z_STREAM_END)
            goto out;
         chunk = sizeof(out_buf) - stream.avail_out;
         if (chunk > 0 && fwrite(out_buf, 1, chunk, out) != chunk)
            goto out;
         if (zret == Z_STREAM_END)
            break;
      } while (stream.avail_in > 0 || stream.avail_out == 0);
   }
   while (zret != Z_STREAM_END) {
      size_t chunk;

      stream.next_out = out_buf;
      stream.avail_out = sizeof(out_buf);
      zret = inflate(&stream, Z_FINISH);
      if (zret != Z_OK && zret != Z_STREAM_END)
         goto out;
      chunk = sizeof(out_buf) - stream.avail_out;
      if (chunk > 0 && fwrite(out_buf, 1, chunk, out) != chunk)
         goto out;
      if (chunk == 0 && zret != Z_STREAM_END)
         goto out;
   }
   if (zret == Z_STREAM_END && stream.total_out == uncompressed_size)
      ret = 0;

out:
   (void)inflateEnd(&stream);
   return ret;
}

static int zip_inflate_to_file_memory(FILE *in, FILE *out,
   size_t compressed_size, size_t uncompressed_size)
{
   uint8_t *compressed = NULL;
   uint8_t *uncompressed = NULL;
   z_stream stream;
   int zret;
   int ret = -1;

   if (compressed_size == 0 || uncompressed_size == 0 ||
       compressed_size > 1024u * 1024u || uncompressed_size > 1024u * 1024u)
      return -1;
   compressed = malloc(compressed_size);
   uncompressed = malloc(uncompressed_size);
   if (!compressed || !uncompressed)
      goto out;
   if (fread(compressed, 1, compressed_size, in) != compressed_size)
      goto out;
   memset(&stream, 0, sizeof(stream));
   if (inflateInit2(&stream, -MAX_WBITS) != Z_OK)
      goto out;
   stream.next_in = compressed;
   stream.avail_in = (uInt)compressed_size;
   stream.next_out = uncompressed;
   stream.avail_out = (uInt)uncompressed_size;
   zret = inflate(&stream, Z_FINISH);
   if (zret == Z_STREAM_END &&
       stream.total_out == uncompressed_size &&
       fwrite(uncompressed, 1, uncompressed_size, out) == uncompressed_size)
      ret = 0;
   (void)inflateEnd(&stream);

out:
   free(uncompressed);
   free(compressed);
   return ret;
}

static int install_theme_archive(const char *archive_path, char *installed_name,
   size_t installed_name_size, frontend_progress_cb progress,
   void *progress_userdata)
{
   FILE *zip_file = NULL;
   uint8_t *tail = NULL;
   size_t zip_size;
   size_t tail_size;
   size_t tail_eocd;
   uint32_t cd_offset;
   uint32_t cd_size;
   uint16_t entries = 0;
   struct frontend_zip_entry *archive_entries = NULL;
   char theme_name[96] = "";
   char dest_root[FRONTEND_MAX_PATH] = "";
   const char *stage = "start";
   unsigned extracted = 0;
   unsigned skipped = 0;
   unsigned asset_errors = 0;
   int ret = -1;
   uint32_t start_ms = unifrog_perf_time_ms();
   uint32_t open_ms = 0;
   uint32_t tail_ms = 0;
   uint32_t scan_ms = 0;
   uint32_t extract_ms = 0;
   uint32_t t0;

   if (!archive_path || !archive_path[0]) {
      stage = "bad_archive_path";
      goto out;
   }
   unifrog_text_copy(theme_name, sizeof(theme_name), basename_const(archive_path));
   strip_known_suffix(theme_name, ".muxthm");
   strip_known_suffix(theme_name, ".zip");
   if (!theme_name[0]) {
      stage = "bad_theme_name";
      goto out;
   }
   if (path_join(dest_root, sizeof(dest_root), FRONTEND_THEME_ROOT, theme_name) != 0) {
      stage = "dest_path";
      goto out;
   }
   unifrog_log("frontend theme archive install begin path=%s name=%s dest=%s\n",
      archive_path, theme_name, dest_root);
   if (progress)
      progress(progress_userdata, "preparing", 0, 100);
   stage = "mkdir_dest";
   ensure_data_dirs();
   if (mkdir_p(dest_root) != 0) {
      unifrog_log("frontend theme archive mkdir failed dest=%s errno=%d\n",
         dest_root, errno);
      goto out;
   }
   if (theme_archive_stamp_matches(dest_root, archive_path)) {
      stage = "cached";
      if (installed_name && installed_name_size)
         unifrog_text_copy(installed_name, installed_name_size, theme_name);
      if (progress)
         progress(progress_userdata, "cached", 100, 100);
      ret = 0;
      goto out;
   }

   stage = "open";
   if (progress)
      progress(progress_userdata, "opening", 2, 100);
   t0 = unifrog_perf_time_ms();
   zip_file = fopen(archive_path, "rb");
   if (!zip_file || file_size_fp(zip_file, &zip_size) != 0 || zip_size < 22u)
      goto out;
   open_ms = unifrog_perf_time_ms() - t0;
   stage = "alloc_tail";
   t0 = unifrog_perf_time_ms();
   tail_size = zip_size > (0xffffu + 22u) ? (0xffffu + 22u) : zip_size;
   tail = malloc(tail_size);
   if (!tail)
      goto out;
   stage = "eocd";
   if (fseek(zip_file, (long)(zip_size - tail_size), SEEK_SET) != 0 ||
       fread(tail, 1, tail_size, zip_file) != tail_size ||
       zip_find_eocd(tail, tail_size, &tail_eocd) != 0)
      goto out;
   tail_ms = unifrog_perf_time_ms() - t0;
   entries = read_le16(tail + tail_eocd + 10);
   cd_size = read_le32(tail + tail_eocd + 12);
   cd_offset = read_le32(tail + tail_eocd + 16);
   if ((size_t)cd_offset + cd_size > zip_size ||
       fseek(zip_file, (long)cd_offset, SEEK_SET) != 0)
      goto out;

   stage = "central_directory";
   if (progress)
      progress(progress_userdata, "scanning", 10, 100);
   t0 = unifrog_perf_time_ms();
   archive_entries = calloc(entries ? entries : 1u, sizeof(*archive_entries));
   if (!archive_entries)
      goto out;
   for (uint16_t i = 0; i < entries; i++) {
      uint8_t fixed[46];
      struct frontend_zip_entry entry;
      uint16_t name_len;
      uint16_t extra_len;
      uint16_t comment_len;
      size_t copy_len;

      if (fread(fixed, 1, sizeof(fixed), zip_file) != sizeof(fixed) ||
          read_le32(fixed) != 0x02014b50u)
         goto out;
      memset(&entry, 0, sizeof(entry));
      entry.flags = read_le16(fixed + 8);
      entry.method = read_le16(fixed + 10);
      entry.compressed_size = read_le32(fixed + 20);
      entry.uncompressed_size = read_le32(fixed + 24);
      entry.local_offset = read_le32(fixed + 42);
      name_len = read_le16(fixed + 28);
      extra_len = read_le16(fixed + 30);
      comment_len = read_le16(fixed + 32);
      copy_len = name_len;
      if (copy_len >= sizeof(entry.name))
         copy_len = sizeof(entry.name) - 1u;
      if (fread(entry.name, 1, copy_len, zip_file) != copy_len)
         goto out;
      entry.name[copy_len] = '\0';
      if ((size_t)name_len > copy_len &&
          fseek(zip_file, (long)((size_t)name_len - copy_len), SEEK_CUR) != 0)
         goto out;
      if (fseek(zip_file, (long)((size_t)extra_len + comment_len), SEEK_CUR) != 0)
         goto out;
      if ((entry.flags & 1u) || !zip_name_safe(entry.name) ||
          (entry.method != 0 && entry.method != 8))
         continue;
      archive_entries[i] = entry;
      if (progress && ((i & 15u) == 0 || i + 1u == entries))
         progress(progress_userdata, "scanning",
            10u + (entries ? ((unsigned)(i + 1u) * 30u / entries) : 30u),
            100);
   }
   if (remove_tree(dest_root) != 0 && errno != ENOENT)
      unifrog_log("frontend theme archive cleanup warning dest=%s errno=%d\n",
         dest_root, errno);
   if (mkdir_p(dest_root) != 0)
      goto out;
   for (uint16_t i = 0; i < entries; i++) {
      struct frontend_zip_entry *entry = &archive_entries[i];
      char out_path[FRONTEND_MAX_PATH];
      size_t data_offset;
      FILE *out_file;
      int copy_ret = -1;
      uint32_t extract_t0;

      if (!entry->name[0])
         continue;
      if (progress && ((i & 7u) == 0 || i + 1u == entries))
         progress(progress_userdata, "extracting",
            40u + (entries ? ((unsigned)(i + 1u) * 58u / entries) : 58u),
            100);
      if (!theme_archive_entry_needed(archive_entries, entries, entry->name)) {
         skipped++;
         continue;
      }
      if (path_join(out_path, sizeof(out_path), dest_root, entry->name) != 0)
         goto out;
      if (zip_entry_name_is_dir(entry->name)) {
         if (mkdir_p(out_path) != 0)
            goto out;
         continue;
      }
      if (zip_locate_entry_data(zip_file, zip_size, entry, &data_offset) != 0 ||
          ensure_parent_dir(out_path) != 0 ||
          fseek(zip_file, (long)data_offset, SEEK_SET) != 0) {
         asset_errors++;
         unifrog_log("frontend theme archive entry skipped name=%s reason=locate errno=%d\n",
            entry->name, errno);
         stage = "central_directory";
         continue;
      }
      stage = entry->name;
      out_file = fopen(out_path, "wb");
      extract_t0 = unifrog_perf_time_ms();
      if (out_file) {
         if (entry->method == 0)
            copy_ret = zip_copy_stored(zip_file, out_file,
               entry->uncompressed_size);
         else {
            long data_pos = ftell(zip_file);

            copy_ret = zip_inflate_to_file_memory(zip_file, out_file,
               entry->compressed_size, entry->uncompressed_size);
            if (copy_ret != 0 && data_pos >= 0 &&
                fseek(zip_file, data_pos, SEEK_SET) == 0)
               copy_ret = zip_inflate_to_file(zip_file, out_file,
                  entry->compressed_size, entry->uncompressed_size);
         }
         if (fclose(out_file) != 0)
            copy_ret = -1;
      }
      if (copy_ret != 0) {
         asset_errors++;
         (void)unlink(out_path);
         unifrog_log("frontend theme archive entry skipped name=%s method=%u comp=%u uncomp=%u errno=%d\n",
            entry->name, entry->method, entry->compressed_size,
            entry->uncompressed_size, errno);
      } else {
         extracted++;
         extract_ms += unifrog_perf_time_ms() - extract_t0;
      }
      if (copy_ret != 0) {
         stage = "central_directory";
         continue;
      }
      stage = "central_directory";
   }
   scan_ms = unifrog_perf_time_ms() - t0;
   if (extracted == 0) {
      stage = "no_assets";
      goto out;
   }
   if (installed_name && installed_name_size)
      unifrog_text_copy(installed_name, installed_name_size, theme_name);
   theme_archive_write_stamp(dest_root, archive_path);
   ret = 0;

out:
   if (progress)
      progress(progress_userdata, ret == 0 ? "done" : stage, 100, 100);
   free(archive_entries);
   free(tail);
   if (zip_file)
      fclose(zip_file);
   unifrog_log("frontend theme archive install path=%s name=%s ret=%d stage=%s extracted=%u skipped=%u errors=%u entries=%u ms=%u open_ms=%u tail_ms=%u scan_ms=%u extract_ms=%u\n",
      archive_path, theme_name, ret, stage, extracted, skipped, asset_errors,
      entries, (unsigned)(unifrog_perf_time_ms() - start_ms),
      (unsigned)open_ms, (unsigned)tail_ms, (unsigned)scan_ms,
      (unsigned)extract_ms);
   return ret;
}

static int package_entry_needed(const char *name)
{
   const char *rel = name;

   if (!name || !name[0] || zip_entry_name_is_dir(name))
      return 0;
   if (strncmp(rel, "./", 2) == 0)
      rel += 2;
   return strncmp(rel, "bios/", 5) == 0 ||
      strncmp(rel, "unifrog/", 8) == 0;
}

static const char *package_entry_rel(const char *name)
{
   if (!name)
      return "";
   if (strncmp(name, "./", 2) == 0)
      return name + 2;
   return name;
}

static int install_update_archive(const char *archive_path, char *slot_name,
   size_t slot_name_size)
{
   FILE *zip_file = NULL;
   uint8_t *tail = NULL;
   size_t zip_size;
   size_t tail_size;
   size_t tail_eocd;
   uint32_t cd_offset;
   uint32_t cd_size;
   uint16_t entries = 0;
   struct frontend_zip_entry *archive_entries = NULL;
   char slot[64] = "";
   char dest_root[FRONTEND_MAX_PATH] = "";
   char check_path[FRONTEND_MAX_PATH];
   const char *stage = "start";
   unsigned extracted = 0;
   unsigned skipped = 0;
   unsigned errors = 0;
   int ret = -1;
   uint32_t start_ms = unifrog_perf_time_ms();

   if (!archive_path || !archive_path[0])
      goto out;
   unifrog_text_copy(slot, sizeof(slot), basename_const(archive_path));
   strip_known_suffix(slot, ".zip");
   sanitize_slot_name(slot);
   if (!slot[0])
      goto out;
   if (path_join(dest_root, sizeof(dest_root), FRONTEND_VERSION_ROOT, slot) != 0)
      goto out;
   unifrog_log("frontend update install begin archive=%s slot=%s dest=%s\n",
      archive_path, slot, dest_root);
   ensure_data_dirs();
   if (mkdir_p(FRONTEND_VERSION_ROOT) != 0)
      goto out;
   if (remove_tree_under(dest_root, FRONTEND_VERSION_ROOT) != 0 &&
       errno != ENOENT)
      unifrog_log("frontend update cleanup warning dest=%s errno=%d\n",
         dest_root, errno);
   if (mkdir_p(dest_root) != 0)
      goto out;

   stage = "open";
   zip_file = fopen(archive_path, "rb");
   if (!zip_file || file_size_fp(zip_file, &zip_size) != 0 || zip_size < 22u)
      goto out;
   stage = "eocd";
   tail_size = zip_size > (0xffffu + 22u) ? (0xffffu + 22u) : zip_size;
   tail = malloc(tail_size);
   if (!tail)
      goto out;
   if (fseek(zip_file, (long)(zip_size - tail_size), SEEK_SET) != 0 ||
       fread(tail, 1, tail_size, zip_file) != tail_size ||
       zip_find_eocd(tail, tail_size, &tail_eocd) != 0)
      goto out;
   entries = read_le16(tail + tail_eocd + 10);
   cd_size = read_le32(tail + tail_eocd + 12);
   cd_offset = read_le32(tail + tail_eocd + 16);
   if ((size_t)cd_offset + cd_size > zip_size ||
       fseek(zip_file, (long)cd_offset, SEEK_SET) != 0)
      goto out;

   stage = "central_directory";
   archive_entries = calloc(entries ? entries : 1u, sizeof(*archive_entries));
   if (!archive_entries)
      goto out;
   for (uint16_t i = 0; i < entries; i++) {
      uint8_t fixed[46];
      struct frontend_zip_entry entry;
      uint16_t name_len;
      uint16_t extra_len;
      uint16_t comment_len;
      size_t copy_len;

      if (fread(fixed, 1, sizeof(fixed), zip_file) != sizeof(fixed) ||
          read_le32(fixed) != 0x02014b50u)
         goto out;
      memset(&entry, 0, sizeof(entry));
      entry.flags = read_le16(fixed + 8);
      entry.method = read_le16(fixed + 10);
      entry.compressed_size = read_le32(fixed + 20);
      entry.uncompressed_size = read_le32(fixed + 24);
      entry.local_offset = read_le32(fixed + 42);
      name_len = read_le16(fixed + 28);
      extra_len = read_le16(fixed + 30);
      comment_len = read_le16(fixed + 32);
      copy_len = name_len;
      if (copy_len >= sizeof(entry.name))
         copy_len = sizeof(entry.name) - 1u;
      if (fread(entry.name, 1, copy_len, zip_file) != copy_len)
         goto out;
      entry.name[copy_len] = '\0';
      if ((size_t)name_len > copy_len &&
          fseek(zip_file, (long)((size_t)name_len - copy_len), SEEK_CUR) != 0)
         goto out;
      if (fseek(zip_file, (long)((size_t)extra_len + comment_len), SEEK_CUR) != 0)
         goto out;
      if ((entry.flags & 1u) || !zip_name_safe(entry.name) ||
          (entry.method != 0 && entry.method != 8))
         continue;
      archive_entries[i] = entry;
   }

   stage = "extract";
   for (uint16_t i = 0; i < entries; i++) {
      struct frontend_zip_entry *entry = &archive_entries[i];
      char out_path[FRONTEND_MAX_PATH];
      size_t data_offset;
      FILE *out_file;
      int copy_ret = -1;

      if (!entry->name[0])
         continue;
      if (!package_entry_needed(entry->name)) {
         skipped++;
         continue;
      }
      if (path_join(out_path, sizeof(out_path), dest_root,
          package_entry_rel(entry->name)) != 0)
         goto out;
      if (zip_locate_entry_data(zip_file, zip_size, entry, &data_offset) != 0 ||
          ensure_parent_dir(out_path) != 0 ||
          fseek(zip_file, (long)data_offset, SEEK_SET) != 0) {
         errors++;
         continue;
      }
      out_file = fopen(out_path, "wb");
      if (out_file) {
         if (entry->method == 0)
            copy_ret = zip_copy_stored(zip_file, out_file,
               entry->uncompressed_size);
         else
            copy_ret = zip_inflate_to_file(zip_file, out_file,
               entry->compressed_size, entry->uncompressed_size);
         if (fclose(out_file) != 0)
            copy_ret = -1;
      }
      if (copy_ret == 0)
         extracted++;
      else {
         errors++;
         (void)unlink(out_path);
      }
   }

   if (path_join(check_path, sizeof(check_path), dest_root,
       "unifrog/firmware/unifrog.bin") != 0 || !file_exists(check_path))
      goto out;
   if (path_join(check_path, sizeof(check_path), dest_root,
       "bios/bisrv.asd") != 0 || !file_exists(check_path))
      goto out;
   if (path_join(check_path, sizeof(check_path), dest_root,
       "unifrog/manifest.ini") != 0 || !file_exists(check_path))
      goto out;
   if (slot_name && slot_name_size)
      unifrog_text_copy(slot_name, slot_name_size, slot);
   ret = 0;

out:
   free(archive_entries);
   free(tail);
   if (zip_file)
      fclose(zip_file);
   unifrog_log("frontend update install archive=%s slot=%s ret=%d stage=%s extracted=%u skipped=%u errors=%u entries=%u ms=%u\n",
      archive_path ? archive_path : "", slot, ret, stage, extracted, skipped,
      errors, entries, (unsigned)(unifrog_perf_time_ms() - start_ms));
   return ret;
}

static int validate_update_archive(const char *archive_path, char *summary,
   size_t summary_size)
{
   FILE *zip_file = NULL;
   uint8_t *tail = NULL;
   size_t zip_size;
   size_t tail_size;
   size_t tail_eocd;
   uint32_t cd_offset;
   uint32_t cd_size;
   uint16_t entries = 0;
   unsigned has_bios = 0;
   unsigned has_fw = 0;
   unsigned has_manifest = 0;
   unsigned dist_entries = 0;
   unsigned user_payload = 0;
   unsigned bad = 0;
   int ret = -1;

   if (summary && summary_size)
      summary[0] = '\0';
   if (!archive_path)
      goto out;
   zip_file = fopen(archive_path, "rb");
   if (!zip_file || file_size_fp(zip_file, &zip_size) != 0 || zip_size < 22u)
      goto out;
   tail_size = zip_size > (0xffffu + 22u) ? (0xffffu + 22u) : zip_size;
   tail = malloc(tail_size);
   if (!tail)
      goto out;
   if (fseek(zip_file, (long)(zip_size - tail_size), SEEK_SET) != 0 ||
       fread(tail, 1, tail_size, zip_file) != tail_size ||
       zip_find_eocd(tail, tail_size, &tail_eocd) != 0)
      goto out;
   entries = read_le16(tail + tail_eocd + 10);
   cd_size = read_le32(tail + tail_eocd + 12);
   cd_offset = read_le32(tail + tail_eocd + 16);
   if ((size_t)cd_offset + cd_size > zip_size ||
       fseek(zip_file, (long)cd_offset, SEEK_SET) != 0)
      goto out;

   for (uint16_t i = 0; i < entries; i++) {
      uint8_t fixed[46];
      char name[FRONTEND_MAX_PATH];
      const char *rel;
      uint16_t name_len;
      uint16_t extra_len;
      uint16_t comment_len;
      size_t copy_len;

      if (fread(fixed, 1, sizeof(fixed), zip_file) != sizeof(fixed) ||
          read_le32(fixed) != 0x02014b50u)
         goto out;
      name_len = read_le16(fixed + 28);
      extra_len = read_le16(fixed + 30);
      comment_len = read_le16(fixed + 32);
      copy_len = name_len;
      if (copy_len >= sizeof(name))
         copy_len = sizeof(name) - 1u;
      if (fread(name, 1, copy_len, zip_file) != copy_len)
         goto out;
      name[copy_len] = '\0';
      if ((size_t)name_len > copy_len &&
          fseek(zip_file, (long)((size_t)name_len - copy_len), SEEK_CUR) != 0)
         goto out;
      if (fseek(zip_file, (long)((size_t)extra_len + comment_len), SEEK_CUR) != 0)
         goto out;
      if (!zip_name_safe(name)) {
         bad++;
         continue;
      }
      rel = package_entry_rel(name);
      if (strcmp(rel, "bios/bisrv.asd") == 0)
         has_bios = 1;
      else if (strcmp(rel, "unifrog/firmware/unifrog.bin") == 0)
         has_fw = 1;
      else if (strcmp(rel, "unifrog/manifest.ini") == 0)
         has_manifest = 1;
      else if (strncmp(rel, "unifrog/", 8) == 0)
         dist_entries++;
      else if (strncmp(rel, "unifrog_data/", 13) == 0 &&
               !zip_entry_name_is_dir(rel))
         user_payload++;
   }
   ret = has_bios && has_fw && has_manifest && bad == 0 &&
      user_payload == 0 ? 0 : -1;

out:
   if (summary && summary_size)
      snprintf(summary, summary_size, "%s entries=%u bios=%u fw=%u manifest=%u dist=%u data=%u bad=%u",
         ret == 0 ? "ok" : "bad", entries, has_bios, has_fw, has_manifest,
         dist_entries, user_payload, bad);
   free(tail);
   if (zip_file)
      fclose(zip_file);
   unifrog_log("frontend package zip_check path=%s ret=%d summary=%s\n",
      archive_path ? archive_path : "", ret, summary ? summary : "");
   return ret;
}

static void mark_boot_ok(void)
{
   char text[160];

   snprintf(text, sizeof(text), "commit=%s\npending=%s\n",
      UNIFROG_GIT_COMMIT,
      file_exists(UNIFROG_PENDING_VERSION_PATH) ? "yes" : "no");
   (void)write_text_file(UNIFROG_BOOT_OK_PATH, text);
   if (file_exists(UNIFROG_PENDING_VERSION_PATH))
      unifrog_log("frontend update boot_ok with pending marker present\n");
}

static int activate_installed_version(const char *slot)
{
   char root[FRONTEND_MAX_PATH];
   char src[FRONTEND_MAX_PATH];
   char dst[FRONTEND_MAX_PATH];
   char marker[160];
   int ret = -1;

   if (!slot || !slot[0] ||
       path_join(root, sizeof(root), FRONTEND_VERSION_ROOT, slot) != 0)
      return -1;
   unifrog_log("frontend update activate begin slot=%s root=%s\n", slot, root);
   snprintf(marker, sizeof(marker), "slot=%s\nstage=pending\napplied_by=%s\n",
      slot, UNIFROG_GIT_COMMIT);
   (void)write_text_file(UNIFROG_PENDING_VERSION_PATH, marker);
   (void)unlink(UNIFROG_BOOT_OK_PATH);
   if (path_join(src, sizeof(src), root, "unifrog/firmware/unifrog.bin") != 0 ||
       copy_file_path(src, FRONTEND_DIST_ROOT "/firmware/unifrog.bin") != 0)
      goto out;
   if (path_join(src, sizeof(src), root, "bios/bisrv.asd") != 0 ||
       copy_file_path(src, FRONTEND_ROOT "/bios/bisrv.asd") != 0)
      goto out;
   (void)remove_tree_under(FRONTEND_DIST_ROOT "/cores", FRONTEND_DIST_ROOT);
   (void)remove_tree_under(FRONTEND_DIST_ROOT "/modules", FRONTEND_DIST_ROOT);
   if (path_join(src, sizeof(src), root, "unifrog") != 0 ||
       copy_tree_merge(src, FRONTEND_DIST_ROOT, NULL) != 0)
      goto out;
   snprintf(marker, sizeof(marker), "slot=%s\nstage=active\napplied_by=%s\n",
      slot, UNIFROG_GIT_COMMIT);
   (void)write_text_file(FRONTEND_ACTIVE_VERSION_PATH, marker);
   (void)unlink(UNIFROG_PENDING_VERSION_PATH);
   if (path_join(dst, sizeof(dst), root, ".active") == 0)
      (void)write_text_file(dst, marker);
   ret = 0;

out:
   unifrog_log("frontend update activate slot=%s ret=%d\n", slot, ret);
   return ret;
}

static const struct frontend_catalog *catalog_for_core(const char *core)
{
   if (!core || !core[0])
      return NULL;
   for (unsigned i = 0; i < ARRAY_SIZE(frontend_catalog); i++) {
      if (strcmp(core, frontend_catalog[i].core) == 0)
         return &frontend_catalog[i];
   }
   return NULL;
}

static const char *safe_core_for_path(const char *path, const char *core)
{
   const struct frontend_catalog *cat = catalog_for_core(core);

   if (cat)
      return cat->core;
   cat = catalog_for_path(path);
   return cat ? cat->core : "";
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
   if (count >= 16u)
      return count;
   unifrog_text_copy(ids[count], UNIFROG_CORE_MODULE_ID_MAX, core);
   return count + 1u;
}

static unsigned collect_core_candidates(const char *path,
   char ids[][UNIFROG_CORE_MODULE_ID_MAX])
{
   const struct frontend_catalog *cat = catalog_for_path(path);
   unsigned count = 0;

   if (cat)
      count = add_core_candidate(ids, count, cat->core);
   if (!count && is_zip_file(path)) {
      for (unsigned i = 0; i < ARRAY_SIZE(frontend_catalog); i++)
         count = add_core_candidate(ids, count, frontend_catalog[i].core);
   }
   return count;
}

static int is_content_file(const char *path)
{
   return catalog_for_path(path) != NULL || is_media_file(path);
}

static const char *frameskip_label(int frameskip)
{
   switch (frameskip) {
   case UNIFROG_LIBRETRO_FRAMESKIP_OFF:
      return "off";
   case UNIFROG_LIBRETRO_FRAMESKIP_AUTO:
      return "auto";
   case UNIFROG_LIBRETRO_FRAMESKIP_FIXED_1:
      return "fixed 1";
   case UNIFROG_LIBRETRO_FRAMESKIP_FIXED_2:
      return "fixed 2";
   default:
      return "unknown";
   }
}

static const char *display_label(int display_mode)
{
   switch (display_mode) {
   case UNIFROG_LIBRETRO_DISPLAY_FIT:
      return "fit";
   case UNIFROG_LIBRETRO_DISPLAY_STRETCH:
      return "stretch";
   case UNIFROG_LIBRETRO_DISPLAY_ORIGINAL:
      return "original";
   default:
      return "unknown";
   }
}

static const char *input_profile_label(int profile)
{
   switch (profile) {
   case UNIFROG_LIBRETRO_INPUT_DEFAULT:
      return "default";
   case UNIFROG_LIBRETRO_INPUT_RETROARCH:
      return "retroarch";
   case UNIFROG_LIBRETRO_INPUT_GENESIS:
      return "genesis";
   case UNIFROG_LIBRETRO_INPUT_SWAP_AB:
      return "swap A/B";
   case UNIFROG_LIBRETRO_INPUT_SWAP_XY:
      return "swap X/Y";
   default:
      return "unknown";
   }
}

static unsigned clamp_state_slot(unsigned slot)
{
   return slot < 10u ? slot : 0u;
}

static const char *state_slot_label(unsigned slot)
{
   static char label[16];

   snprintf(label, sizeof(label), "slot %u", clamp_state_slot(slot));
   return label;
}

static const char *ge_clock_label(int ge_clock)
{
   switch (ge_clock) {
   case -1:
      return "auto";
   case 0:
      return "198 MHz";
   case 1:
      return "148 MHz";
   case 2:
      return "225 MHz";
   case 3:
      return "238 MHz";
   default:
      return "custom";
   }
}

static const char *language_label(int index)
{
   static const char *const labels[] = {
      "english", "espanol", "francais", "deutsch", "italiano",
   };

   if (index < 0)
      index = 0;
   return labels[(unsigned)index % ARRAY_SIZE(labels)];
}

static const char *active_language_label(struct native_frontend *fe)
{
   if (fe && fe->language_name[0])
      return fe->language_name;
   return language_label(fe ? fe->language_index : 0);
}

static const char *active_theme_label(struct native_frontend *fe)
{
   if (fe && fe->theme_name[0])
      return fe->theme_name;
   return "muos";
}

static const char *tr(struct native_frontend *fe, const char *key)
{
   if (!fe || !key)
      return key ? key : "";
   for (unsigned i = 0; i < fe->i18n_count; i++) {
      if (strcmp(fe->i18n_key[i], key) == 0)
         return fe->i18n_value[i];
   }
   return key;
}

static void load_language(struct native_frontend *fe)
{
   FILE *file;
   char path[FRONTEND_MAX_PATH];
   char line[FRONTEND_MAX_LINE];
   unsigned lines = 0;

   if (!fe)
      return;
   fe->i18n_count = 0;
   if (!fe->language_name[0] || strcmp(fe->language_name, "english") == 0)
      return;
   if (path_join_ini(path, sizeof(path), FRONTEND_LANGUAGE_ROOT,
       fe->language_name) != 0)
      return;
   file = fopen(path, "rb");
   if (!file) {
      unifrog_log("frontend language open failed name=%s path=%s errno=%d\n",
         fe->language_name, path, errno);
      return;
   }
   while (fgets(line, sizeof(line), file) && fe->i18n_count < FRONTEND_I18N_MAX) {
      char *sep;

      lines++;
      strip_eol(line);
      if (!line[0] || line[0] == '#')
         continue;
      sep = strchr(line, '=');
      if (!sep || sep == line || !sep[1])
         continue;
      *sep++ = '\0';
      unifrog_text_copy(fe->i18n_key[fe->i18n_count],
         sizeof(fe->i18n_key[0]), line);
      unifrog_text_copy(fe->i18n_value[fe->i18n_count],
         sizeof(fe->i18n_value[0]), sep);
      fe->i18n_count++;
   }
   fclose(file);
   unifrog_log("frontend language loaded name=%s entries=%u\n",
      fe->language_name, fe->i18n_count);
}

static const char *lvgl_screen_module(enum unifrog_frontend_lvgl_screen screen)
{
   switch (screen) {
   case UNIFROG_FRONTEND_LVGL_LAUNCH:
      return "muxlaunch";
   case UNIFROG_FRONTEND_LVGL_CONFIG:
      return "muxconfig";
   case UNIFROG_FRONTEND_LVGL_CONNECT:
      return "muxconnect";
   case UNIFROG_FRONTEND_LVGL_CUSTOM:
      return "muxcustom";
   case UNIFROG_FRONTEND_LVGL_INFO:
      return "muxinfo";
   case UNIFROG_FRONTEND_LVGL_POWER:
      return "muxpower";
   case UNIFROG_FRONTEND_LVGL_STORAGE:
      return "muxstorage";
   case UNIFROG_FRONTEND_LVGL_SYSINFO:
      return "muxsysinfo";
   case UNIFROG_FRONTEND_LVGL_VISUAL:
      return "muxvisual";
   default:
      return "muxplore";
   }
}

static const char *list_view_glyph_module(enum frontend_view view)
{
   switch (view) {
   case FRONTEND_VIEW_APPS:
      return "muxapp";
   case FRONTEND_VIEW_LAUNCH_SETTINGS:
      return "muxtweakgen";
   case FRONTEND_VIEW_THEME:
      return "muxtheme";
   case FRONTEND_VIEW_LANGUAGE:
      return "muxlanguage";
   case FRONTEND_VIEW_HISTORY:
      return "muxhistory";
   case FRONTEND_VIEW_FAVORITES:
      return "muxcollect";
   case FRONTEND_VIEW_FIRMWARE:
      return "muxarchive";
   case FRONTEND_VIEW_STORAGE_MODE:
   case FRONTEND_VIEW_STORAGE_CONFIRM:
      return "muxstorage";
   case FRONTEND_VIEW_EXPLORE:
      return "muxplore";
   default:
      return "muxplore";
   }
}

static int scheme_set_color(struct unifrog_ui_theme *theme,
   struct unifrog_frontend_lvgl_style *style, const char *section,
   const char *key, const char *value)
{
   uint16_t color;

   if (!theme || !style || !section || !key || !value)
      return 0;
   if (strstr(key, "_ALPHA")) {
      uint8_t alpha = parse_alpha(value, 255);

      if (strcmp(section, "background") == 0 &&
          strcmp(key, "BACKGROUND_ALPHA") == 0)
         style->background_alpha = alpha;
      else if (strcmp(section, "header") == 0 &&
               strcmp(key, "HEADER_BACKGROUND_ALPHA") == 0)
         style->header_alpha = alpha;
      else if (strcmp(section, "header") == 0 &&
               strcmp(key, "HEADER_TEXT_ALPHA") == 0)
         style->header_text_alpha = alpha;
      else if (strcmp(section, "footer") == 0 &&
               strcmp(key, "FOOTER_BACKGROUND_ALPHA") == 0)
         style->footer_alpha = alpha;
      else if (strcmp(section, "footer") == 0 &&
               strcmp(key, "FOOTER_TEXT_ALPHA") == 0)
         style->footer_text_alpha = alpha;
      else if (strcmp(section, "list") == 0 &&
               strcmp(key, "LIST_DEFAULT_BACKGROUND_ALPHA") == 0)
         style->list_alpha = alpha;
      else if (strcmp(section, "list") == 0 &&
               strcmp(key, "LIST_DEFAULT_TEXT_ALPHA") == 0)
         style->list_text_alpha = alpha;
      else if (strcmp(section, "list") == 0 &&
               strcmp(key, "LIST_DEFAULT_INDICATOR_ALPHA") == 0)
         style->list_indicator_alpha = alpha;
      else if (strcmp(section, "list") == 0 &&
               strcmp(key, "LIST_DEFAULT_GLYPH_ALPHA") == 0)
         style->list_glyph_alpha = alpha;
      else if (strcmp(section, "list") == 0 &&
               strcmp(key, "LIST_FOCUS_BACKGROUND_ALPHA") == 0)
         style->list_focus_alpha = alpha;
      else if (strcmp(section, "list") == 0 &&
               strcmp(key, "LIST_FOCUS_TEXT_ALPHA") == 0)
         style->list_focus_text_alpha = alpha;
      else if (strcmp(section, "list") == 0 &&
               strcmp(key, "LIST_FOCUS_INDICATOR_ALPHA") == 0)
         style->list_focus_indicator_alpha = alpha;
      else if (strcmp(section, "list") == 0 &&
               strcmp(key, "LIST_FOCUS_GLYPH_ALPHA") == 0)
         style->list_focus_glyph_alpha = alpha;
      return 0;
   }
   if (parse_theme_hex(value, &color) != 0)
      return 0;
   if (strcmp(section, "background") == 0) {
      if (strcmp(key, "BACKGROUND") == 0) {
         theme->background = color;
         style->background = color;
      } else if (strcmp(key, "BACKGROUND_GRADIENT_COLOR") == 0) {
         theme->panel = color;
      }
   } else if (strcmp(section, "header") == 0) {
      if (strcmp(key, "HEADER_BACKGROUND") == 0) {
         theme->panel = color;
         style->header_background = color;
      } else if (strcmp(key, "HEADER_TEXT") == 0) {
         theme->text = color;
         style->header_text = color;
      }
   } else if (strcmp(section, "footer") == 0) {
      if (strcmp(key, "FOOTER_BACKGROUND") == 0) {
         theme->panel = color;
         style->footer_background = color;
      } else if (strcmp(key, "FOOTER_TEXT") == 0) {
         theme->muted = color;
         style->footer_text = color;
      }
   } else if (strcmp(section, "list") == 0) {
      if (strcmp(key, "LIST_DEFAULT_BACKGROUND") == 0) {
         theme->panel = color;
         style->list_background = color;
      } else if (strcmp(key, "LIST_DEFAULT_TEXT") == 0) {
         theme->text = color;
         style->list_text = color;
      } else if (strcmp(key, "LIST_DEFAULT_INDICATOR") == 0) {
         theme->muted = color;
         style->list_indicator = color;
      } else if (strcmp(key, "LIST_FOCUS_BACKGROUND") == 0) {
         theme->focus = color;
         style->list_focus_background = color;
      } else if (strcmp(key, "LIST_FOCUS_TEXT") == 0) {
         theme->accent = color;
         style->list_focus_text = color;
      } else if (strcmp(key, "LIST_FOCUS_INDICATOR") == 0) {
         theme->accent = color;
         style->list_focus_indicator = color;
      }
   } else if (strcmp(section, "notification") == 0) {
      if (strcmp(key, "MSG_BACKGROUND") == 0)
         theme->danger = color;
   }
   return 0;
}

static int scheme_set_layout(struct unifrog_frontend_lvgl_style *style,
   const char *section, const char *key, const char *value)
{
   int v;

   if (!style || !section || !key || !value)
      return 0;
   v = parse_int(value, -32768);
   if (v == -32768)
      return 0;
   if (strcmp(section, "header") == 0 &&
       strcmp(key, "HEADER_HEIGHT") == 0)
      style->header_height = v;
   else if (strcmp(section, "footer") == 0 &&
            strcmp(key, "FOOTER_HEIGHT") == 0)
      style->footer_height = v;
   else if (strcmp(section, "list") == 0 &&
            strcmp(key, "LIST_DEFAULT_RADIUS") == 0)
      style->list_radius = v;
   else if (strcmp(section, "list") == 0 &&
            strcmp(key, "LIST_FOCUS_RADIUS") == 0)
      style->list_radius = v;
   else if (strcmp(section, "list") == 0 &&
            strcmp(key, "LIST_DEFAULT_BORDER_WIDTH") == 0)
      style->list_border_width = v;
   else if (strcmp(section, "misc") == 0 &&
            strcmp(key, "NAVIGATION_TYPE") == 0)
      style->navigation_type = v;
   else if (strcmp(section, "misc") == 0 &&
            strcmp(key, "CONTENT_ITEM_HEIGHT") == 0) {
      if (v > 0)
         style->list_h = v;
   } else if (strcmp(section, "misc") == 0 &&
            strcmp(key, "CONTENT_PADDING_LEFT") == 0)
      style->list_x = v;
   else if (strcmp(section, "misc") == 0 &&
            strcmp(key, "CONTENT_PADDING_TOP") == 0)
      style->list_y = v;
   else if (strcmp(section, "misc") == 0 &&
            strcmp(key, "CONTENT_WIDTH") == 0) {
      if (v > 0)
         style->list_w = v;
   } else if (strcmp(section, "grid") == 0 &&
            strcmp(key, "COLUMN_COUNT") == 0) {
      style->grid_column_count = v;
      style->launch_cols = v > 0 ? v : style->launch_cols;
      style->grid_enabled = style->grid_column_count > 0 &&
         style->grid_row_count > 0;
   } else if (strcmp(section, "grid") == 0 &&
            strcmp(key, "ROW_COUNT") == 0) {
      style->grid_row_count = v;
      style->grid_enabled = style->grid_column_count > 0 &&
         style->grid_row_count > 0;
   } else if (strcmp(section, "grid") == 0 &&
            strcmp(key, "LOCATION_X") == 0)
      style->launch_x = v;
   else if (strcmp(section, "grid") == 0 &&
            strcmp(key, "LOCATION_Y") == 0)
      style->launch_y = v;
   else if (strcmp(section, "grid") == 0 &&
            strcmp(key, "COLUMN_PADDING") == 0)
      style->launch_gap_x = v;
   else if (strcmp(section, "grid") == 0 &&
            strcmp(key, "ROW_PADDING") == 0)
      style->launch_gap_y = v;
   else if (strcmp(section, "grid") == 0 &&
            strcmp(key, "NAVIGATION_TYPE") == 0)
      style->navigation_type = v;
   else if (strcmp(section, "navigation") == 0 &&
            strcmp(key, "ALIGNMENT") == 0)
      style->navigation_type = v;
   return 0;
}

static int load_muos_scheme_file(const char *path,
   struct unifrog_ui_theme *theme, struct unifrog_frontend_lvgl_style *style)
{
   FILE *file;
   char line[FRONTEND_MAX_LINE];
   char section[48] = "";
   uint32_t start_ms = unifrog_perf_time_ms();
   unsigned lines = 0;
   unsigned keys = 0;

   file = fopen(path, "rb");
   if (!file)
      return -1;
   while (fgets(line, sizeof(line), file)) {
      char *eq;

      strip_eol(line);
      if (!line[0] || line[0] == '#' || line[0] == ';')
         continue;
      if (line[0] == '[') {
         char *end = strchr(line + 1, ']');

         if (end) {
            *end = '\0';
            unifrog_text_copy(section, sizeof(section),
               trim_ascii(line + 1));
         }
         continue;
      }
      eq = strchr(line, '=');
      if (!eq || eq == line)
         continue;
      *eq++ = '\0';
      scheme_set_color(theme, style, section, trim_ascii(line),
         trim_ascii(eq));
      scheme_set_layout(style, section, trim_ascii(line), trim_ascii(eq));
      keys++;
   }
   fclose(file);
   if (unifrog_perf_time_ms() - start_ms > 50u)
      unifrog_log("frontend theme scheme slow path=%s ms=%u lines=%u keys=%u\n",
         path, (unsigned)(unifrog_perf_time_ms() - start_ms), lines, keys);
   return 0;
}

static void theme_try_wallpaper(struct unifrog_frontend_lvgl_style *style,
   const char *dir, const char *name)
{
   char path[FRONTEND_MAX_PATH];

   if (!style || style->wallpaper[0] || !dir || !name)
      return;
   if (path_join(path, sizeof(path), dir, name) == 0 && file_exists(path))
      unifrog_text_copy(style->wallpaper, sizeof(style->wallpaper), path);
}

static void theme_override_wallpaper(struct unifrog_frontend_lvgl_style *style,
   const char *dir, const char *name)
{
   char path[FRONTEND_MAX_PATH];

   if (!style || !dir || !name)
      return;
   if (path_join(path, sizeof(path), dir, name) == 0 && file_exists(path))
      unifrog_text_copy(style->wallpaper, sizeof(style->wallpaper), path);
}

static void theme_try_static_image(struct unifrog_frontend_lvgl_style *style,
   const char *dir, const char *name)
{
   char path[FRONTEND_MAX_PATH];

   if (!style || style->static_image[0] || !dir || !name)
      return;
   if (path_join(path, sizeof(path), dir, name) == 0 && file_exists(path))
      unifrog_text_copy(style->static_image, sizeof(style->static_image),
         path);
}

static void theme_override_static_image(struct unifrog_frontend_lvgl_style *style,
   const char *dir, const char *name)
{
   char path[FRONTEND_MAX_PATH];

   if (!style || !dir || !name)
      return;
   if (path_join(path, sizeof(path), dir, name) == 0 && file_exists(path))
      unifrog_text_copy(style->static_image, sizeof(style->static_image),
         path);
}

static void theme_try_launch_wallpaper(struct unifrog_frontend_lvgl_style *style,
   unsigned index, const char *dir, const char *name)
{
   char path[FRONTEND_MAX_PATH];

   if (!style || index >= ARRAY_SIZE(style->launch_wallpaper) ||
       style->launch_wallpaper[index][0] || !dir || !name)
      return;
   if (path_join(path, sizeof(path), dir, name) == 0 && file_exists(path))
      unifrog_text_copy(style->launch_wallpaper[index],
         sizeof(style->launch_wallpaper[index]), path);
}

static void theme_try_launch_wallpaper_all(struct unifrog_frontend_lvgl_style *style,
   const char *dir, const char *name)
{
   char path[FRONTEND_MAX_PATH];

   if (!style || !dir || !name)
      return;
   if (path_join(path, sizeof(path), dir, name) != 0 || !file_exists(path))
      return;
   for (unsigned i = 0; i < ARRAY_SIZE(style->launch_wallpaper); i++) {
      if (!style->launch_wallpaper[i][0])
         unifrog_text_copy(style->launch_wallpaper[i],
            sizeof(style->launch_wallpaper[i]), path);
   }
   theme_try_launch_wallpaper_all(style, dir,
      "320x240/image/wall/muxlaunch.png");
   theme_try_launch_wallpaper_all(style, dir,
      "640x480/image/wall/muxlaunch.png");
   theme_try_launch_wallpaper_all(style, dir,
      "720x480/image/wall/muxlaunch.png");
   theme_try_launch_wallpaper_all(style, dir,
      "720x576/image/wall/muxlaunch.png");
   theme_try_launch_wallpaper_all(style, dir,
      "1024x768/image/wall/muxlaunch.png");
   theme_try_launch_wallpaper_all(style, dir,
      "1280x720/image/wall/muxlaunch.png");
   theme_try_launch_wallpaper_all(style, dir,
      "320x240/image/static/muxlaunch.png");
   theme_try_launch_wallpaper_all(style, dir,
      "640x480/image/static/muxlaunch.png");
   theme_try_launch_wallpaper_all(style, dir,
      "720x480/image/static/muxlaunch.png");
   theme_try_launch_wallpaper_all(style, dir,
      "720x576/image/static/muxlaunch.png");
   theme_try_launch_wallpaper_all(style, dir,
      "1024x768/image/static/muxlaunch.png");
   theme_try_launch_wallpaper_all(style, dir,
      "1280x720/image/static/muxlaunch.png");
   theme_try_launch_wallpaper_all(style, dir,
      "320x240/image/muxlaunch.png");
   theme_try_launch_wallpaper_all(style, dir,
      "640x480/image/muxlaunch.png");
   theme_try_launch_wallpaper_all(style, dir,
      "720x480/image/muxlaunch.png");
   theme_try_launch_wallpaper_all(style, dir,
      "720x576/image/muxlaunch.png");
   theme_try_launch_wallpaper_all(style, dir,
      "1024x768/image/muxlaunch.png");
   theme_try_launch_wallpaper_all(style, dir,
      "1280x720/image/muxlaunch.png");
   theme_try_launch_wallpaper_all(style, dir, "image/muxlaunch.png");
}

static void theme_try_launch_icon(struct unifrog_frontend_lvgl_style *style,
   unsigned index, const char *dir, const char *name)
{
   char path[FRONTEND_MAX_PATH];

   if (!style || index >= ARRAY_SIZE(style->launch_icon) ||
       style->launch_icon[index][0] || !dir || !name)
      return;
   if (path_join(path, sizeof(path), dir, name) == 0 && file_exists(path))
      unifrog_text_copy(style->launch_icon[index],
         sizeof(style->launch_icon[index]), path);
}

static void frontend_loading_show(struct native_frontend *fe, const char *title,
   const char *name, const char *stage, unsigned percent)
{
   struct unifrog_surface surface;
   int bar_x;
   int bar_y;
   int bar_w;
   int bar_h;
   int fill_w;
   char percent_text[16];

   if (!fe)
      return;
   if (percent > 100u)
      percent = 100u;
   frontend_invalidate_draw(fe);
   if (!fe->ui.fb.pixels)
      return;

   unifrog_ui_begin(&fe->ui, UNIFROG_RGB565(0, 0, 0));
   surface = unifrog_ui_surface(&fe->ui);
   unifrog_gfx_draw_text(&surface, 18, 54, title ? title : "LOADING",
      UNIFROG_RGB565(236, 241, 246), 2);
   if (name && name[0])
      unifrog_gfx_draw_text(&surface, 18, 86, name,
         UNIFROG_RGB565(160, 174, 188), 1);
   if (stage && stage[0])
      unifrog_gfx_draw_text(&surface, 18, 104, stage,
         UNIFROG_RGB565(160, 174, 188), 1);

   bar_x = 18;
   bar_y = (int)surface.height - 54;
   bar_w = (int)surface.width - 36;
   bar_h = 14;
   fill_w = (bar_w - 4) * (int)percent / 100;
   snprintf(percent_text, sizeof(percent_text), "%u%%", percent);
   unifrog_gfx_fill_rect(&surface, bar_x, bar_y, bar_w, bar_h,
      UNIFROG_RGB565(42, 50, 60));
   if (fill_w > 0)
      unifrog_gfx_fill_rect(&surface, bar_x + 2, bar_y + 2, fill_w,
         bar_h - 4, UNIFROG_RGB565(68, 188, 136));
   unifrog_gfx_draw_text(&surface, 18, bar_y + 24, percent_text,
      UNIFROG_RGB565(236, 241, 246), 1);
   unifrog_ui_present(&fe->ui);
}

static void frontend_install_progress_update(void *userdata, const char *stage,
   unsigned done, unsigned total)
{
   struct frontend_install_progress *progress = userdata;
   struct native_frontend *fe;
   uint32_t now;
   unsigned percent;
   char detail[80];

   if (!progress || !progress->fe)
      return;
   fe = progress->fe;
   if (total == 0)
      total = 1;
   if (done > total)
      done = total;
   percent = done * 100u / total;
   now = unifrog_perf_time_ms();
   if (!progress->start_ms)
      progress->start_ms = now;
   if (percent < 100u && progress->last_draw_ms &&
       now - progress->last_draw_ms < 120u &&
       percent < progress->last_percent + 2u)
      return;

   if (percent > 0u && percent < 100u) {
      uint32_t elapsed_ms = now - progress->start_ms;
      uint32_t eta_ms = elapsed_ms * (100u - percent) / percent;

      snprintf(detail, sizeof(detail), "%s %u%% eta %us",
         stage && stage[0] ? stage : "working", percent,
         (unsigned)((eta_ms + 999u) / 1000u));
   } else {
      snprintf(detail, sizeof(detail), "%s %u%%",
         stage && stage[0] ? stage : "working", percent);
   }
   frontend_loading_show(fe, progress->title, progress->name, detail, percent);
   progress->last_draw_ms = now;
   progress->last_percent = percent;
}

static const char *item_glyph_key(const struct frontend_item *item)
{
   if (!item)
      return "";
   if (is_back_item(item))
      return "back";
   if (item->kind == FRONTEND_ITEM_DIR)
      return "folder";
   if (item->kind == FRONTEND_ITEM_GAME)
      return "content";
   if (item->kind == FRONTEND_ITEM_MEDIA)
      return "media";
   if (item->kind == FRONTEND_ITEM_FIRMWARE)
      return "archive";
   if (item->kind == FRONTEND_ITEM_THEME_ARCHIVE)
      return "theme";
   if (item->kind == FRONTEND_ITEM_CORE_MODULE)
      return "core";
   if (strcmp(item->path, "launch_settings") == 0)
      return "general";
   if (strcmp(item->path, "theme_alternate") == 0)
      return "alternate";
   if (strcmp(item->path, "launch_splash") == 0)
      return "splash";
   if (strcmp(item->path, "boxart_hide") == 0)
      return "boxarthide";
   if (strcmp(item->path, "title_root") == 0)
      return "titleincluderootdrive";
   if (strcmp(item->path, "empty_folder") == 0)
      return "folderempty";
   if (strcmp(item->path, "folder_counts") == 0)
      return "folderitemcount";
   if (strcmp(item->path, "counter_folder") == 0)
      return "counterfolder";
   if (strcmp(item->path, "counter_file") == 0)
      return "counterfile";
   if (strcmp(item->path, "content_collect") == 0)
      return "collection";
   if (strcmp(item->path, "content_history") == 0)
      return "history";
   if (strcmp(item->path, "mixed_content") == 0)
      return "mixedcontent";
   if (strcmp(item->path, "storage_fast_probe") == 0)
      return "tester";
   if (strcmp(item->path, "package_check") == 0)
      return "tester";
   if (strcmp(item->path, "cores") == 0)
      return "core";
   if (strcmp(item->path, "storage_recover") == 0)
      return "storage";
   if (strcmp(item->path, "flush_log") == 0)
      return "log";
   if (strcmp(item->path, "explore_unifrog") == 0)
      return "init";
   if (strcmp(item->path, "explore_data") == 0)
      return "folder";
   if (strcmp(item->path, "explore_bios") == 0)
      return "bios";
   if (strcmp(item->path, "explore_saves") == 0)
      return "save";
   if (strcmp(item->path, "storage_mode") == 0 ||
       strcmp(item->path, "storage_profile") == 0)
      return "storage";
   if (strcmp(item->path, "back") == 0 ||
       strncmp(item->path, "back_", 5) == 0)
      return "back";
   if (strcmp(item->path, "theme_select") == 0)
      return "theme";
   if (strcmp(item->path, "language_select") == 0)
      return "language";
   if (strcmp(item->path, "firmware") == 0)
      return "reboot";
   if (item->path[0] && strcmp(item->path, "noop") != 0)
      return item->path;
   return item->name;
}

static void sanitize_glyph_key(char *dst, size_t dst_size, const char *src)
{
   size_t pos = 0;

   if (!dst || dst_size == 0)
      return;
   dst[0] = '\0';
   if (!src)
      return;
   for (; *src && pos + 1u < dst_size; src++) {
      unsigned char ch = (unsigned char)*src;

      if (ch >= 'A' && ch <= 'Z')
         ch = (unsigned char)(ch - 'A' + 'a');
      if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
          ch == '_' || ch == '-')
         dst[pos++] = (char)ch;
   }
   dst[pos] = '\0';
}

static int theme_glyph_path(struct native_frontend *fe, const char *module,
   const char *key, char *out, size_t out_size)
{
   char dir[FRONTEND_MAX_PATH];
   char rel[FRONTEND_MAX_PATH];
   char clean[64];
   static const char *const prefixes[] = {
      "320x240/glyph",
      "640x480/glyph",
      "720x480/glyph",
      "720x576/glyph",
      "1024x768/glyph",
      "1280x720/glyph",
      "glyph",
   };

   if (!fe || !module || !key || !out || out_size == 0 ||
       !fe->dir_theme_loaded ||
       path_join(dir, sizeof(dir), FRONTEND_THEME_ROOT, fe->theme_name) != 0)
      return -1;
   sanitize_glyph_key(clean, sizeof(clean), key);
   if (!clean[0])
      return -1;
   for (unsigned i = 0; i < ARRAY_SIZE(prefixes); i++) {
      snprintf(rel, sizeof(rel), "%s/%s/%s.png", prefixes[i], module, clean);
      if (path_join(out, out_size, dir, rel) == 0 && file_exists(out))
         return 0;
   }
   for (unsigned i = 0; i < ARRAY_SIZE(prefixes); i++) {
      snprintf(rel, sizeof(rel), "%s/%s.png", prefixes[i], clean);
      if (path_join(out, out_size, dir, rel) == 0 && file_exists(out))
         return 0;
   }
   out[0] = '\0';
   return -1;
}

static unsigned visible_rows_for_style(
   const struct unifrog_frontend_lvgl_style *style)
{
   int header_h = style ? style->header_height : 38;
   int footer_h = style ? style->footer_height : 38;
   int content_h = 240 - header_h - footer_h - 2;
   int row_h = style ? style->list_h : 22;
   int row_gap = style ? style->list_gap : 2;
   int list_y = style ? style->list_y : 8;
   int rows;

   if (content_h < 40)
      content_h = 40;
   if (row_h < 1)
      row_h = 1;
   rows = (content_h - list_y + row_gap) / (row_h + row_gap);
   if (rows < 1)
      rows = 1;
   if (rows > (int)FRONTEND_LVGL_LIST_ROWS)
      rows = (int)FRONTEND_LVGL_LIST_ROWS;
   return (unsigned)rows;
}

static void visible_item_range(unsigned count, unsigned selected,
   unsigned rows, unsigned *start, unsigned *stop)
{
   if (!start || !stop) {
      return;
   }
   if (count == 0 || rows == 0) {
      *start = 0;
      *stop = 0;
      return;
   }
   if (selected >= count)
      selected = count - 1u;
   *start = 0;
   if (count > rows && selected >= rows / 2u)
      *start = selected - rows / 2u;
   if (*start + rows > count)
      *start = count > rows ? count - rows : 0;
   *stop = *start + rows;
   if (*stop > count)
      *stop = count;
}

static void fill_visible_item_glyphs(struct native_frontend *fe,
   const char *module, unsigned start, unsigned stop,
   char paths[FRONTEND_MAX_ITEMS][FRONTEND_MAX_PATH], const char *glyphs[FRONTEND_MAX_ITEMS])
{
   if (!fe || !paths || !glyphs)
      return;
   if (!fe->dir_theme_loaded || !module || !module[0]) {
      for (unsigned i = 0; i < fe->item_count; i++)
         glyphs[i] = NULL;
      return;
   }
   if (fe->glyph_cache_generation != fe->item_generation ||
       strcmp(fe->glyph_cache_module, module) != 0) {
      memset(fe->item_glyph_resolved, 0, sizeof(fe->item_glyph_resolved));
      for (unsigned i = 0; i < fe->item_count; i++) {
         paths[i][0] = '\0';
         glyphs[i] = NULL;
      }
      fe->glyph_cache_generation = fe->item_generation;
      unifrog_text_copy(fe->glyph_cache_module, sizeof(fe->glyph_cache_module),
         module);
   }
   if (fe->item_count == 0)
      return;
   if (start > fe->item_count)
      start = fe->item_count;
   if (stop > fe->item_count)
      stop = fe->item_count;
   for (unsigned i = start; i < stop; i++) {
      if (fe->item_glyph_resolved[i]) {
         glyphs[i] = paths[i][0] ? paths[i] : NULL;
         continue;
      }
      fe->item_glyph_resolved[i] = 1;
      if (theme_glyph_path(fe, module, item_glyph_key(&fe->items[i]),
          paths[i], FRONTEND_MAX_PATH) == 0)
         glyphs[i] = paths[i];
      else
         glyphs[i] = NULL;
   }
}

static const char *const frontend_scheme_prefixes[] = {
   "320x240/scheme",
   "640x480/scheme",
   "720x480/scheme",
   "720x576/scheme",
   "720x720/scheme",
   "1024x768/scheme",
   "1280x720/scheme",
   "scheme",
};

static void add_theme_scheme(struct native_frontend *fe, const char *name)
{
   char clean[32];
   size_t len;

   if (!fe || !name || !name[0])
      return;
   unifrog_text_copy(clean, sizeof(clean), name);
   if (!unifrog_text_ends_with_ci(clean, ".ini"))
      return;
   len = strlen(clean);
   if (len > 4u)
      clean[len - 4u] = '\0';
   if (!clean[0])
      return;
   for (unsigned i = 0; i < fe->scheme_count; i++) {
      if (strcmp(fe->scheme_name[i], clean) == 0)
         return;
   }
   if (fe->scheme_count >= FRONTEND_SCHEME_MAX)
      return;
   unifrog_text_copy(fe->scheme_name[fe->scheme_count],
      sizeof(fe->scheme_name[0]), clean);
   fe->scheme_count++;
}

static void scan_theme_schemes(struct native_frontend *fe, const char *dir)
{
   uint32_t start_ms = unifrog_perf_time_ms();

   if (!fe || !dir)
      return;
   fe->scheme_count = 0;
   for (unsigned i = 0; i < ARRAY_SIZE(frontend_scheme_prefixes); i++) {
      char path[FRONTEND_MAX_PATH];
      DIR *scan;
      struct dirent *entry;

      if (path_join(path, sizeof(path), dir, frontend_scheme_prefixes[i]) != 0)
         continue;
      scan = opendir(path);
      if (!scan)
         continue;
      while ((entry = readdir(scan)) != NULL)
         add_theme_scheme(fe, entry->d_name);
      closedir(scan);
   }
   unifrog_log("frontend theme scheme index count=%u ms=%u\n",
      fe->scheme_count, (unsigned)(unifrog_perf_time_ms() - start_ms));
}

static int frontend_has_scheme(const struct native_frontend *fe,
   const char *scheme)
{
   if (!fe || !scheme || !scheme[0])
      return 0;
   for (unsigned i = 0; i < fe->scheme_count; i++)
      if (strcmp(fe->scheme_name[i], scheme) == 0)
         return 1;
   return 0;
}

static int load_muos_named_scheme(const char *dir, const char *scheme,
   struct unifrog_ui_theme *theme, struct unifrog_frontend_lvgl_style *style)
{
   char path[FRONTEND_MAX_PATH];

   if (!dir || !scheme || !scheme[0] || !theme || !style)
      return -1;
   for (unsigned i = 0; i < ARRAY_SIZE(frontend_scheme_prefixes); i++) {
      char rel[96];

      snprintf(rel, sizeof(rel), "%s/%s.ini", frontend_scheme_prefixes[i],
         scheme);
      if (path_join(path, sizeof(path), dir, rel) == 0 &&
          load_muos_scheme_file(path, theme, style) == 0)
         return 0;
   }
   return -1;
}

static void theme_apply_wallpapers(struct unifrog_frontend_lvgl_style *style,
   const char *dir, const char *module, int include_launch)
{
   if (!style || !dir)
      return;
   theme_try_wallpaper(style, dir, "320x240/image/wall/default.png");
   theme_try_wallpaper(style, dir, "640x480/image/wall/default.png");
   theme_try_wallpaper(style, dir, "720x480/image/wall/default.png");
   theme_try_wallpaper(style, dir, "720x576/image/wall/default.png");
   theme_try_wallpaper(style, dir, "1024x768/image/wall/default.png");
   theme_try_wallpaper(style, dir, "1280x720/image/wall/default.png");
   theme_try_wallpaper(style, dir, "image/wall/default.png");
   theme_try_wallpaper(style, dir, "320x240/image/default.png");
   theme_try_wallpaper(style, dir, "640x480/image/default.png");
   theme_try_wallpaper(style, dir, "720x480/image/default.png");
   theme_try_wallpaper(style, dir, "720x576/image/default.png");
   theme_try_wallpaper(style, dir, "1024x768/image/default.png");
   theme_try_wallpaper(style, dir, "1280x720/image/default.png");
   theme_try_wallpaper(style, dir, "image/default.png");
   theme_try_static_image(style, dir, "320x240/image/static/default.png");
   theme_try_static_image(style, dir, "640x480/image/static/default.png");
   theme_try_static_image(style, dir, "720x480/image/static/default.png");
   theme_try_static_image(style, dir, "720x576/image/static/default.png");
   theme_try_static_image(style, dir, "1024x768/image/static/default.png");
   theme_try_static_image(style, dir, "1280x720/image/static/default.png");
   theme_try_static_image(style, dir, "image/static/default.png");
   if (module && module[0]) {
      char rel[96];

      snprintf(rel, sizeof(rel), "1280x720/image/wall/%s.png", module);
      theme_override_wallpaper(style, dir, rel);
      snprintf(rel, sizeof(rel), "1024x768/image/wall/%s.png", module);
      theme_override_wallpaper(style, dir, rel);
      snprintf(rel, sizeof(rel), "720x576/image/wall/%s.png", module);
      theme_override_wallpaper(style, dir, rel);
      snprintf(rel, sizeof(rel), "720x480/image/wall/%s.png", module);
      theme_override_wallpaper(style, dir, rel);
      snprintf(rel, sizeof(rel), "640x480/image/wall/%s.png", module);
      theme_override_wallpaper(style, dir, rel);
      snprintf(rel, sizeof(rel), "320x240/image/wall/%s.png", module);
      theme_override_wallpaper(style, dir, rel);
      snprintf(rel, sizeof(rel), "image/wall/%s.png", module);
      theme_override_wallpaper(style, dir, rel);
      snprintf(rel, sizeof(rel), "640x480/image/wall/%s/default.png", module);
      theme_override_wallpaper(style, dir, rel);
      snprintf(rel, sizeof(rel), "320x240/image/wall/%s/default.png", module);
      theme_override_wallpaper(style, dir, rel);
      snprintf(rel, sizeof(rel), "1280x720/image/static/%s.png", module);
      theme_override_static_image(style, dir, rel);
      snprintf(rel, sizeof(rel), "1024x768/image/static/%s.png", module);
      theme_override_static_image(style, dir, rel);
      snprintf(rel, sizeof(rel), "720x576/image/static/%s.png", module);
      theme_override_static_image(style, dir, rel);
      snprintf(rel, sizeof(rel), "720x480/image/static/%s.png", module);
      theme_override_static_image(style, dir, rel);
      snprintf(rel, sizeof(rel), "640x480/image/static/%s.png", module);
      theme_override_static_image(style, dir, rel);
      snprintf(rel, sizeof(rel), "320x240/image/static/%s.png", module);
      theme_override_static_image(style, dir, rel);
      snprintf(rel, sizeof(rel), "image/static/%s.png", module);
      theme_override_static_image(style, dir, rel);
      snprintf(rel, sizeof(rel), "640x480/image/static/%s/default.png",
         module);
      theme_override_static_image(style, dir, rel);
      snprintf(rel, sizeof(rel), "320x240/image/static/%s/default.png",
         module);
      theme_override_static_image(style, dir, rel);
      snprintf(rel, sizeof(rel), "1280x720/image/%s.png", module);
      theme_override_wallpaper(style, dir, rel);
      snprintf(rel, sizeof(rel), "1024x768/image/%s.png", module);
      theme_override_wallpaper(style, dir, rel);
      snprintf(rel, sizeof(rel), "720x576/image/%s.png", module);
      theme_override_wallpaper(style, dir, rel);
      snprintf(rel, sizeof(rel), "720x480/image/%s.png", module);
      theme_override_wallpaper(style, dir, rel);
      snprintf(rel, sizeof(rel), "640x480/image/%s.png", module);
      theme_override_wallpaper(style, dir, rel);
      snprintf(rel, sizeof(rel), "320x240/image/%s.png", module);
      theme_override_wallpaper(style, dir, rel);
      snprintf(rel, sizeof(rel), "image/%s.png", module);
      theme_override_wallpaper(style, dir, rel);
      snprintf(rel, sizeof(rel), "640x480/image/%s/default.png", module);
      theme_override_wallpaper(style, dir, rel);
      snprintf(rel, sizeof(rel), "320x240/image/%s/default.png", module);
      theme_override_wallpaper(style, dir, rel);
      snprintf(rel, sizeof(rel), "image/%s/default.png", module);
      theme_override_wallpaper(style, dir, rel);
   }
   if (!include_launch)
      return;
   theme_try_wallpaper(style, dir, "320x240/image/wall/muxlaunch/default.png");
   theme_try_wallpaper(style, dir, "640x480/image/wall/muxlaunch/default.png");
   theme_try_wallpaper(style, dir, "720x480/image/wall/muxlaunch/default.png");
   theme_try_wallpaper(style, dir, "720x576/image/wall/muxlaunch/default.png");
   theme_try_wallpaper(style, dir, "1024x768/image/wall/muxlaunch/default.png");
   theme_try_wallpaper(style, dir, "1280x720/image/wall/muxlaunch/default.png");
   theme_try_wallpaper(style, dir, "320x240/image/static/muxlaunch/default.png");
   theme_try_wallpaper(style, dir, "640x480/image/static/muxlaunch/default.png");
   theme_try_wallpaper(style, dir, "720x480/image/static/muxlaunch/default.png");
   theme_try_wallpaper(style, dir, "720x576/image/static/muxlaunch/default.png");
   theme_try_wallpaper(style, dir, "1024x768/image/static/muxlaunch/default.png");
   theme_try_wallpaper(style, dir, "1280x720/image/static/muxlaunch/default.png");
   theme_try_wallpaper(style, dir, "320x240/image/muxlaunch/default.png");
   theme_try_wallpaper(style, dir, "640x480/image/muxlaunch/default.png");
   theme_try_wallpaper(style, dir, "720x480/image/muxlaunch/default.png");
   theme_try_wallpaper(style, dir, "720x576/image/muxlaunch/default.png");
   theme_try_wallpaper(style, dir, "1024x768/image/muxlaunch/default.png");
   theme_try_wallpaper(style, dir, "1280x720/image/muxlaunch/default.png");
   theme_try_wallpaper(style, dir, "image/muxlaunch/default.png");
   theme_try_wallpaper(style, dir, "320x240/preview.png");
   theme_try_wallpaper(style, dir, "640x480/preview.png");
   theme_try_wallpaper(style, dir, "preview.png");
   {
      static const char *const launch_names[] = {
         "explore", "collection", "history", "apps",
         "info", "config", "reboot", "shutdown",
      };

      for (unsigned i = 0; i < ARRAY_SIZE(launch_names); i++) {
         char rel[96];

         snprintf(rel, sizeof(rel),
            "320x240/image/wall/muxlaunch/%s.png", launch_names[i]);
         theme_try_launch_wallpaper(style, i, dir, rel);
         snprintf(rel, sizeof(rel),
            "640x480/image/wall/muxlaunch/%s.png", launch_names[i]);
         theme_try_launch_wallpaper(style, i, dir, rel);
         snprintf(rel, sizeof(rel),
            "320x240/image/static/muxlaunch/%s.png", launch_names[i]);
         theme_try_launch_wallpaper(style, i, dir, rel);
         snprintf(rel, sizeof(rel),
            "640x480/image/static/muxlaunch/%s.png", launch_names[i]);
         theme_try_launch_wallpaper(style, i, dir, rel);
         snprintf(rel, sizeof(rel),
            "720x480/image/static/muxlaunch/%s.png", launch_names[i]);
         theme_try_launch_wallpaper(style, i, dir, rel);
         snprintf(rel, sizeof(rel),
            "720x576/image/static/muxlaunch/%s.png", launch_names[i]);
         theme_try_launch_wallpaper(style, i, dir, rel);
         snprintf(rel, sizeof(rel),
            "1024x768/image/static/muxlaunch/%s.png", launch_names[i]);
         theme_try_launch_wallpaper(style, i, dir, rel);
         snprintf(rel, sizeof(rel),
            "1280x720/image/static/muxlaunch/%s.png", launch_names[i]);
         theme_try_launch_wallpaper(style, i, dir, rel);
         snprintf(rel, sizeof(rel),
            "image/wall/muxlaunch/%s.png", launch_names[i]);
         theme_try_launch_wallpaper(style, i, dir, rel);
         snprintf(rel, sizeof(rel),
            "image/static/muxlaunch/%s.png", launch_names[i]);
         theme_try_launch_wallpaper(style, i, dir, rel);
         snprintf(rel, sizeof(rel),
            "320x240/image/muxlaunch/%s.png", launch_names[i]);
         theme_try_launch_wallpaper(style, i, dir, rel);
         snprintf(rel, sizeof(rel),
            "640x480/image/muxlaunch/%s.png", launch_names[i]);
         theme_try_launch_wallpaper(style, i, dir, rel);
         snprintf(rel, sizeof(rel),
            "720x480/image/muxlaunch/%s.png", launch_names[i]);
         theme_try_launch_wallpaper(style, i, dir, rel);
         snprintf(rel, sizeof(rel),
            "720x576/image/muxlaunch/%s.png", launch_names[i]);
         theme_try_launch_wallpaper(style, i, dir, rel);
         snprintf(rel, sizeof(rel),
            "1024x768/image/muxlaunch/%s.png", launch_names[i]);
         theme_try_launch_wallpaper(style, i, dir, rel);
         snprintf(rel, sizeof(rel),
            "1280x720/image/muxlaunch/%s.png", launch_names[i]);
         theme_try_launch_wallpaper(style, i, dir, rel);
         snprintf(rel, sizeof(rel),
            "image/muxlaunch/%s.png", launch_names[i]);
         theme_try_launch_wallpaper(style, i, dir, rel);
         snprintf(rel, sizeof(rel),
            "320x240/glyph/muxlaunch/%s.png", launch_names[i]);
         theme_try_launch_icon(style, i, dir, rel);
         snprintf(rel, sizeof(rel),
            "640x480/glyph/muxlaunch/%s.png", launch_names[i]);
         theme_try_launch_icon(style, i, dir, rel);
         snprintf(rel, sizeof(rel), "glyph/muxlaunch/%s.png",
            launch_names[i]);
         theme_try_launch_icon(style, i, dir, rel);
      }
   }
}

static int load_muos_theme_dir(const char *name, const char *module,
   struct unifrog_ui_theme *theme, struct unifrog_frontend_lvgl_style *style)
{
   char dir[FRONTEND_MAX_PATH];
   char path[FRONTEND_MAX_PATH];
   DIR *scan;
   struct dirent *entry;
   int loaded = 0;

   if (!name || !name[0] || !theme || !style ||
       path_join(dir, sizeof(dir), FRONTEND_THEME_ROOT, name) != 0)
      return -1;
   if (load_muos_named_scheme(dir, "global", theme, style) == 0)
      loaded++;
   if (load_muos_named_scheme(dir, "default", theme, style) == 0)
      loaded++;
   if (module && module[0] &&
       load_muos_named_scheme(dir, module, theme, style) == 0)
      loaded++;
   if (loaded == 0) {
      static const char *const fallback_schemes[] = {
         "muxplore",
         "muxhistory",
         "muxcollect",
      };

      for (unsigned i = 0; i < ARRAY_SIZE(fallback_schemes); i++) {
         if (load_muos_named_scheme(dir, fallback_schemes[i], theme,
             style) == 0) {
            loaded++;
            break;
         }
      }
   }
   scan = opendir(dir);
   if (loaded == 0 && scan) {
      while ((entry = readdir(scan)) != NULL) {
         char candidate[FRONTEND_MAX_PATH];

         if (entry->d_name[0] == '.')
            continue;
         if (snprintf(candidate, sizeof(candidate), "%s/%s/scheme/default.ini",
             dir, entry->d_name) <= 0)
            continue;
         if (load_muos_scheme_file(candidate, theme, style) == 0) {
            loaded++;
            break;
         }
      }
   }
   if (scan)
      closedir(scan);
   if (loaded == 0 &&
       path_join(path, sizeof(path), dir, "theme.ini") == 0 &&
       load_muos_scheme_file(path, theme, style) == 0)
      loaded++;
   theme_apply_wallpapers(style, dir, module, module == NULL || !module[0]);
   return loaded > 0 ? 0 : -1;
}

static int load_frontend_module_from_base(struct native_frontend *fe,
   const char *name, const char *module,
   const struct unifrog_ui_theme *base_theme,
   const struct unifrog_frontend_lvgl_style *base_style,
   struct unifrog_ui_theme *theme, struct unifrog_frontend_lvgl_style *style)
{
   char dir[FRONTEND_MAX_PATH];
   uint32_t start_ms = unifrog_perf_time_ms();
   int module_loaded = 0;

   if (!name || !name[0] || !module || !base_theme || !base_style ||
       !theme || !style ||
       path_join(dir, sizeof(dir), FRONTEND_THEME_ROOT, name) != 0)
      return -1;
   *theme = *base_theme;
   *style = *base_style;
   if (frontend_has_scheme(fe, module) &&
       load_muos_named_scheme(dir, module, theme, style) == 0)
      module_loaded = 1;
   theme_apply_wallpapers(style, dir, module, 0);
   if (unifrog_perf_time_ms() - start_ms > 100u)
      unifrog_log("frontend theme module slow name=%s module=%s loaded=%d ms=%u\n",
         name, module, module_loaded,
         (unsigned)(unifrog_perf_time_ms() - start_ms));
   return 0;
}

static void alternate_style(struct unifrog_frontend_lvgl_style *style)
{
   uint16_t style_tmp;

   if (!style)
      return;
   style_tmp = style->list_focus_background;
   style->list_focus_background = style->list_focus_indicator;
   style->list_focus_indicator = style_tmp;
}

static void apply_frontend_style(struct native_frontend *fe, int id,
   const struct unifrog_frontend_lvgl_style *style)
{
   if (!fe || !style)
      return;
   if (fe->applied_style_id == id)
      return;
   unifrog_frontend_lvgl_set_style(style);
   fe->applied_style_id = id;
}

static const struct unifrog_frontend_lvgl_style *frontend_screen_style(
   struct native_frontend *fe, enum unifrog_frontend_lvgl_screen screen)
{
   struct unifrog_ui_theme style_theme;
   uint32_t start_ms;

   if (!fe || screen > UNIFROG_FRONTEND_LVGL_VISUAL)
      return fe ? &fe->active_style : NULL;
   if (!fe->dir_theme_loaded || fe->screen_style_valid[screen])
      return &fe->screen_style[screen];

   start_ms = unifrog_perf_time_ms();
   fe->screen_style[screen] = fe->active_style;
   if (load_frontend_module_from_base(fe, fe->theme_name,
       lvgl_screen_module(screen), &fe->base_theme, &fe->base_style,
       &style_theme,
       &fe->screen_style[screen]) != 0)
      fe->screen_style[screen] = fe->active_style;
   else if (fe->theme_alternate)
      alternate_style(&fe->screen_style[screen]);
   fe->screen_style_valid[screen] = 1;
   unifrog_log("frontend theme screen style loaded name=%s screen=%d module=%s ms=%u text=%04x/%04x alpha=%u/%u\n",
      fe->theme_name, (int)screen, lvgl_screen_module(screen),
      (unsigned)(unifrog_perf_time_ms() - start_ms),
      fe->screen_style[screen].list_text,
      fe->screen_style[screen].list_focus_text,
      fe->screen_style[screen].list_text_alpha,
      fe->screen_style[screen].list_focus_text_alpha);
   return &fe->screen_style[screen];
}

static const struct unifrog_frontend_lvgl_style *frontend_view_style(
   struct native_frontend *fe, enum frontend_view view)
{
   (void)view;
   if (!fe)
      return fe ? &fe->list_style : NULL;
   return &fe->list_style;
}

static int try_load_theme_font_path(const char *dir, const char *rel)
{
   char path[FRONTEND_MAX_PATH];
   int ret;

   if (!dir || !rel || path_join(path, sizeof(path), dir, rel) != 0 ||
       !file_exists(path))
      return -1;
   ret = unifrog_gfx_load_font5x7_file(path);
   unifrog_log("frontend theme font load path=%s ret=%d\n", path, ret);
   return ret > 0 ? 0 : -1;
}

static void load_theme_font(struct native_frontend *fe)
{
   char dir[FRONTEND_MAX_PATH];
   const char *language;
   const char *module = "default";
   char rel[128];
   static const char *const exts[] = { ".ttf", ".otf", ".bin", ".font" };
   static const char *const roots[] = {
      "320x240/font",
      "640x480/font",
      "font",
   };

   unifrog_gfx_reset_font5x7();
   if (!fe || !fe->theme_name[0] ||
       path_join(dir, sizeof(dir), FRONTEND_THEME_ROOT, fe->theme_name) != 0)
      return;
   language = active_language_label(fe);
   for (unsigned r = 0; r < ARRAY_SIZE(roots); r++) {
      for (unsigned e = 0; e < ARRAY_SIZE(exts); e++) {
         snprintf(rel, sizeof(rel), "%s/%s/%s%s", roots[r], language,
            module, exts[e]);
         if (try_load_theme_font_path(dir, rel) == 0)
            return;
         snprintf(rel, sizeof(rel), "%s/%s/default%s", roots[r], language,
            exts[e]);
         if (try_load_theme_font_path(dir, rel) == 0)
            return;
         snprintf(rel, sizeof(rel), "%s/%s%s", roots[r], module, exts[e]);
         if (try_load_theme_font_path(dir, rel) == 0)
            return;
         snprintf(rel, sizeof(rel), "%s/default%s", roots[r], exts[e]);
         if (try_load_theme_font_path(dir, rel) == 0)
            return;
      }
   }
   unifrog_log("frontend theme font skipped name=%s language=%s\n",
      fe->theme_name, language);
}

static void load_theme(struct native_frontend *fe)
{
   FILE *file;
   char path[FRONTEND_MAX_PATH];
   char line[FRONTEND_MAX_LINE];
   struct unifrog_ui_theme theme = frontend_theme;
   struct unifrog_frontend_lvgl_style style;
   struct unifrog_ui_theme style_theme;
   struct unifrog_ui_theme base_theme;
   struct unifrog_frontend_lvgl_style base_style;
   uint32_t start_ms = unifrog_perf_time_ms();
   uint32_t dir_ms = 0;
   uint32_t list_ms = 0;
   uint32_t screens_ms = 0;
   uint32_t apply_ms = 0;
   uint32_t preload_ms = 0;
   uint32_t t0;
   int dir_theme_loaded = 0;

   if (!fe)
      return;
   if (!fe->theme_name[0])
      unifrog_text_copy(fe->theme_name, sizeof(fe->theme_name), "muos");
   if (fe->theme_loaded &&
       strcmp(fe->loaded_theme_name, fe->theme_name) == 0 &&
       strcmp(fe->loaded_theme_language, active_language_label(fe)) == 0 &&
       fe->loaded_theme_alternate == (fe->theme_alternate ? 1 : 0)) {
      unifrog_log("frontend theme load skipped name=%s language=%s alternate=%d\n",
         fe->theme_name, active_language_label(fe),
         fe->theme_alternate ? 1 : 0);
      return;
   }
   unifrog_exception_activity_set(UNIFROG_ACTIVITY_PHASE_FRONTEND_START,
      unifrog_exception_activity_hash("theme.scheme"),
      unifrog_exception_activity_hash(fe->theme_name),
      (uint32_t)(fe->theme_alternate ? 1 : 0));
   frontend_loading_show(fe, "Loading Theme", fe->theme_name, "scheme", 5);
   unifrog_frontend_lvgl_style_default(&style, &theme);
   fe->scheme_count = 0;
   t0 = unifrog_perf_time_ms();
   if (strcmp(fe->theme_name, "muos") != 0 &&
       load_muos_theme_dir(fe->theme_name, NULL, &theme, &style) == 0) {
      dir_theme_loaded = 1;
      unifrog_log("frontend theme dir loaded name=%s\n", fe->theme_name);
      if (path_join(path, sizeof(path), FRONTEND_THEME_ROOT, fe->theme_name) == 0)
         scan_theme_schemes(fe, path);
   } else if (strcmp(fe->theme_name, "muos") != 0 &&
              path_join_ini(path, sizeof(path), FRONTEND_THEME_ROOT,
              fe->theme_name) == 0) {
      file = fopen(path, "rb");
      if (file) {
         while (fgets(line, sizeof(line), file)) {
            const char *value;
            uint16_t color;

            strip_eol(line);
            if (!line[0] || line[0] == '#')
               continue;
            if ((value = read_key_value(line, "background")) != NULL &&
                parse_rgb565_hex(value, &color) == 0) {
               theme.background = color;
               style.background = color;
            }
            else if ((value = read_key_value(line, "panel")) != NULL &&
                     parse_rgb565_hex(value, &color) == 0) {
               theme.panel = color;
               style.header_background = color;
               style.footer_background = color;
               style.list_background = color;
            }
            else if (((value = read_key_value(line, "focus")) != NULL ||
                     (value = read_key_value(line, "row")) != NULL) &&
                     parse_rgb565_hex(value, &color) == 0) {
               theme.focus = color;
               style.list_focus_background = color;
            }
            else if (((value = read_key_value(line, "text")) != NULL ||
                     (value = read_key_value(line, "text_primary")) != NULL ||
                     (value = read_key_value(line, "text_title")) != NULL) &&
                     parse_rgb565_hex(value, &color) == 0) {
               theme.text = color;
               style.header_text = color;
               style.list_text = color;
            }
            else if (((value = read_key_value(line, "muted")) != NULL ||
                     (value = read_key_value(line, "text_muted")) != NULL ||
                     (value = read_key_value(line, "text_footer")) != NULL) &&
                     parse_rgb565_hex(value, &color) == 0) {
               theme.muted = color;
               style.footer_text = color;
               style.list_indicator = color;
            }
            else if ((value = read_key_value(line, "accent")) != NULL &&
                     parse_rgb565_hex(value, &color) == 0) {
               theme.accent = color;
               style.list_focus_text = color;
               style.list_focus_indicator = color;
            }
            else if (((value = read_key_value(line, "danger")) != NULL ||
                     (value = read_key_value(line, "text_warning")) != NULL) &&
                     parse_rgb565_hex(value, &color) == 0)
               theme.danger = color;
         }
         fclose(file);
         unifrog_log("frontend theme loaded name=%s path=%s\n",
            fe->theme_name, path);
      } else {
         unifrog_log("frontend theme open failed name=%s path=%s errno=%d\n",
            fe->theme_name, path, errno);
      }
   }
   dir_ms = unifrog_perf_time_ms() - t0;
   style.theme_chrome = dir_theme_loaded ? 1u : 0u;
   unifrog_exception_activity_set(UNIFROG_ACTIVITY_PHASE_FRONTEND_START,
      unifrog_exception_activity_hash("theme.assets"),
      unifrog_exception_activity_hash(fe->theme_name),
      (uint32_t)(fe->theme_alternate ? 1 : 0));
   frontend_loading_show(fe, "Loading Theme", fe->theme_name, "assets", 35);
   base_theme = theme;
   base_style = style;
   if (dir_theme_loaded) {
      style_theme = base_theme;
      fe->list_style = base_style;
      t0 = unifrog_perf_time_ms();
      if (load_frontend_module_from_base(fe, fe->theme_name, "muxplore",
          &base_theme, &base_style, &style_theme, &fe->list_style) == 0) {
         theme = style_theme;
         style = fe->list_style;
      }
   }
   if (fe->theme_alternate) {
      uint16_t tmp = theme.focus;

      theme.focus = theme.accent;
      theme.accent = tmp;
      alternate_style(&style);
   }
   fe->active_theme = theme;
   fe->base_theme = base_theme;
   fe->active_style = style;
   fe->base_style = base_style;
   fe->dir_theme_loaded = dir_theme_loaded;
   if (!dir_theme_loaded)
      fe->list_style = style;
   else if (fe->theme_alternate)
      alternate_style(&fe->list_style);
   fe->applied_style_id = -1;
   t0 = unifrog_perf_time_ms();
   for (unsigned i = 0; i <= UNIFROG_FRONTEND_LVGL_VISUAL; i++) {
      fe->screen_style[i] = style;
      fe->screen_style_valid[i] = dir_theme_loaded ? 0 : 1;
   }
   for (unsigned i = 0; i <= FRONTEND_VIEW_SYSINFO; i++) {
      fe->view_style[i] = fe->list_style;
      fe->view_style_valid[i] = dir_theme_loaded ? 0 : 1;
   }
   screens_ms = unifrog_perf_time_ms() - t0;
   unifrog_exception_activity_set(UNIFROG_ACTIVITY_PHASE_FRONTEND_START,
      unifrog_exception_activity_hash("theme.cache"),
      unifrog_exception_activity_hash(fe->theme_name),
      (uint32_t)(fe->theme_alternate ? 1 : 0));
   frontend_loading_show(fe, "Loading Theme", fe->theme_name, "cache", 65);
   t0 = unifrog_perf_time_ms();
   {
      char cache_key[64];

      snprintf(cache_key, sizeof(cache_key), "%s:%d", fe->theme_name,
         fe->theme_alternate ? 1 : 0);
      if (strcmp(fe->resource_cache_key, cache_key) != 0) {
         unifrog_frontend_lvgl_clear_resource_cache();
         unifrog_text_copy(fe->resource_cache_key,
            sizeof(fe->resource_cache_key), cache_key);
      }
   }
   unifrog_frontend_lvgl_preload_style_images(&fe->active_style);
   unifrog_frontend_lvgl_preload_style_images(&fe->list_style);
   load_theme_font(fe);
   preload_ms = unifrog_perf_time_ms() - t0;
   fe->theme = &fe->active_theme;
   t0 = unifrog_perf_time_ms();
   unifrog_exception_activity_set(UNIFROG_ACTIVITY_PHASE_FRONTEND_START,
      unifrog_exception_activity_hash("theme.apply"),
      unifrog_exception_activity_hash(fe->theme_name),
      (uint32_t)(fe->theme_alternate ? 1 : 0));
   frontend_loading_show(fe, "Loading Theme", fe->theme_name, "apply", 90);
   apply_frontend_style(fe, -2, &fe->active_style);
   apply_ms = unifrog_perf_time_ms() - t0;
   unifrog_log("frontend theme load done name=%s dir=%d ms=%u dir_ms=%u list_ms=%u screens_ms=%u preload_ms=%u apply_ms=%u active_text=%04x/%04x active_alpha=%u/%u list_text=%04x/%04x list_alpha=%u/%u\n",
      fe->theme_name, dir_theme_loaded,
      (unsigned)(unifrog_perf_time_ms() - start_ms),
      (unsigned)dir_ms, (unsigned)list_ms, (unsigned)screens_ms,
      (unsigned)preload_ms, (unsigned)apply_ms,
      fe->active_style.list_text, fe->active_style.list_focus_text,
      fe->active_style.list_text_alpha,
      fe->active_style.list_focus_text_alpha, fe->list_style.list_text,
      fe->list_style.list_focus_text, fe->list_style.list_text_alpha,
      fe->list_style.list_focus_text_alpha);
   unifrog_text_copy(fe->loaded_theme_name, sizeof(fe->loaded_theme_name),
      fe->theme_name);
   unifrog_text_copy(fe->loaded_theme_language,
      sizeof(fe->loaded_theme_language), active_language_label(fe));
   fe->loaded_theme_alternate = fe->theme_alternate ? 1 : 0;
   fe->theme_loaded = 1;
   unifrog_exception_activity_clear();
   frontend_invalidate_draw(fe);
}

static void ensure_data_dirs(void)
{
   if (!unifrog_log_disk_writes_enabled()) {
      unifrog_log("frontend ensure_data_dirs disabled reason=log_disk_disabled\n");
      return;
   }
   (void)mkdir(FRONTEND_DATA_ROOT, 0777);
   (void)mkdir(FRONTEND_DATA_ROOT "/saves", 0777);
   (void)mkdir(FRONTEND_DATA_ROOT "/cache", 0777);
   (void)mkdir(UNIFROG_LOG_ROOT, 0777);
   (void)mkdir(UNIFROG_REPORT_ROOT, 0777);
   (void)mkdir(UNIFROG_SCREENSHOT_ROOT, 0777);
   (void)mkdir(FRONTEND_SCRIPT_ROOT, 0777);
   (void)mkdir(FRONTEND_ARCHIVE_ROOT, 0777);
   (void)mkdir(FRONTEND_THEME_ROOT, 0777);
   (void)mkdir(FRONTEND_LANGUAGE_ROOT, 0777);
   (void)mkdir(FRONTEND_FIRMWARE_ROOT, 0777);
   (void)mkdir(FRONTEND_UPDATE_ROOT, 0777);
   (void)mkdir(FRONTEND_VERSION_ROOT, 0777);
}

static unsigned count_visible_dir_entries(const char *path)
{
   DIR *dir;
   struct dirent *entry;
   unsigned count = 0;

   dir = opendir(path);
   if (!dir)
      return 0;
   while ((entry = readdir(dir)) != NULL) {
      if (entry->d_name[0] != '.')
         count++;
   }
   closedir(dir);
   return count;
}

static unsigned count_content_dir_entries(const char *path, int show_hidden,
   int mixed_content)
{
   DIR *dir;
   struct dirent *entry;
   unsigned count = 0;

   dir = opendir(path);
   if (!dir)
      return 0;
   while ((entry = readdir(dir)) != NULL) {
      char full[FRONTEND_MAX_PATH];

      if (!show_hidden && entry->d_name[0] == '.')
         continue;
      if (path_join(full, sizeof(full), path, entry->d_name) != 0)
         continue;
      if (entry->d_type == DT_DIR || catalog_for_path(full) ||
          (mixed_content && is_media_file(full)))
         count++;
   }
   closedir(dir);
   return count;
}

static const char *on_off_label(int value)
{
   return value ? "on" : "off";
}

static void reset_items(struct native_frontend *fe, const char *title)
{
   fe->item_count = 0;
   fe->selected = 0;
   fe->scroll = 0;
   fe->item_generation++;
   fe->current_dir[0] = '\0';
   unifrog_text_copy(fe->title, sizeof(fe->title),
      tr(fe, title ? title : "muOS"));
   fe->needs_draw = 1;
}

static struct frontend_item *add_item(struct native_frontend *fe, const char *name,
   const char *meta, enum frontend_item_kind kind, const char *path,
   const char *core)
{
   struct frontend_item *item;

   if (fe->item_count >= FRONTEND_MAX_ITEMS)
      return NULL;
   item = &fe->items[fe->item_count++];
   memset(item, 0, sizeof(*item));
   unifrog_text_copy(item->name, sizeof(item->name),
      tr(fe, name ? name : ""));
   unifrog_text_copy(item->meta, sizeof(item->meta), meta ? meta : "");
   unifrog_text_copy(item->path, sizeof(item->path), path ? path : "");
   unifrog_text_copy(item->core, sizeof(item->core), core ? core : "");
   item->kind = kind;
   return item;
}

static int item_compare(const void *a, const void *b)
{
   const struct frontend_item *ia = a;
   const struct frontend_item *ib = b;
   int cmp;

   if (is_back_item(ia) && !is_back_item(ib))
      return -1;
   if (!is_back_item(ia) && is_back_item(ib))
      return 1;
   if (ia->kind == FRONTEND_ITEM_DIR && ib->kind != FRONTEND_ITEM_DIR)
      return -1;
   if (ia->kind != FRONTEND_ITEM_DIR && ib->kind == FRONTEND_ITEM_DIR)
      return 1;
   cmp = natural_name_compare(ia->name, ib->name);
   return frontend_sort_desc ? -cmp : cmp;
}

static void sort_items(struct native_frontend *fe)
{
   frontend_sort_desc = fe ? fe->sort_desc : 0;
   qsort(fe->items, fe->item_count, sizeof(fe->items[0]), item_compare);
}

static void clamp_selection(struct native_frontend *fe)
{
   if (fe->item_count == 0) {
      fe->selected = 0;
      fe->scroll = 0;
      return;
   }
   if (fe->selected >= fe->item_count)
      fe->selected = fe->item_count - 1u;
   if (fe->scroll > fe->selected)
      fe->scroll = fe->selected;
   if (fe->selected >= fe->scroll + FRONTEND_ROWS)
      fe->scroll = fe->selected - FRONTEND_ROWS + 1u;
}

static void log_selection(struct native_frontend *fe, const char *reason)
{
   struct frontend_item *item;
   int hot_repeat;
   int should_log;

   if (fe->selected >= fe->item_count)
      return;
   hot_repeat = reason &&
      (strcmp(reason, "up") == 0 || strcmp(reason, "down") == 0);
   should_log = !hot_repeat || fe->view != fe->nav_log_last_view ||
      fe->selected == 0 || fe->selected + 1u == fe->item_count ||
      fe->selected / FRONTEND_NAV_LOG_STEP !=
         fe->nav_log_last_selected / FRONTEND_NAV_LOG_STEP;
   fe->nav_log_last_view = fe->view;
   fe->nav_log_last_selected = fe->selected;
   if (!should_log)
      return;
   item = &fe->items[fe->selected];
   if (hot_repeat) {
      unifrog_log("frontend nav %s view=%d title=%s selected=%u/%u "
         "name=%s meta=%s kind=%d\n",
         reason ? reason : "select", fe->view, fe->title,
         fe->selected + 1u, fe->item_count, item->name, item->meta,
         item->kind);
   } else {
      unifrog_log("frontend nav %s view=%d title=%s dir=%s selected=%u/%u "
         "name=%s meta=%s path=%s kind=%d core=%s\n",
         reason ? reason : "select", fe->view, fe->title, fe->current_dir,
         fe->selected + 1u, fe->item_count, item->name, item->meta,
         item->path, item->kind, item->core);
   }
}

static void log_item_sample(struct native_frontend *fe, const char *tag)
{
   unsigned limit = fe->item_count < 8u ? fe->item_count : 8u;

   for (unsigned i = 0; i < limit; i++) {
      struct frontend_item *item = &fe->items[i];

      unifrog_log("frontend %s item=%u/%u name=%s meta=%s path=%s kind=%d core=%s\n",
         tag ? tag : "items", i + 1u, fe->item_count, item->name,
         item->meta, item->path, item->kind, item->core);
   }
}

static void nav_reset(struct native_frontend *fe)
{
   fe->nav_count = 0;
}

static void set_parent_view(struct native_frontend *fe)
{
   if (fe->view_stack_count >= FRONTEND_NAV_MAX) {
      memmove(fe->view_stack, fe->view_stack + 1,
         (FRONTEND_NAV_MAX - 1u) * sizeof(fe->view_stack[0]));
      memmove(fe->view_stack_selected, fe->view_stack_selected + 1,
         (FRONTEND_NAV_MAX - 1u) * sizeof(fe->view_stack_selected[0]));
      memmove(fe->view_stack_scroll, fe->view_stack_scroll + 1,
         (FRONTEND_NAV_MAX - 1u) * sizeof(fe->view_stack_scroll[0]));
      fe->view_stack_count = FRONTEND_NAV_MAX - 1u;
   }
   fe->view_stack[fe->view_stack_count] = fe->view;
   fe->view_stack_selected[fe->view_stack_count] = fe->selected;
   fe->view_stack_scroll[fe->view_stack_count] = fe->scroll;
   fe->view_stack_count++;
   fe->parent_view = fe->view;
   fe->has_parent_view = 1;
}

static void clear_parent_view(struct native_frontend *fe)
{
   fe->has_parent_view = 0;
   fe->parent_view = FRONTEND_VIEW_LAUNCH;
   fe->view_stack_count = 0;
}

static void nav_push(struct native_frontend *fe)
{
   if ((fe->view != FRONTEND_VIEW_EXPLORE && fe->view != FRONTEND_VIEW_FIRMWARE &&
        fe->view != FRONTEND_VIEW_SCRIPTS) ||
       !fe->current_dir[0])
      return;
   if (fe->nav_count >= FRONTEND_NAV_MAX) {
      memmove(fe->nav_path[0], fe->nav_path[1],
         (FRONTEND_NAV_MAX - 1u) * sizeof(fe->nav_path[0]));
      memmove(fe->nav_selected, fe->nav_selected + 1,
         (FRONTEND_NAV_MAX - 1u) * sizeof(fe->nav_selected[0]));
      fe->nav_count = FRONTEND_NAV_MAX - 1u;
   }
   unifrog_text_copy(fe->nav_path[fe->nav_count],
      sizeof(fe->nav_path[0]), fe->current_dir);
   fe->nav_selected[fe->nav_count] = fe->selected;
   fe->nav_count++;
   unifrog_log("frontend nav push view=%d depth=%u path=%s selected=%u\n",
      fe->view, fe->nav_count, fe->current_dir, fe->selected);
}

static void set_status(struct native_frontend *fe, const char *fmt, ...)
{
   va_list ap;

   va_start(ap, fmt);
   vsnprintf(fe->status, sizeof(fe->status), fmt, ap);
   va_end(ap);
   fe->needs_draw = 1;
}

static void fast_probe_progress_cb(void *userdata, const char *line1,
   const char *line2)
{
   struct native_frontend *fe = userdata;

   if (!fe)
      return;
   set_status(fe, "%s%s%s", line1 ? line1 : "fast SD probe",
      line2 && line2[0] ? ": " : "", line2 && line2[0] ? line2 : "");
   draw(fe);
}

static const char *storage_profile_label(const char *profile)
{
   if (!profile || !profile[0] || strcmp(profile, "boot") == 0)
      return "boot profile";
   if (strcmp(profile, "wide1") == 0)
      return "4-bit 1 MHz";
   if (strcmp(profile, "wide2") == 0)
      return "4-bit 2 MHz";
   if (strcmp(profile, "wide4") == 0)
      return "4-bit 4 MHz";
   if (strcmp(profile, "wide8") == 0)
      return "4-bit 8 MHz";
   if (strcmp(profile, "wide10") == 0)
      return "4-bit 10 MHz";
   if (strcmp(profile, "wide12") == 0)
      return "4-bit 12 MHz";
   if (strcmp(profile, "wide14") == 0)
      return "4-bit 14 MHz";
   if (strcmp(profile, "wide16") == 0)
      return "4-bit 16 MHz";
   if (strcmp(profile, "wide18") == 0)
      return "4-bit 18 MHz";
   if (strcmp(profile, "wide20") == 0)
      return "4-bit 20 MHz";
   if (strcmp(profile, "wide22") == 0)
      return "4-bit 22 MHz";
   if (strcmp(profile, "wide24") == 0)
      return "4-bit 24 MHz";
   if (strcmp(profile, "wide25") == 0)
      return "4-bit 25 MHz";
   return profile;
}

static unsigned storage_profile_index(const char *profile)
{
   for (unsigned i = 0; i < ARRAY_SIZE(storage_config_profiles); i++) {
      if (strcmp(profile ? profile : "", storage_config_profiles[i]) == 0)
         return i;
   }
   return 0;
}

static void normalize_storage_profile(struct native_frontend *fe)
{
   if (!fe->storage_profile[0] ||
       storage_profile_index(fe->storage_profile) == 0)
      unifrog_text_copy(fe->storage_profile, sizeof(fe->storage_profile),
         "boot");
}

static int apply_storage_profile(struct native_frontend *fe, const char *reason)
{
   char detail[192];
   size_t old_auto_flush;
   int ret;

   normalize_storage_profile(fe);
   old_auto_flush = unifrog_log_auto_flush_bytes();
   unifrog_log_set_auto_flush_bytes(0);
   unifrog_platform_set_storage_log_suspended(1);
   if (strcmp(fe->storage_profile, "boot") == 0) {
      ret = unifrog_platform_sd_restore_boot(4, 100, detail, sizeof(detail));
   } else {
      ret = unifrog_platform_sd_apply_profile(fe->storage_profile, 4, 100,
         detail, sizeof(detail));
   }
   unifrog_platform_set_storage_log_suspended(0);
   unifrog_log_set_auto_flush_bytes(old_auto_flush);
   unifrog_log("frontend storage_profile apply reason=%s profile=%s ret=%d detail=%s\n",
      reason ? reason : "", fe->storage_profile, ret, detail);
   (void)unifrog_log_flush();
   set_status(fe, "SD %s %d", fe->storage_profile, ret);
   return ret;
}

static const char *active_storage_label(void)
{
   return storage_profile_label(unifrog_platform_sd_active_profile());
}

static void save_settings(struct native_frontend *fe)
{
   FILE *file;
   char tmp[FRONTEND_MAX_PATH];

   ensure_data_dirs();
   snprintf(tmp, sizeof(tmp), "%s.tmp", FRONTEND_SETTINGS_PATH);
   file = fopen(tmp, "wb");
   if (!file)
      return;
   fprintf(file, "audio=%d\n", fe->run_options.audio_enabled);
   fprintf(file, "cpu=%u\n", fe->run_options.scpu_mhz);
   fprintf(file, "frameskip=%d\n", fe->run_options.frameskip);
   fprintf(file, "display=%d\n", fe->run_options.display_mode);
   fprintf(file, "gain=%u\n", fe->run_options.audio_gain);
   fprintf(file, "ge_clock=%d\n", fe->run_options.ge_clock);
   fprintf(file, "backlight=%d\n", fe->run_options.backlight_level);
   fprintf(file, "keymap=%d\n", fe->run_options.input_profile);
   fprintf(file, "state_slot=%u\n",
      clamp_state_slot(fe->run_options.state_slot));
   fprintf(file, "state_auto_load=%d\n",
      fe->run_options.state_auto_load ? 1 : 0);
   fprintf(file, "state_auto_save=%d\n",
      fe->run_options.state_auto_save ? 1 : 0);
   fprintf(file, "sort_desc=%d\n", fe->sort_desc);
   fprintf(file, "show_hidden=%d\n", fe->show_hidden);
   fprintf(file, "folder_counts=%d\n", fe->folder_counts);
   fprintf(file, "mixed_content=%d\n", fe->mixed_content);
   fprintf(file, "display_empty_folder=%d\n", fe->display_empty_folder);
   fprintf(file, "menu_counter_folder=%d\n", fe->menu_counter_folder);
   fprintf(file, "menu_counter_file=%d\n", fe->menu_counter_file);
   fprintf(file, "content_collect=%d\n", fe->content_collect);
   fprintf(file, "content_history=%d\n", fe->content_history);
   fprintf(file, "clock_enabled=%d\n", fe->clock_enabled);
   fprintf(file, "title_include_root=%d\n", fe->title_include_root);
   fprintf(file, "theme_alternate=%d\n", fe->theme_alternate);
   fprintf(file, "boxart_hidden=%d\n", fe->boxart_hidden);
   fprintf(file, "launch_splash=%d\n", fe->launch_splash);
   fprintf(file, "sound_enabled=%d\n", fe->sound_enabled);
   fprintf(file, "log_flush_every=%d\n", fe->log_flush_every);
   fprintf(file, "language=%d\n", fe->language_index);
   fprintf(file, "language_name=%s\n", active_language_label(fe));
   fprintf(file, "theme_name=%s\n", active_theme_label(fe));
   fprintf(file, "storage_profile=%s\n", fe->storage_profile);
   fprintf(file, "rom_root=%s\n", frontend_rom_root(fe));
   fprintf(file, "rom_root_label=%s\n", frontend_rom_root_label(fe));
   fprintf(file, "last_path=%s\n", fe->last_path);
   fprintf(file, "last_core=%s\n", fe->last_core);
   if (fclose(file) == 0) {
      unlink(FRONTEND_SETTINGS_PATH);
      (void)rename(tmp, FRONTEND_SETTINGS_PATH);
   } else {
      unlink(tmp);
   }
}

static void load_settings(struct native_frontend *fe)
{
   FILE *file = fopen(FRONTEND_SETTINGS_PATH, "rb");
   char line[FRONTEND_MAX_LINE];

   if (!file)
      return;
   while (fgets(line, sizeof(line), file)) {
      const char *value;

      strip_eol(line);
      if ((value = read_key_value(line, "audio")) != NULL)
         fe->run_options.audio_enabled = parse_int(value,
            fe->run_options.audio_enabled) ? 1 : 0;
      else if ((value = read_key_value(line, "cpu")) != NULL)
         fe->run_options.scpu_mhz = (unsigned)parse_int(value,
            (int)fe->run_options.scpu_mhz);
      else if ((value = read_key_value(line, "frameskip")) != NULL)
         fe->run_options.frameskip = parse_int(value,
            fe->run_options.frameskip);
      else if ((value = read_key_value(line, "display")) != NULL)
         fe->run_options.display_mode = parse_int(value,
            fe->run_options.display_mode);
      else if ((value = read_key_value(line, "gain")) != NULL)
         fe->run_options.audio_gain = (unsigned)parse_int(value,
            (int)fe->run_options.audio_gain);
      else if ((value = read_key_value(line, "ge_clock")) != NULL)
         fe->run_options.ge_clock = parse_int(value,
            fe->run_options.ge_clock);
      else if ((value = read_key_value(line, "backlight")) != NULL)
         fe->run_options.backlight_level = parse_int(value,
            fe->run_options.backlight_level);
      else if ((value = read_key_value(line, "keymap")) != NULL)
         fe->run_options.input_profile = parse_int(value,
            fe->run_options.input_profile);
      else if ((value = read_key_value(line, "state_slot")) != NULL)
         fe->run_options.state_slot = clamp_state_slot((unsigned)
            parse_int(value, (int)fe->run_options.state_slot));
      else if ((value = read_key_value(line, "state_auto_load")) != NULL)
         fe->run_options.state_auto_load = parse_int(value,
            fe->run_options.state_auto_load) ? 1 : 0;
      else if ((value = read_key_value(line, "state_auto_save")) != NULL)
         fe->run_options.state_auto_save = parse_int(value,
            fe->run_options.state_auto_save) ? 1 : 0;
      else if ((value = read_key_value(line, "sort_desc")) != NULL)
         fe->sort_desc = parse_int(value, fe->sort_desc) ? 1 : 0;
      else if ((value = read_key_value(line, "show_hidden")) != NULL)
         fe->show_hidden = parse_int(value, fe->show_hidden) ? 1 : 0;
      else if ((value = read_key_value(line, "folder_counts")) != NULL)
         fe->folder_counts = parse_int(value, fe->folder_counts) ? 1 : 0;
      else if ((value = read_key_value(line, "mixed_content")) != NULL)
         fe->mixed_content = parse_int(value, fe->mixed_content) ? 1 : 0;
      else if ((value = read_key_value(line, "display_empty_folder")) != NULL)
         fe->display_empty_folder = parse_int(value,
            fe->display_empty_folder) ? 1 : 0;
      else if ((value = read_key_value(line, "menu_counter_folder")) != NULL)
         fe->menu_counter_folder = parse_int(value,
            fe->menu_counter_folder) ? 1 : 0;
      else if ((value = read_key_value(line, "menu_counter_file")) != NULL)
         fe->menu_counter_file = parse_int(value,
            fe->menu_counter_file) ? 1 : 0;
      else if ((value = read_key_value(line, "content_collect")) != NULL)
         fe->content_collect = parse_int(value, fe->content_collect) ? 1 : 0;
      else if ((value = read_key_value(line, "content_history")) != NULL)
         fe->content_history = parse_int(value, fe->content_history) ? 1 : 0;
      else if ((value = read_key_value(line, "clock_enabled")) != NULL)
         fe->clock_enabled = parse_int(value, fe->clock_enabled) ? 1 : 0;
      else if ((value = read_key_value(line, "title_include_root")) != NULL)
         fe->title_include_root = parse_int(value,
            fe->title_include_root) ? 1 : 0;
      else if ((value = read_key_value(line, "theme_alternate")) != NULL)
         fe->theme_alternate = parse_int(value, fe->theme_alternate) ? 1 : 0;
      else if ((value = read_key_value(line, "boxart_hidden")) != NULL)
         fe->boxart_hidden = parse_int(value, fe->boxart_hidden) ? 1 : 0;
      else if ((value = read_key_value(line, "launch_splash")) != NULL)
         fe->launch_splash = parse_int(value, fe->launch_splash) ? 1 : 0;
      else if ((value = read_key_value(line, "sound_enabled")) != NULL)
         fe->sound_enabled = parse_int(value, fe->sound_enabled) ? 1 : 0;
      else if ((value = read_key_value(line, "log_flush_every")) != NULL)
         fe->log_flush_every = parse_int(value, fe->log_flush_every) ? 1 : 0;
      else if ((value = read_key_value(line, "language")) != NULL)
         fe->language_index = parse_int(value, fe->language_index);
      else if ((value = read_key_value(line, "language_name")) != NULL)
         unifrog_text_copy(fe->language_name, sizeof(fe->language_name),
            value);
      else if ((value = read_key_value(line, "theme_name")) != NULL)
         unifrog_text_copy(fe->theme_name, sizeof(fe->theme_name), value);
      else if ((value = read_key_value(line, "storage_profile")) != NULL)
         unifrog_text_copy(fe->storage_profile, sizeof(fe->storage_profile),
            value);
      else if ((value = read_key_value(line, "rom_root")) != NULL) {
         char normalized[FRONTEND_MAX_PATH];

         if (frontend_normalize_path(normalized, sizeof(normalized),
             value) == 0)
            unifrog_text_copy(fe->rom_root, sizeof(fe->rom_root),
               normalized);
      }
      else if ((value = read_key_value(line, "rom_roots")) != NULL) {
         char first_root[FRONTEND_MAX_PATH];
         char normalized[FRONTEND_MAX_PATH];
         char *sep;

         unifrog_text_copy(first_root, sizeof(first_root), value);
         sep = strchr(first_root, '|');
         if (sep)
            *sep = '\0';
         if (frontend_normalize_path(normalized, sizeof(normalized),
             first_root) == 0)
            unifrog_text_copy(fe->rom_root, sizeof(fe->rom_root),
               normalized);
      }
      else if ((value = read_key_value(line, "rom_root_label")) != NULL)
         unifrog_text_copy(fe->rom_root_label, sizeof(fe->rom_root_label),
            value);
      else if ((value = read_key_value(line, "last_path")) != NULL)
         unifrog_text_copy(fe->last_path, sizeof(fe->last_path), value);
      else if ((value = read_key_value(line, "last_core")) != NULL)
         unifrog_text_copy(fe->last_core, sizeof(fe->last_core), value);
   }
   fclose(file);
   fe->run_options.state_slot = clamp_state_slot(fe->run_options.state_slot);
   normalize_storage_profile(fe);
}

static void record_history(struct native_frontend *fe, const char *path,
   const char *core)
{
   FILE *in;
   FILE *out;
   char tmp[FRONTEND_MAX_PATH];
   char old[FRONTEND_HISTORY_MAX][FRONTEND_MAX_LINE];
   unsigned old_count = 0;

   if (!path || !path[0])
      return;
   if (!fe->content_history) {
      unifrog_text_copy(fe->last_path, sizeof(fe->last_path), path);
      unifrog_text_copy(fe->last_core, sizeof(fe->last_core), core ? core : "");
      save_settings(fe);
      return;
   }
   ensure_data_dirs();
   unifrog_text_copy(fe->last_path, sizeof(fe->last_path), path);
   unifrog_text_copy(fe->last_core, sizeof(fe->last_core), core ? core : "");

   in = fopen(FRONTEND_HISTORY_PATH, "rb");
   if (in) {
      char line[FRONTEND_MAX_LINE];

      while (fgets(line, sizeof(line), in) && old_count < ARRAY_SIZE(old)) {
         char *sep;

         strip_eol(line);
         sep = strchr(line, '|');
         if (sep)
            *sep = '\0';
         if (strcmp(line, path) == 0)
            continue;
         if (sep)
            *sep = '|';
         unifrog_text_copy(old[old_count++], sizeof(old[0]), line);
      }
      fclose(in);
   }

   snprintf(tmp, sizeof(tmp), "%s.tmp", FRONTEND_HISTORY_PATH);
   out = fopen(tmp, "wb");
   if (!out)
      return;
   fprintf(out, "%s|%s\n", path, core ? core : "");
   for (unsigned i = 0; i < old_count && i + 1u < ARRAY_SIZE(old); i++)
      fprintf(out, "%s\n", old[i]);
   if (fclose(out) == 0) {
      unlink(FRONTEND_HISTORY_PATH);
      (void)rename(tmp, FRONTEND_HISTORY_PATH);
   } else {
      unlink(tmp);
   }
   save_settings(fe);
}

static int favorite_matches(const char *line, const char *path)
{
   char scratch[FRONTEND_MAX_LINE];
   char *sep;

   unifrog_text_copy(scratch, sizeof(scratch), line ? line : "");
   sep = strchr(scratch, '|');
   if (sep)
      *sep = '\0';
   return path && strcmp(scratch, path) == 0;
}

static int is_favorite(const char *path)
{
   FILE *file;
   char line[FRONTEND_MAX_LINE];

   if (!path || !path[0])
      return 0;
   file = fopen(FRONTEND_FAVORITES_PATH, "rb");
   if (!file)
      return 0;
   while (fgets(line, sizeof(line), file)) {
      strip_eol(line);
      if (favorite_matches(line, path)) {
         fclose(file);
         return 1;
      }
   }
   fclose(file);
   return 0;
}

static void toggle_favorite(struct native_frontend *fe,
   const struct frontend_item *item)
{
   FILE *in;
   FILE *out;
   char old[FRONTEND_FAVORITES_MAX][FRONTEND_MAX_LINE];
   char tmp[FRONTEND_MAX_PATH];
   unsigned old_count = 0;
   int removed = 0;

   if (!item || !item->path[0])
      return;
   if (!fe->content_collect) {
      set_status(fe, "collection disabled");
      return;
   }
   ensure_data_dirs();
   in = fopen(FRONTEND_FAVORITES_PATH, "rb");
   if (in) {
      char line[FRONTEND_MAX_LINE];

      while (fgets(line, sizeof(line), in) && old_count < ARRAY_SIZE(old)) {
         strip_eol(line);
         if (favorite_matches(line, item->path)) {
            removed = 1;
            continue;
         }
         unifrog_text_copy(old[old_count++], sizeof(old[0]), line);
      }
      fclose(in);
   }

   snprintf(tmp, sizeof(tmp), "%s.tmp", FRONTEND_FAVORITES_PATH);
   out = fopen(tmp, "wb");
   if (!out)
      return;
   if (!removed)
      fprintf(out, "%s|%s\n", item->path, item->core);
   for (unsigned i = 0; i < old_count; i++)
      fprintf(out, "%s\n", old[i]);
   if (fclose(out) == 0) {
      unlink(FRONTEND_FAVORITES_PATH);
      (void)rename(tmp, FRONTEND_FAVORITES_PATH);
      set_status(fe, "%s favorite", removed ? "removed" : "added");
   } else {
      unlink(tmp);
   }
}

static void show_launch(struct native_frontend *fe)
{
   reset_items(fe, "muOS");
   fe->view = FRONTEND_VIEW_LAUNCH;
   nav_reset(fe);
   add_item(fe, "Explore", "content", FRONTEND_ITEM_ACTION, "explore", NULL);
   add_item(fe, "Collection", "favorites", FRONTEND_ITEM_ACTION, "favorites", NULL);
   add_item(fe, "History", "recent", FRONTEND_ITEM_ACTION, "history", NULL);
   add_item(fe, "Apps", "native", FRONTEND_ITEM_ACTION, "apps", NULL);
   add_item(fe, "Info", "device", FRONTEND_ITEM_ACTION, "info", NULL);
   add_item(fe, "Config", "settings", FRONTEND_ITEM_ACTION, "config", NULL);
   add_item(fe, "Reboot", "system", FRONTEND_ITEM_ACTION, "reboot", NULL);
   add_item(fe, "Shutdown", "not supported", FRONTEND_ITEM_ACTION, "shutdown", NULL);
   fe->status[0] = '\0';
   fe->needs_draw = 1;
}

static void add_dir_entry(struct native_frontend *fe, const char *dir,
   const char *name, unsigned char type)
{
   char full[FRONTEND_MAX_PATH];
   struct stat st;
   const struct frontend_catalog *cat;
   int known_dir = type == DT_DIR;
   int known_file = type == DT_REG;

   if (!name || (!fe->show_hidden && name[0] == '.'))
      return;
   if (path_join(full, sizeof(full), dir, name) != 0)
      return;
   cat = catalog_for_path(full);
   if (cat || (fe->mixed_content && is_media_file(full))) {
      if (known_dir) {
         if (stat(full, &st) == 0) {
            known_dir = S_ISDIR(st.st_mode);
            known_file = S_ISREG(st.st_mode);
         }
      }
      if (!known_dir) {
         if (cat)
            add_item(fe, name, cat->core, FRONTEND_ITEM_GAME, full, cat->core);
         else
            add_item(fe, name, "media", FRONTEND_ITEM_MEDIA, full, NULL);
         return;
      }
   }
   if (!known_dir && !known_file) {
      if (stat(full, &st) != 0)
         return;
      known_dir = S_ISDIR(st.st_mode);
      known_file = S_ISREG(st.st_mode);
   }
   if (known_dir) {
      char meta[32];

      if (fe->folder_counts || !fe->display_empty_folder) {
         unsigned count = count_content_dir_entries(full, fe->show_hidden,
            fe->mixed_content);

         if (!fe->display_empty_folder && count == 0)
            return;
         snprintf(meta, sizeof(meta), "%u items", count);
         add_item(fe, name, meta, FRONTEND_ITEM_DIR, full, NULL);
      } else {
         add_item(fe, name, "folder", FRONTEND_ITEM_DIR, full, NULL);
      }
      return;
   }
   if (!known_file)
      return;
}

static void add_rom_dir_entry(struct native_frontend *fe, const char *dir,
   const char *name, unsigned char type)
{
   char full[FRONTEND_MAX_PATH];
   struct stat st;
   const struct frontend_catalog *cat;
   int known_dir = type == DT_DIR;
   int known_file = type == DT_REG;

   if (!name || (!fe->show_hidden && name[0] == '.'))
      return;
   if (path_join(full, sizeof(full), dir, name) != 0)
      return;
   if (!known_dir && !known_file) {
      if (stat(full, &st) != 0)
         return;
      known_dir = S_ISDIR(st.st_mode);
      known_file = S_ISREG(st.st_mode);
   }
   if (known_dir) {
      char meta[32];

      if (fe->folder_counts) {
         unsigned count = count_content_dir_entries(full, fe->show_hidden, 1);

         snprintf(meta, sizeof(meta), "%u items", count);
      } else {
         snprintf(meta, sizeof(meta), "folder");
      }
      add_item(fe, name, meta, FRONTEND_ITEM_DIR, full, NULL);
      return;
   }
   if (!known_file)
      return;
   cat = catalog_for_path(full);
   if (cat)
      add_item(fe, name, cat->core, FRONTEND_ITEM_GAME, full, cat->core);
   else if (is_media_file(full))
      add_item(fe, name, "media", FRONTEND_ITEM_MEDIA, full, NULL);
   else
      add_item(fe, name, "auto", FRONTEND_ITEM_GAME, full, NULL);
}

static void add_rom_system_entry(struct native_frontend *fe, const char *dir,
   const char *name, unsigned char type)
{
   char full[FRONTEND_MAX_PATH];
   struct stat st;
   int known_dir = type == DT_DIR;

   if (!name || (!fe->show_hidden && name[0] == '.') ||
       strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
      return;
   if (path_join(full, sizeof(full), dir, name) != 0)
      return;
   if (!known_dir) {
      if (stat(full, &st) != 0)
         return;
      known_dir = S_ISDIR(st.st_mode);
   }
   if (known_dir) {
      char meta[32];
      unsigned count = count_content_dir_entries(full, fe->show_hidden, 1);

      snprintf(meta, sizeof(meta), "%u games", count);
      add_item(fe, name, meta, FRONTEND_ITEM_DIR, full, NULL);
   }
}

static void add_firmware_dir_entry(struct native_frontend *fe, const char *dir,
   const char *name, unsigned char type)
{
   char full[FRONTEND_MAX_PATH];
   char rel[FRONTEND_MAX_PATH];
   struct stat st;
   int known_dir = type == DT_DIR;
   int known_file = type == DT_REG;

   if (!name || name[0] == '.')
      return;
   if (path_join(full, sizeof(full), dir, name) != 0)
      return;
   if (!known_dir && !known_file) {
      if (stat(full, &st) != 0)
         return;
      known_dir = S_ISDIR(st.st_mode);
      known_file = S_ISREG(st.st_mode);
   }
   if (known_dir) {
      add_item(fe, name, "folder", FRONTEND_ITEM_DIR, full, NULL);
      return;
   }
   if (!known_file || !is_asd_file(full))
      return;
   if (sd_relative_path(rel, sizeof(rel), full) != 0)
      return;
   add_item(fe, name,
      unifrog_boot_asd_path_supported(rel) ? "boot .asd" : "unsupported name",
      FRONTEND_ITEM_FIRMWARE, full, NULL);
}

static void add_script_dir_entry(struct native_frontend *fe, const char *dir,
   const char *name, unsigned char type)
{
   char full[FRONTEND_MAX_PATH];
   struct stat st;
   int known_dir = type == DT_DIR;
   int known_file = type == DT_REG;

   if (!name || name[0] == '.')
      return;
   if (path_join(full, sizeof(full), dir, name) != 0)
      return;
   if (!known_dir && !known_file) {
      if (stat(full, &st) != 0)
         return;
      known_dir = S_ISDIR(st.st_mode);
      known_file = S_ISREG(st.st_mode);
   }
   if (known_dir) {
      add_item(fe, name, "folder", FRONTEND_ITEM_DIR, full, NULL);
      return;
   }
   if (known_file && is_js_script_file(full))
      add_item(fe, name, "JS2300", FRONTEND_ITEM_SCRIPT, full, NULL);
}

static void show_rom_systems(struct native_frontend *fe)
{
   DIR *dir;
   struct dirent *entry;
   const char *root = frontend_rom_root(fe);
   uint32_t start_ms = unifrog_perf_time_ms();
   unsigned seen = 0;
   int limited = 0;

   reset_items(fe, frontend_rom_root_label(fe));
   fe->view = FRONTEND_VIEW_ROM_SYSTEMS;
   unifrog_text_copy(fe->current_dir, sizeof(fe->current_dir), root);
   add_item(fe, "..", "launcher", FRONTEND_ITEM_DIR, "", NULL);
   dir = opendir(root);
   if (!dir) {
      set_status(fe, "open failed: %s", root);
      unifrog_log("frontend rom_systems open_failed path=%s errno=%d\n",
         root, errno);
      return;
   }
   while ((entry = readdir(dir)) != NULL) {
      seen++;
      if (fe->item_count >= FRONTEND_MAX_ITEMS) {
         limited = 1;
         continue;
      }
      add_rom_system_entry(fe, root, entry->d_name, entry->d_type);
   }
   closedir(dir);
   sort_items(fe);
   set_status(fe, limited ? "%u/%u systems" : "%u systems",
      fe->item_count ? fe->item_count - 1u : 0u, seen);
   unifrog_log("frontend rom_systems root=%s seen=%u items=%u limited=%d ms=%lu\n",
      root, seen, fe->item_count, limited,
      (unsigned long)(unifrog_perf_time_ms() - start_ms));
   log_item_sample(fe, "rom_systems");
   log_selection(fe, "enter");
}

static void show_explore(struct native_frontend *fe, const char *path)
{
   DIR *dir;
   struct dirent *entry;
   uint32_t start_ms = unifrog_perf_time_ms();
   unsigned seen = 0;
   int limited = 0;

   reset_items(fe, fe->title_include_root ? path : frontend_rom_title(fe, path));
   fe->view = FRONTEND_VIEW_EXPLORE;
   unifrog_text_copy(fe->current_dir, sizeof(fe->current_dir), path);
   add_item(fe, "..", "back", FRONTEND_ITEM_DIR, "", NULL);
   if (is_content_file(path)) {
      set_status(fe, "content path; press A to launch");
      add_item(fe, basename_const(path), "content", FRONTEND_ITEM_GAME, path,
         catalog_for_path(path) ? catalog_for_path(path)->core : NULL);
      return;
   }
   dir = opendir(path);
   if (!dir) {
      set_status(fe, "open failed: %s", path);
      return;
   }
   while ((entry = readdir(dir)) != NULL) {
      seen++;
      if (fe->item_count >= FRONTEND_MAX_ITEMS) {
         limited = 1;
         continue;
      }
      if (frontend_path_is_inside_rom_root(fe, path))
         add_rom_dir_entry(fe, path, entry->d_name, entry->d_type);
      else
         add_dir_entry(fe, path, entry->d_name, entry->d_type);
   }
   closedir(dir);
   sort_items(fe);
   set_status(fe, limited ? "%u/%u entries" : "%u entries",
      fe->item_count ? fe->item_count - 1u : 0u, seen);
   unifrog_log("frontend explore path=%s seen=%u items=%u limited=%d ms=%lu\n",
      path, seen, fe->item_count, limited,
      (unsigned long)(unifrog_perf_time_ms() - start_ms));
   log_item_sample(fe, "explore");
   log_selection(fe, "enter");
}

static void show_firmware_browser(struct native_frontend *fe, const char *path)
{
   DIR *dir;
   struct dirent *entry;
   uint32_t start_ms = unifrog_perf_time_ms();
   unsigned seen = 0;
   int limited = 0;
   char title[64];

   snprintf(title, sizeof(title), "Firmware:%s",
      strcmp(path, FRONTEND_ROOT) == 0 ? "/" : basename_const(path));
   reset_items(fe, title);
   fe->view = FRONTEND_VIEW_FIRMWARE;
   unifrog_text_copy(fe->current_dir, sizeof(fe->current_dir), path);
   add_item(fe, "..", "back", FRONTEND_ITEM_DIR, "", NULL);

   dir = opendir(path);
   if (!dir) {
      set_status(fe, "open failed: %s", path);
      unifrog_log("frontend firmware open_failed path=%s errno=%d\n",
         path, errno);
      return;
   }
   while ((entry = readdir(dir)) != NULL) {
      seen++;
      if (fe->item_count >= FRONTEND_MAX_ITEMS) {
         limited = 1;
         continue;
      }
      add_firmware_dir_entry(fe, path, entry->d_name, entry->d_type);
   }
   closedir(dir);
   sort_items(fe);
   add_item(fe, "Reboot UniFrog", "restart", FRONTEND_ITEM_ACTION,
      "reboot", NULL);
   set_status(fe, limited ? "%u/%u entries" : "%u entries",
      fe->item_count ? fe->item_count - 2u : 0u, seen);
   unifrog_log("frontend firmware path=%s seen=%u items=%u limited=%d ms=%lu\n",
      path, seen, fe->item_count, limited,
      (unsigned long)(unifrog_perf_time_ms() - start_ms));
   log_item_sample(fe, "firmware");
   log_selection(fe, "enter");
}

static void show_script_browser(struct native_frontend *fe, const char *path)
{
   DIR *dir;
   struct dirent *entry;
   uint32_t start_ms = unifrog_perf_time_ms();
   unsigned seen = 0;
   int limited = 0;

   reset_items(fe, strcmp(path, FRONTEND_SCRIPT_ROOT) == 0 ? "Scripts" :
      basename_const(path));
   fe->view = FRONTEND_VIEW_SCRIPTS;
   unifrog_text_copy(fe->current_dir, sizeof(fe->current_dir), path);
   add_item(fe, "..", "back", FRONTEND_ITEM_DIR, "", NULL);

   dir = opendir(path);
   if (!dir) {
      set_status(fe, "open failed: %s", path);
      unifrog_log("frontend scripts open_failed path=%s errno=%d\n",
         path, errno);
      return;
   }
   while ((entry = readdir(dir)) != NULL) {
      seen++;
      if (fe->item_count >= FRONTEND_MAX_ITEMS) {
         limited = 1;
         continue;
      }
      add_script_dir_entry(fe, path, entry->d_name, entry->d_type);
   }
   closedir(dir);
   sort_items(fe);
   set_status(fe, limited ? "%u/%u scripts" : "%u scripts",
      fe->item_count ? fe->item_count - 1u : 0u, seen);
   unifrog_log("frontend scripts path=%s seen=%u items=%u limited=%d ms=%lu\n",
      path, seen, fe->item_count, limited,
      (unsigned long)(unifrog_perf_time_ms() - start_ms));
   log_item_sample(fe, "scripts");
   log_selection(fe, "enter");
}

static void show_file_list(struct native_frontend *fe, const char *title,
   const char *path, enum frontend_view view)
{
   FILE *file;
   char line[FRONTEND_MAX_LINE];

   reset_items(fe, title);
   fe->view = view;
   file = fopen(path, "rb");
   if (!file) {
      add_item(fe, "Back", "launcher", FRONTEND_ITEM_ACTION, "back", NULL);
      set_status(fe, "no entries");
      return;
   }
   while (fgets(line, sizeof(line), file) && fe->item_count < FRONTEND_MAX_ITEMS) {
      char *sep;
      char game[FRONTEND_MAX_PATH];
      char core[24];

      strip_eol(line);
      sep = strchr(line, '|');
      if (sep) {
         *sep = '\0';
         unifrog_text_copy(core, sizeof(core), sep + 1);
      } else {
         core[0] = '\0';
      }
      unifrog_text_copy(game, sizeof(game), line);
      if (game[0]) {
         const char *safe_core = safe_core_for_path(game, core);
         struct stat st;
         char meta[64];

         if (stat(game, &st) == 0)
            snprintf(meta, sizeof(meta), "%s",
               safe_core[0] ? safe_core : "auto");
         else
            snprintf(meta, sizeof(meta), "missing");
         add_item(fe, basename_const(game), meta,
            FRONTEND_ITEM_GAME, game, safe_core[0] ? safe_core : NULL);
      }
   }
   fclose(file);
   add_item(fe, "Back", "launcher", FRONTEND_ITEM_ACTION, "back", NULL);
   set_status(fe, "%u entries", fe->item_count ? fe->item_count - 1u : 0u);
}

static void show_open_with(struct native_frontend *fe,
   const struct frontend_item *item)
{
   char title[96];

   if (!fe || !item)
      return;
   fe->pending_open_item = *item;
   fe->pending_open_valid = 1;
   snprintf(title, sizeof(title), "Open:%.88s", item->name);
   reset_items(fe, title);
   fe->view = FRONTEND_VIEW_OPEN_WITH;
   if (item->kind == FRONTEND_ITEM_GAME) {
      char ids[16][UNIFROG_CORE_MODULE_ID_MAX];
      unsigned count = collect_core_candidates(item->path, ids);

      for (unsigned i = 0; i < count; i++)
         add_item(fe, ids[i], i == 0 ? "default core" : "compatible core",
            FRONTEND_ITEM_ACTION, "open_with_core", ids[i]);
      if (!count)
         add_item(fe, "Auto core", "launcher default",
            FRONTEND_ITEM_ACTION, "open_with_core", NULL);
   } else if (item->kind == FRONTEND_ITEM_MEDIA) {
      if (media_path_has_native_wav(item->path))
         add_item(fe, "Homemade WAV", "native low latency",
            FRONTEND_ITEM_ACTION, "open_with_media_native", NULL);
#if UNIFROG_HCRTOS_MEDIA_FIRMWARE
      add_item(fe, "HCPlayer Auto", "quiet unless audio is verified",
         FRONTEND_ITEM_ACTION, "open_with_media_hcplayer", NULL);
      add_item(fe, "HCPlayer Audio", "force speaker output",
         FRONTEND_ITEM_ACTION, "open_with_media_hcplayer_audio", NULL);
      add_item(fe, "HCPlayer Muted", "video or preview only",
         FRONTEND_ITEM_ACTION, "open_with_media_hcplayer_muted", NULL);
#endif
   }
   add_item(fe, "Back", "launcher", FRONTEND_ITEM_ACTION, "back", NULL);
   set_status(fe, "choose handler");
   log_item_sample(fe, "open_with");
}

static void show_config(struct native_frontend *fe)
{
   reset_items(fe, "Config");
   fe->view = FRONTEND_VIEW_CONFIG;
   add_item(fe, "General", "launch defaults", FRONTEND_ITEM_ACTION,
      "launch_settings", NULL);
   add_item(fe, "Custom", "theme options", FRONTEND_ITEM_ACTION,
      "custom", NULL);
   add_item(fe, "Visual", "browser options", FRONTEND_ITEM_ACTION,
      "interface", NULL);
   add_item(fe, "Theme", active_theme_label(fe), FRONTEND_ITEM_ACTION,
      "theme", NULL);
   add_item(fe, "Language", active_language_label(fe),
      FRONTEND_ITEM_ACTION, "language", NULL);
   add_item(fe, "Power", "battery/reboot", FRONTEND_ITEM_ACTION,
      "power", NULL);
   add_item(fe, "Storage", "SD tools", FRONTEND_ITEM_ACTION,
      "storage", NULL);
}

static void show_launch_settings(struct native_frontend *fe)
{
   char detail[48];
   unsigned backlight = 0;

   reset_items(fe, "General");
   fe->view = FRONTEND_VIEW_LAUNCH_SETTINGS;
   add_item(fe, "Audio", fe->run_options.audio_enabled ? "enabled" : "muted",
      FRONTEND_ITEM_ACTION, "audio", NULL);
   snprintf(detail, sizeof(detail), "%ux", fe->run_options.audio_gain);
   add_item(fe, "Volume", detail, FRONTEND_ITEM_ACTION, "gain", NULL);
   snprintf(detail, sizeof(detail), "%u MHz", fe->run_options.scpu_mhz);
   add_item(fe, "CPU", detail, FRONTEND_ITEM_ACTION, "cpu", NULL);
   add_item(fe, "GPU", ge_clock_label(fe->run_options.ge_clock),
      FRONTEND_ITEM_ACTION, "ge_clock", NULL);
   add_item(fe, "Frameskip", frameskip_label(fe->run_options.frameskip),
      FRONTEND_ITEM_ACTION, "frameskip", NULL);
   add_item(fe, "Display", display_label(fe->run_options.display_mode),
      FRONTEND_ITEM_ACTION, "display", NULL);
   add_item(fe, "Keymap", input_profile_label(fe->run_options.input_profile),
      FRONTEND_ITEM_ACTION, "keymap", NULL);
   add_item(fe, "State Slot", state_slot_label(fe->run_options.state_slot),
      FRONTEND_ITEM_ACTION, "state_slot", NULL);
   add_item(fe, "Auto Load State",
      on_off_label(fe->run_options.state_auto_load), FRONTEND_ITEM_ACTION,
      "state_auto_load", NULL);
   add_item(fe, "Auto Save State",
      on_off_label(fe->run_options.state_auto_save), FRONTEND_ITEM_ACTION,
      "state_auto_save", NULL);
   if (unifrog_backlight_get(&backlight) != 0)
      backlight = 0;
   snprintf(detail, sizeof(detail), "%u", backlight);
   add_item(fe, "Backlight", detail, FRONTEND_ITEM_ACTION, "backlight", NULL);
   add_item(fe, "ROM Systems", "defaults", FRONTEND_ITEM_ACTION,
      "rom_systems", NULL);
   add_item(fe, "Back", "config", FRONTEND_ITEM_ACTION, "back_config", NULL);
}

static void show_connect(struct native_frontend *fe)
{
   reset_items(fe, "Connect");
   fe->view = FRONTEND_VIEW_CONNECT;
   add_item(fe, "Network", "unsupported", FRONTEND_ITEM_ACTION, "noop", NULL);
   add_item(fe, "NetAdv", "unsupported", FRONTEND_ITEM_ACTION, "noop", NULL);
   add_item(fe, "Services", "unavailable", FRONTEND_ITEM_ACTION, "noop", NULL);
   add_item(fe, "Bluetooth", "unsupported", FRONTEND_ITEM_ACTION, "noop", NULL);
   add_item(fe, "UsbFunction", "unsupported", FRONTEND_ITEM_ACTION, "noop", NULL);
}

static void show_custom(struct native_frontend *fe)
{
   reset_items(fe, "Custom");
   fe->view = FRONTEND_VIEW_CUSTOM;
   add_item(fe, "Theme", active_theme_label(fe), FRONTEND_ITEM_ACTION, "theme",
      NULL);
   add_item(fe, "Alternate Theme", on_off_label(fe->theme_alternate),
      FRONTEND_ITEM_ACTION, "theme_alternate", NULL);
   add_item(fe, "Launch Splash", on_off_label(fe->launch_splash),
      FRONTEND_ITEM_ACTION, "launch_splash", NULL);
   add_item(fe, "Box Art Hide", on_off_label(fe->boxart_hidden),
      FRONTEND_ITEM_ACTION, "boxart_hide", NULL);
   add_item(fe, "Navigation Sound", on_off_label(fe->sound_enabled),
      FRONTEND_ITEM_ACTION, "sound", NULL);
   add_item(fe, "Debug Log Flush", fe->log_flush_every ? "every log" :
      "buffered", FRONTEND_ITEM_ACTION, "log_flush_every", NULL);
}

static void show_visual(struct native_frontend *fe)
{
   reset_items(fe, "Visual");
   fe->view = FRONTEND_VIEW_VISUAL;
   add_item(fe, "Sort", fe->sort_desc ? "name desc" : "name asc",
      FRONTEND_ITEM_ACTION, "sort", NULL);
   add_item(fe, "Clock", on_off_label(fe->clock_enabled), FRONTEND_ITEM_ACTION,
      "clock", NULL);
   add_item(fe, "Title Root Drive", on_off_label(fe->title_include_root),
      FRONTEND_ITEM_ACTION, "title_root", NULL);
   add_item(fe, "Folder Item Count", on_off_label(fe->folder_counts),
      FRONTEND_ITEM_ACTION, "folder_counts", NULL);
   add_item(fe, "Display Empty Folder", on_off_label(fe->display_empty_folder),
      FRONTEND_ITEM_ACTION, "empty_folder", NULL);
   add_item(fe, "Menu Counter Folder", on_off_label(fe->menu_counter_folder),
      FRONTEND_ITEM_ACTION, "counter_folder", NULL);
   add_item(fe, "Menu Counter File", on_off_label(fe->menu_counter_file),
      FRONTEND_ITEM_ACTION, "counter_file", NULL);
   add_item(fe, "Hidden", fe->show_hidden ? "shown" : "hidden",
      FRONTEND_ITEM_ACTION, "hidden", NULL);
   add_item(fe, "Content Collect", on_off_label(fe->content_collect),
      FRONTEND_ITEM_ACTION, "content_collect", NULL);
   add_item(fe, "Content History", on_off_label(fe->content_history),
      FRONTEND_ITEM_ACTION, "content_history", NULL);
   add_item(fe, "Mixed Content", on_off_label(fe->mixed_content),
      FRONTEND_ITEM_ACTION, "mixed_content", NULL);
}

static void show_power(struct native_frontend *fe)
{
   char detail[48];

   reset_items(fe, "Power");
   fe->view = FRONTEND_VIEW_POWER;
   add_item(fe, "Reboot", "restart UniFrog", FRONTEND_ITEM_ACTION,
      "reboot", NULL);
   add_item(fe, "Firmware Boot", "select .asd", FRONTEND_ITEM_ACTION,
      "firmware", NULL);
   if (unifrog_battery_update(&fe->battery, 0) != 0)
      unifrog_battery_status_init(&fe->battery);
   if (fe->battery.available)
      snprintf(detail, sizeof(detail), "%u mV %u bars",
         fe->battery.millivolts, fe->battery.bars);
   else
      snprintf(detail, sizeof(detail), "not available");
   add_item(fe, "Battery", detail, FRONTEND_ITEM_ACTION, "battery_refresh", NULL);
   add_item(fe, "Storage Recover", "remount SD", FRONTEND_ITEM_ACTION,
      "storage_recover", NULL);
}

static void show_storage(struct native_frontend *fe)
{
   char roms[32];
   char data[32];
   char saves[32];
   unsigned count;

   count = count_content_dir_entries(frontend_rom_root(fe), fe->show_hidden,
      fe->mixed_content);
   snprintf(roms, sizeof(roms), "%u entries", count);
   count = count_visible_dir_entries(FRONTEND_DATA_ROOT);
   snprintf(data, sizeof(data), "%u entries", count);
   count = count_visible_dir_entries(FRONTEND_DATA_ROOT "/saves");
   snprintf(saves, sizeof(saves), "%u saves", count);

   reset_items(fe, "Storage");
   fe->view = FRONTEND_VIEW_STORAGE;
   add_item(fe, "SD Mode", active_storage_label(), FRONTEND_ITEM_ACTION,
      "storage_mode", NULL);
   add_item(fe, "Configured", storage_profile_label(fe->storage_profile),
      FRONTEND_ITEM_ACTION, "storage_mode", NULL);
   add_item(fe, "UniFrog Files", "/unifrog", FRONTEND_ITEM_ACTION,
      "explore_unifrog", NULL);
   add_item(fe, "Bios", "/bios", FRONTEND_ITEM_ACTION, "explore_bios", NULL);
   add_item(fe, frontend_rom_root_label(fe), roms, FRONTEND_ITEM_ACTION,
      "explore", NULL);
   add_item(fe, "Collection", "favorites", FRONTEND_ITEM_ACTION,
      "favorites", NULL);
   add_item(fe, "History", "recent", FRONTEND_ITEM_ACTION, "history", NULL);
   add_item(fe, "Data", data, FRONTEND_ITEM_ACTION, "explore_unifrog", NULL);
   add_item(fe, "Firmware Boot", "boot .asd", FRONTEND_ITEM_ACTION, "firmware",
      NULL);
   add_item(fe, "Updates", "version slots", FRONTEND_ITEM_ACTION, "updates",
      NULL);
   add_item(fe, "Core Manager", "ABI status", FRONTEND_ITEM_ACTION, "cores",
      NULL);
   add_item(fe, "Package Check", "layout", FRONTEND_ITEM_ACTION, "package_check",
      NULL);
   add_item(fe, "Save Data", saves, FRONTEND_ITEM_ACTION, "explore_saves", NULL);
   add_item(fe, "Storage Probe", "1-25 MHz stress", FRONTEND_ITEM_ACTION,
      "storage_fast_probe", NULL);
   add_item(fe, "Theme Files", active_theme_label(fe), FRONTEND_ITEM_ACTION,
      "theme", NULL);
   add_item(fe, "Log Flush", "logs", FRONTEND_ITEM_ACTION, "flush_log", NULL);
   set_status(fe, "SD %s", unifrog_platform_sd_active_profile());
}

static void show_storage_mode(struct native_frontend *fe)
{
   reset_items(fe, "SD Mode");
   fe->view = FRONTEND_VIEW_STORAGE_MODE;
   add_item(fe, "Active", active_storage_label(), FRONTEND_ITEM_ACTION,
      "noop", NULL);
   add_item(fe, "Configured", storage_profile_label(fe->storage_profile),
      FRONTEND_ITEM_ACTION, "noop", NULL);
   add_item(fe, "Bus Width", "4-bit", FRONTEND_ITEM_ACTION, "noop", NULL);
   add_item(fe, "Timing", "default speed", FRONTEND_ITEM_ACTION, "noop", NULL);
   add_item(fe, "Signal", "3.3 V, no UHS", FRONTEND_ITEM_ACTION, "noop", NULL);
   add_item(fe, "Boot Profile", UNIFROG_SD_MODE, FRONTEND_ITEM_ACTION,
      "storage_profile", "boot");
   for (unsigned i = 1; i < ARRAY_SIZE(storage_config_profiles); i++) {
      const char *profile = storage_config_profiles[i];
      char meta[64];

      snprintf(meta, sizeof(meta), "%s%s", storage_profile_label(profile),
         strcmp(profile, fe->storage_profile) == 0 ? " *" : "");
      add_item(fe, profile, meta, FRONTEND_ITEM_ACTION,
         "storage_profile", profile);
   }
   add_item(fe, "Back", "storage", FRONTEND_ITEM_ACTION, "back_storage", NULL);
   set_status(fe, "A choose  B back");
}

static void show_storage_confirm(struct native_frontend *fe, const char *profile)
{
   unifrog_text_copy(fe->storage_pending_profile,
      sizeof(fe->storage_pending_profile), profile ? profile : "boot");
   reset_items(fe, "Apply SD Mode");
   fe->view = FRONTEND_VIEW_STORAGE_CONFIRM;
   add_item(fe, "Apply", storage_profile_label(fe->storage_pending_profile),
      FRONTEND_ITEM_ACTION, "storage_apply_pending", NULL);
   add_item(fe, "Cancel", "no change", FRONTEND_ITEM_ACTION, "storage_mode",
      NULL);
   add_item(fe, "Active", active_storage_label(), FRONTEND_ITEM_ACTION,
      "noop", NULL);
   add_item(fe, "Configured", storage_profile_label(fe->storage_profile),
      FRONTEND_ITEM_ACTION, "noop", NULL);
   set_status(fe, "confirm SD switch");
}

static void show_theme_list(struct native_frontend *fe)
{
   DIR *dir;
   struct dirent *entry;

   reset_items(fe, "Theme");
   fe->view = FRONTEND_VIEW_THEME;
   add_item(fe, "muOS", strcmp(active_theme_label(fe), "muos") == 0 ?
      "active" : "built-in", FRONTEND_ITEM_ACTION, "theme_select", "muos");
   dir = opendir(FRONTEND_THEME_ROOT);
   if (dir) {
      while ((entry = readdir(dir)) != NULL) {
         char name[48];
         char meta[32];
         char version_path[FRONTEND_MAX_PATH];
         char full[FRONTEND_MAX_PATH];

         unifrog_text_copy(name, sizeof(name), entry->d_name);
         if (!name[0] || name[0] == '.' || strcmp(name, "muos") == 0)
            continue;
         if (unifrog_text_ends_with_ci(name, ".ini")) {
            strip_ini_suffix(name);
         } else {
            if (path_join(full, sizeof(full), FRONTEND_THEME_ROOT,
                entry->d_name) != 0 ||
                path_join(version_path, sizeof(version_path), full,
                "version.txt") != 0 ||
                access(version_path, F_OK) != 0)
               continue;
         }
         snprintf(meta, sizeof(meta), "%s",
            strcmp(active_theme_label(fe), name) == 0 ? "active" : "theme");
         add_item(fe, name, meta, FRONTEND_ITEM_ACTION, "theme_select", name);
      }
      closedir(dir);
   }
   dir = opendir(FRONTEND_ARCHIVE_ROOT);
   if (dir) {
      while ((entry = readdir(dir)) != NULL) {
         char full[FRONTEND_MAX_PATH];

         if (!unifrog_text_ends_with_ci(entry->d_name, ".muxthm"))
            continue;
         if (path_join(full, sizeof(full), FRONTEND_ARCHIVE_ROOT,
             entry->d_name) != 0)
            continue;
         add_item(fe, entry->d_name, "install archive",
            FRONTEND_ITEM_THEME_ARCHIVE, full, NULL);
      }
      closedir(dir);
   }
   dir = opendir(FRONTEND_STOCK_ARCHIVE_ROOT);
   if (dir) {
      while ((entry = readdir(dir)) != NULL) {
         char full[FRONTEND_MAX_PATH];

         if (!unifrog_text_ends_with_ci(entry->d_name, ".muxthm"))
            continue;
         if (path_join(full, sizeof(full), FRONTEND_STOCK_ARCHIVE_ROOT,
             entry->d_name) != 0)
            continue;
         add_item(fe, entry->d_name, "install archive",
            FRONTEND_ITEM_THEME_ARCHIVE, full, NULL);
      }
      closedir(dir);
   }
   add_item(fe, "Back", "custom", FRONTEND_ITEM_ACTION, "custom", NULL);
   set_status(fe, "A apply/install theme");
}

static void show_language_list(struct native_frontend *fe)
{
   DIR *dir;
   struct dirent *entry;

   reset_items(fe, "Language");
   fe->view = FRONTEND_VIEW_LANGUAGE;
   add_item(fe, "english", strcmp(active_language_label(fe), "english") == 0 ?
      "active" : "built-in", FRONTEND_ITEM_ACTION, "language_select",
      "english");
   dir = opendir(FRONTEND_LANGUAGE_ROOT);
   if (dir) {
      while ((entry = readdir(dir)) != NULL) {
         char name[48];
         char meta[32];

         if (!unifrog_text_ends_with_ci(entry->d_name, ".ini"))
            continue;
         unifrog_text_copy(name, sizeof(name), entry->d_name);
         strip_ini_suffix(name);
         if (!name[0] || strcmp(name, "english") == 0)
            continue;
         snprintf(meta, sizeof(meta), "%s",
            strcmp(active_language_label(fe), name) == 0 ? "active" :
            "language");
         add_item(fe, name, meta, FRONTEND_ITEM_ACTION, "language_select", name);
      }
      closedir(dir);
   }
   add_item(fe, "Back", "config", FRONTEND_ITEM_ACTION, "config", NULL);
   set_status(fe, "A apply language");
}

static void show_firmware(struct native_frontend *fe)
{
   nav_reset(fe);
   show_firmware_browser(fe, FRONTEND_ROOT);
}

static void show_info(struct native_frontend *fe)
{
   char detail[64];
   struct stat st;

   reset_items(fe, "Information");
   fe->view = FRONTEND_VIEW_INFO;
   snprintf(detail, sizeof(detail), "%s%s", UNIFROG_GIT_COMMIT,
      UNIFROG_GIT_DIRTY ? " dirty" : "");
   add_item(fe, "Activity Log", "mark log", FRONTEND_ITEM_ACTION,
      "flush_log", NULL);
   add_item(fe, "Screenshot", "diagnose", FRONTEND_ITEM_ACTION,
      "screenshot", NULL);
   add_item(fe, "Diagnostics", "runtime", FRONTEND_ITEM_ACTION, "sysinfo", NULL);
   add_item(fe, "Core Manager", "ABI status", FRONTEND_ITEM_ACTION, "cores",
      NULL);
   add_item(fe, "Package Check", "layout", FRONTEND_ITEM_ACTION, "package_check",
      NULL);
   add_item(fe, "Space", "storage", FRONTEND_ITEM_ACTION, "storage", NULL);
   add_item(fe, "Tester", "storage recover", FRONTEND_ITEM_ACTION,
      "storage_recover", NULL);
   add_item(fe, "SysInfo", detail, FRONTEND_ITEM_ACTION, "sysinfo", NULL);
   add_item(fe, "Uptime", fe->clock_enabled ? "local tick" : "off",
      FRONTEND_ITEM_ACTION, "chrony", NULL);
   add_item(fe, "Credit", UNIFROG_NATIVE_FRONTEND_GIT_COMMIT,
      FRONTEND_ITEM_ACTION, "noop", NULL);
   if (stat(FRONTEND_SETTINGS_PATH, &st) == 0) {
      snprintf(detail, sizeof(detail), "%lu bytes",
         (unsigned long)st.st_size);
      set_status(fe, "settings %s", detail);
   }
}

static void show_apps(struct native_frontend *fe)
{
   reset_items(fe, "Apps");
   fe->view = FRONTEND_VIEW_APPS;
   add_item(fe, "File Browser", "SD root", FRONTEND_ITEM_ACTION,
      "explore_sd", NULL);
   add_item(fe, "Updates", "versions", FRONTEND_ITEM_ACTION,
      "updates", NULL);
   add_item(fe, "Core Manager", "ABI status", FRONTEND_ITEM_ACTION,
      "cores", NULL);
   add_item(fe, "Package Check", "layout", FRONTEND_ITEM_ACTION,
      "package_check", NULL);
   add_item(fe, "Runtime Settings", "core options", FRONTEND_ITEM_ACTION,
      "launch_settings", NULL);
   add_item(fe, "JavaScript Scripts", "/unifrog/scripts", FRONTEND_ITEM_ACTION,
      "scripts", NULL);
   add_item(fe, "History", "recent games", FRONTEND_ITEM_ACTION,
      "history", NULL);
   add_item(fe, "SD Files", "browse", FRONTEND_ITEM_ACTION,
      "explore_sd", NULL);
   add_item(fe, "ROM Browser", "content", FRONTEND_ITEM_ACTION,
      "explore", NULL);
   add_item(fe, "Storage Recover", "remount SD", FRONTEND_ITEM_ACTION,
      "storage_recover", NULL);
   add_item(fe, "Storage Probe", "speed/stability", FRONTEND_ITEM_ACTION,
      "storage_fast_probe", NULL);
   add_item(fe, "Flush Log", "write log.txt", FRONTEND_ITEM_ACTION,
      "flush_log", NULL);
   add_item(fe, "Firmware Boot", "handoff", FRONTEND_ITEM_ACTION, "firmware",
      NULL);
   add_item(fe, "Power", "battery", FRONTEND_ITEM_ACTION, "power", NULL);
   add_item(fe, "SysInfo", "device", FRONTEND_ITEM_ACTION, "sysinfo", NULL);
   add_item(fe, "Back", "launcher", FRONTEND_ITEM_ACTION, "back", NULL);
}

static void show_updates(struct native_frontend *fe)
{
   DIR *dir;
   struct dirent *entry;

   ensure_data_dirs();
   reset_items(fe, "Updates");
   fe->view = FRONTEND_VIEW_UPDATES;
   add_item(fe, "Current", UNIFROG_GIT_COMMIT, FRONTEND_ITEM_ACTION,
      "noop", NULL);
   dir = opendir(FRONTEND_UPDATE_ROOT);
   if (dir) {
      while ((entry = readdir(dir)) != NULL) {
         char full[FRONTEND_MAX_PATH];

         if (!is_zip_file(entry->d_name))
            continue;
         if (path_join(full, sizeof(full), FRONTEND_UPDATE_ROOT,
             entry->d_name) != 0)
            continue;
         add_item(fe, entry->d_name, "install zip",
            FRONTEND_ITEM_UPDATE_ARCHIVE, full, NULL);
      }
      closedir(dir);
   }
   dir = opendir(FRONTEND_VERSION_ROOT);
   if (dir) {
      while ((entry = readdir(dir)) != NULL) {
         char full[FRONTEND_MAX_PATH];
         char marker[FRONTEND_MAX_PATH];
         struct stat st;

         if (entry->d_name[0] == '.')
            continue;
         if (path_join(full, sizeof(full), FRONTEND_VERSION_ROOT,
             entry->d_name) != 0 ||
             stat(full, &st) != 0 || !S_ISDIR(st.st_mode))
            continue;
         if (path_join(marker, sizeof(marker), full,
             "unifrog/firmware/unifrog.bin") != 0 || !file_exists(marker))
            continue;
         add_item(fe, entry->d_name, "activate version",
            FRONTEND_ITEM_VERSION, full, entry->d_name);
      }
      closedir(dir);
   }
   add_item(fe, "Back", "apps", FRONTEND_ITEM_ACTION, "back_apps", NULL);
   set_status(fe, "put update zips in /unifrog_data/updates");
}

static void add_check_item(struct native_frontend *fe, unsigned *pass,
   unsigned *fail, const char *name, int ok, const char *detail)
{
   add_item(fe, name, ok ? "ok" : detail ? detail : "bad",
      FRONTEND_ITEM_ACTION, "noop", NULL);
   if (ok)
      (*pass)++;
   else
      (*fail)++;
}

static void show_core_info(struct native_frontend *fe, const char *path)
{
   struct unifrog_core_module_header h;
   char detail[96];
   int valid;
   int compat;

   reset_items(fe, "Core Info");
   fe->view = FRONTEND_VIEW_CORE_INFO;
   if (read_core_module_header(path, &h) != 0) {
      add_item(fe, basename_const(path), "read failed", FRONTEND_ITEM_ACTION,
         "noop", NULL);
      add_item(fe, "Back", "cores", FRONTEND_ITEM_ACTION, "cores", NULL);
      return;
   }
   valid = core_module_header_valid(&h);
   compat = core_module_header_compatible(&h);
   add_item(fe, "Core", h.core_id[0] ? h.core_id : basename_const(path),
      FRONTEND_ITEM_ACTION, "noop", NULL);
   add_item(fe, "Status", valid ? (compat ? "compatible" : "unsupported") :
      "invalid", FRONTEND_ITEM_ACTION, "noop", NULL);
   snprintf(detail, sizeof(detail), "%u.%u.%u size %lu",
      (unsigned)UNIFROG_ABI_VERSION_GET_MAJOR(h.required_abi_version),
      (unsigned)UNIFROG_ABI_VERSION_GET_MINOR(h.required_abi_version),
      (unsigned)UNIFROG_ABI_VERSION_GET_PATCH(h.required_abi_version),
      (unsigned long)(h.required_abi_size ? h.required_abi_size :
      (unsigned)UNIFROG_ABI_CORE_MIN_SIZE));
   add_item(fe, "Requires ABI", detail, FRONTEND_ITEM_ACTION, "noop", NULL);
   snprintf(detail, sizeof(detail), "%u.%u.%u size %lu",
      (unsigned)UNIFROG_ABI_VERSION_GET_MAJOR(h.built_abi_version),
      (unsigned)UNIFROG_ABI_VERSION_GET_MINOR(h.built_abi_version),
      (unsigned)UNIFROG_ABI_VERSION_GET_PATCH(h.built_abi_version),
      (unsigned long)h.built_abi_size);
   add_item(fe, "Built ABI", detail, FRONTEND_ITEM_ACTION, "noop", NULL);
   snprintf(detail, sizeof(detail), "0x%08lx + %u",
      (unsigned long)h.load_addr,
      (unsigned)(h.memory_end_addr - h.load_addr));
   add_item(fe, "Memory", detail, FRONTEND_ITEM_ACTION, "noop", NULL);
   snprintf(detail, sizeof(detail), "%lu bytes",
      (unsigned long)h.exports_size);
   add_item(fe, "Exports", detail, FRONTEND_ITEM_ACTION, "noop", NULL);
   add_item(fe, "Extensions", h.extensions[0] ? h.extensions : "none",
      FRONTEND_ITEM_ACTION, "noop", NULL);
   add_item(fe, "Path", basename_const(path), FRONTEND_ITEM_ACTION, "noop", NULL);
   add_item(fe, "Back", "cores", FRONTEND_ITEM_ACTION, "cores", NULL);
}

static void show_core_manager(struct native_frontend *fe)
{
   DIR *dir;
   struct dirent *entry;
   unsigned total = 0;
   unsigned bad = 0;

   reset_items(fe, "Cores");
   fe->view = FRONTEND_VIEW_CORES;
   dir = opendir(UNIFROG_CORE_ROOT);
   if (dir) {
      while ((entry = readdir(dir)) != NULL) {
         char full[FRONTEND_MAX_PATH];
         char meta[64];
         struct unifrog_core_module_header h;

         if (!is_core_module_file(entry->d_name))
            continue;
         if (path_join(full, sizeof(full), UNIFROG_CORE_ROOT,
             entry->d_name) != 0)
            continue;
         total++;
         if (read_core_module_header(full, &h) == 0) {
            core_module_meta(meta, sizeof(meta), &h);
            if (!core_module_header_compatible(&h))
               bad++;
         } else {
            snprintf(meta, sizeof(meta), "unreadable");
            bad++;
         }
         add_item(fe, entry->d_name, meta, FRONTEND_ITEM_CORE_MODULE, full, NULL);
      }
      closedir(dir);
   }
   add_item(fe, "Back", "apps", FRONTEND_ITEM_ACTION, "back_apps", NULL);
   set_status(fe, "%u cores, %u issues", total, bad);
}

static int dist_has_user_state_dir(const char *name)
{
   return strcmp(name, "saves") == 0 || strcmp(name, "cache") == 0 ||
      strcmp(name, "logs") == 0 || strcmp(name, "themes") == 0 ||
      strcmp(name, "languages") == 0 || strcmp(name, "archive") == 0 ||
      strcmp(name, "scripts") == 0 || strcmp(name, "updates") == 0 ||
      strcmp(name, "versions") == 0 || strcmp(name, "user") == 0;
}

static void show_package_check(struct native_frontend *fe)
{
   DIR *dir;
   struct dirent *entry;
   unsigned pass = 0;
   unsigned fail = 0;
   unsigned cores = 0;
   unsigned core_bad = 0;
   char summary[96];

   reset_items(fe, "Package Check");
   fe->view = FRONTEND_VIEW_PACKAGE_CHECK;
   add_check_item(fe, &pass, &fail, "bios/bisrv.asd",
      file_exists(UNIFROG_BIOS_ROOT "/bisrv.asd"), "missing");
   add_check_item(fe, &pass, &fail, "firmware",
      file_exists(UNIFROG_DIST_FIRMWARE_PATH), "missing");
   add_check_item(fe, &pass, &fail, "manifest",
      file_exists(UNIFROG_DIST_MANIFEST_PATH), "missing");
   add_check_item(fe, &pass, &fail, "data root",
      file_exists(UNIFROG_DATA_ROOT), "missing");
   add_check_item(fe, &pass, &fail, "logs root",
      file_exists(UNIFROG_LOG_ROOT), "missing");
   dir = opendir(FRONTEND_DIST_ROOT);
   if (dir) {
      while ((entry = readdir(dir)) != NULL) {
         if (entry->d_name[0] == '.' || !dist_has_user_state_dir(entry->d_name))
            continue;
         snprintf(summary, sizeof(summary), "state in dist: %.72s",
            entry->d_name);
         add_check_item(fe, &pass, &fail, "dist clean", 0, summary);
      }
      closedir(dir);
   }
   dir = opendir(UNIFROG_CORE_ROOT);
   if (dir) {
      while ((entry = readdir(dir)) != NULL) {
         char full[FRONTEND_MAX_PATH];
         struct unifrog_core_module_header h;

         if (!is_core_module_file(entry->d_name))
            continue;
         if (path_join(full, sizeof(full), UNIFROG_CORE_ROOT,
             entry->d_name) != 0)
            continue;
         cores++;
         if (read_core_module_header(full, &h) != 0 ||
             !core_module_header_compatible(&h))
            core_bad++;
      }
      closedir(dir);
   }
   snprintf(summary, sizeof(summary), "%u cores, %u bad", cores, core_bad);
   add_check_item(fe, &pass, &fail, "core headers", core_bad == 0 && cores > 0,
      summary);
   dir = opendir(FRONTEND_UPDATE_ROOT);
   if (dir) {
      while ((entry = readdir(dir)) != NULL) {
         char full[FRONTEND_MAX_PATH];

         if (!is_zip_file(entry->d_name))
            continue;
         if (path_join(full, sizeof(full), FRONTEND_UPDATE_ROOT,
             entry->d_name) != 0)
            continue;
         validate_update_archive(full, summary, sizeof(summary));
         add_check_item(fe, &pass, &fail, entry->d_name,
            strncmp(summary, "ok ", 3) == 0, summary);
      }
      closedir(dir);
   }
   snprintf(summary, sizeof(summary), "pass=%u fail=%u\ncommit=%s\n",
      pass, fail, UNIFROG_GIT_COMMIT);
   (void)write_text_file(UNIFROG_PACKAGE_CHECK_PATH, summary);
   add_item(fe, "Back", "apps", FRONTEND_ITEM_ACTION, "back_apps", NULL);
   set_status(fe, "%u pass, %u fail", pass, fail);
   unifrog_log("frontend package check pass=%u fail=%u cores=%u bad=%u\n",
      pass, fail, cores, core_bad);
}

static void show_sysinfo(struct native_frontend *fe)
{
   char detail[64];
   char slot[64];
   uint32_t now = unifrog_perf_time_ms();

   reset_items(fe, "SysInfo");
   fe->view = FRONTEND_VIEW_SYSINFO;
   add_item(fe, "Version", UNIFROG_GIT_COMMIT, FRONTEND_ITEM_ACTION,
      "noop", NULL);
   add_item(fe, "Layout", "current", FRONTEND_ITEM_ACTION, "noop", NULL);
   snprintf(detail, sizeof(detail), "%u.%u.%u size %u",
      UNIFROG_ABI_VERSION_MAJOR_VALUE, UNIFROG_ABI_VERSION_MINOR_VALUE,
      UNIFROG_ABI_VERSION_PATCH_VALUE, (unsigned)unifrog_abi_get()->size);
   add_item(fe, "ABI", detail, FRONTEND_ITEM_ACTION, "noop", NULL);
   if (read_file_key(slot, sizeof(slot), FRONTEND_ACTIVE_VERSION_PATH, "slot") != 0)
      unifrog_text_copy(slot, sizeof(slot), "live");
   add_item(fe, "Slot", slot, FRONTEND_ITEM_ACTION, "noop", NULL);
   add_item(fe, "Boot OK", file_exists(UNIFROG_BOOT_OK_PATH) ? "yes" : "no",
      FRONTEND_ITEM_ACTION, "noop", NULL);
   add_item(fe, "Pending", file_exists(UNIFROG_PENDING_VERSION_PATH) ? "yes" :
      "no", FRONTEND_ITEM_ACTION, "noop", NULL);
   add_item(fe, "Theme", active_theme_label(fe), FRONTEND_ITEM_ACTION, "noop",
      NULL);
   add_item(fe, "Language", active_language_label(fe), FRONTEND_ITEM_ACTION,
      "noop", NULL);
   add_item(fe, "SD Mode", unifrog_platform_sd_active_profile(),
      FRONTEND_ITEM_ACTION, "noop", NULL);
   add_item(fe, "Log Path", unifrog_log_last_path() ?
      unifrog_log_last_path() : UNIFROG_LOG_ROOT, FRONTEND_ITEM_ACTION, "noop",
      NULL);
   snprintf(detail, sizeof(detail), "%s%s", UNIFROG_NATIVE_FRONTEND_GIT_COMMIT,
      UNIFROG_GIT_DIRTY ? " dirty" : "");
   add_item(fe, "Build", detail, FRONTEND_ITEM_ACTION, "noop", NULL);
   add_item(fe, "Device", "SF2000", FRONTEND_ITEM_ACTION, "noop", NULL);
   add_item(fe, "Kernel", "HCRTOS", FRONTEND_ITEM_ACTION, "noop", NULL);
   snprintf(detail, sizeof(detail), "%lus", (unsigned long)(now / 1000u));
   add_item(fe, "Uptime", detail, FRONTEND_ITEM_ACTION, "noop", NULL);
   add_item(fe, "Cpu", "MIPS", FRONTEND_ITEM_ACTION, "noop", NULL);
   snprintf(detail, sizeof(detail), "%u MHz", fe->run_options.scpu_mhz);
   add_item(fe, "Speed", detail, FRONTEND_ITEM_ACTION, "noop", NULL);
   add_item(fe, "Governor", "fixed", FRONTEND_ITEM_ACTION, "noop", NULL);
   add_item(fe, "Memory", "external", FRONTEND_ITEM_ACTION, "noop", NULL);
   add_item(fe, "Swap", "none", FRONTEND_ITEM_ACTION, "noop", NULL);
   add_item(fe, "Temp", "n/a", FRONTEND_ITEM_ACTION, "noop", NULL);
   snprintf(detail, sizeof(detail), "%u bars", fe->battery.bars);
   add_item(fe, "Capacity", fe->battery.available ? detail : "n/a",
      FRONTEND_ITEM_ACTION, "noop", NULL);
   snprintf(detail, sizeof(detail), "%u mV", fe->battery.millivolts);
   add_item(fe, "Voltage", fe->battery.available ? detail : "n/a",
      FRONTEND_ITEM_ACTION, "noop", NULL);
   add_item(fe, "Charger", "unknown", FRONTEND_ITEM_ACTION, "noop", NULL);
   add_item(fe, "Reload", "refresh", FRONTEND_ITEM_ACTION, "sysinfo", NULL);
}

static void restore_view_selection(struct native_frontend *fe, unsigned selected,
   unsigned scroll)
{
   fe->selected = selected;
   fe->scroll = scroll;
   clamp_selection(fe);
   fe->needs_draw = 1;
   log_selection(fe, "back");
}

static void show_view(struct native_frontend *fe, enum frontend_view view)
{
   switch (view) {
   case FRONTEND_VIEW_LAUNCH:
      show_launch(fe);
      break;
   case FRONTEND_VIEW_CONFIG:
      show_config(fe);
      break;
   case FRONTEND_VIEW_CONNECT:
      show_connect(fe);
      break;
   case FRONTEND_VIEW_CUSTOM:
      show_custom(fe);
      break;
   case FRONTEND_VIEW_VISUAL:
      show_visual(fe);
      break;
   case FRONTEND_VIEW_POWER:
      show_power(fe);
      break;
   case FRONTEND_VIEW_STORAGE:
      show_storage(fe);
      break;
   case FRONTEND_VIEW_STORAGE_MODE:
      show_storage_mode(fe);
      break;
   case FRONTEND_VIEW_INFO:
      show_info(fe);
      break;
   case FRONTEND_VIEW_APPS:
      show_apps(fe);
      break;
   case FRONTEND_VIEW_UPDATES:
      show_updates(fe);
      break;
   case FRONTEND_VIEW_CORES:
      show_core_manager(fe);
      break;
   case FRONTEND_VIEW_PACKAGE_CHECK:
      show_package_check(fe);
      break;
   case FRONTEND_VIEW_SYSINFO:
      show_sysinfo(fe);
      break;
   case FRONTEND_VIEW_THEME:
      show_theme_list(fe);
      break;
   case FRONTEND_VIEW_LANGUAGE:
      show_language_list(fe);
      break;
   case FRONTEND_VIEW_LAUNCH_SETTINGS:
      show_launch_settings(fe);
      break;
   default:
      show_launch(fe);
      break;
   }
}

static int restore_parent_view(struct native_frontend *fe,
   enum frontend_view fallback)
{
   enum frontend_view view = fallback;
   unsigned selected = 0;
   unsigned scroll = 0;
   int had_parent = 0;

   if (fe->view_stack_count > 0) {
      fe->view_stack_count--;
      view = fe->view_stack[fe->view_stack_count];
      selected = fe->view_stack_selected[fe->view_stack_count];
      scroll = fe->view_stack_scroll[fe->view_stack_count];
      had_parent = 1;
   }
   if (fe->view_stack_count > 0) {
      fe->parent_view = fe->view_stack[fe->view_stack_count - 1u];
      fe->has_parent_view = 1;
   } else {
      fe->parent_view = FRONTEND_VIEW_LAUNCH;
      fe->has_parent_view = 0;
   }
   show_view(fe, view);
   if (had_parent)
      restore_view_selection(fe, selected, scroll);
   return had_parent;
}

static void browser_back(struct native_frontend *fe)
{
   char parent[FRONTEND_MAX_PATH];
   char *slash;

   if (fe->view == FRONTEND_VIEW_LAUNCH)
      return;
   if (fe->view == FRONTEND_VIEW_OPEN_WITH) {
      fe->pending_open_valid = 0;
      if (fe->has_parent_view && fe->parent_view == FRONTEND_VIEW_EXPLORE) {
         clear_parent_view(fe);
         show_explore(fe, fe->current_dir[0] ? fe->current_dir :
            frontend_rom_root(fe));
         return;
      }
      clear_parent_view(fe);
      show_launch(fe);
      return;
   }
   if (fe->view == FRONTEND_VIEW_SYSINFO || fe->view == FRONTEND_VIEW_CORES ||
       fe->view == FRONTEND_VIEW_PACKAGE_CHECK) {
      restore_parent_view(fe, FRONTEND_VIEW_INFO);
      return;
   }
   if (fe->view == FRONTEND_VIEW_CORE_INFO) {
      show_core_manager(fe);
      return;
   }
   if (fe->view == FRONTEND_VIEW_HISTORY || fe->view == FRONTEND_VIEW_FAVORITES) {
      restore_parent_view(fe, FRONTEND_VIEW_LAUNCH);
      return;
   }
   if (fe->view == FRONTEND_VIEW_CONNECT || fe->view == FRONTEND_VIEW_CUSTOM ||
       fe->view == FRONTEND_VIEW_VISUAL) {
      restore_parent_view(fe, FRONTEND_VIEW_CONFIG);
      return;
   }
   if (fe->view == FRONTEND_VIEW_THEME) {
      restore_parent_view(fe, FRONTEND_VIEW_CUSTOM);
      return;
   }
   if (fe->view == FRONTEND_VIEW_LANGUAGE) {
      restore_parent_view(fe, FRONTEND_VIEW_CONFIG);
      return;
   }
   if (fe->view == FRONTEND_VIEW_STORAGE_CONFIRM) {
      show_storage_mode(fe);
      return;
   }
   if (fe->view == FRONTEND_VIEW_STORAGE_MODE) {
      show_storage(fe);
      return;
   }
   if (fe->view == FRONTEND_VIEW_ROM_SYSTEMS) {
      show_launch(fe);
      return;
   }
   if (fe->view == FRONTEND_VIEW_POWER || fe->view == FRONTEND_VIEW_STORAGE ||
       fe->view == FRONTEND_VIEW_LAUNCH_SETTINGS || fe->view == FRONTEND_VIEW_UPDATES) {
      restore_parent_view(fe, FRONTEND_VIEW_CONFIG);
      return;
   }
   if (fe->view == FRONTEND_VIEW_FIRMWARE) {
      if (fe->nav_count > 0) {
         char path[FRONTEND_MAX_PATH];
         unsigned selected;

         fe->nav_count--;
         unifrog_text_copy(path, sizeof(path), fe->nav_path[fe->nav_count]);
         selected = fe->nav_selected[fe->nav_count];
         unifrog_log("frontend nav back view=%d depth=%u path=%s selected=%u\n",
            fe->view, fe->nav_count, path, selected);
         show_firmware_browser(fe, path);
         fe->selected = selected;
         clamp_selection(fe);
         log_selection(fe, "back");
         return;
      }
      if (strcmp(fe->current_dir, FRONTEND_FIRMWARE_ROOT) == 0) {
         unifrog_log("frontend nav back firmware root -> apps\n");
         show_apps(fe);
         return;
      }
      unifrog_text_copy(parent, sizeof(parent), fe->current_dir);
      slash = strrchr(parent, '/');
      if (slash && slash > parent)
         *slash = '\0';
      else
         unifrog_text_copy(parent, sizeof(parent), FRONTEND_FIRMWARE_ROOT);
      if (strncmp(parent, FRONTEND_FIRMWARE_ROOT,
          strlen(FRONTEND_FIRMWARE_ROOT)) != 0)
         unifrog_text_copy(parent, sizeof(parent), FRONTEND_FIRMWARE_ROOT);
      unifrog_log("frontend nav back firmware parent=%s\n", parent);
      show_firmware_browser(fe, parent);
      return;
   }
   if (fe->view != FRONTEND_VIEW_EXPLORE) {
      show_launch(fe);
      return;
   }
   if (fe->nav_count > 0) {
      char path[FRONTEND_MAX_PATH];
      unsigned selected;

      fe->nav_count--;
      unifrog_text_copy(path, sizeof(path), fe->nav_path[fe->nav_count]);
      selected = fe->nav_selected[fe->nav_count];
      unifrog_log("frontend nav back view=%d depth=%u path=%s selected=%u\n",
         fe->view, fe->nav_count, path, selected);
      show_explore(fe, path);
      fe->selected = selected;
      clamp_selection(fe);
      log_selection(fe, "back");
      return;
   }
   if (frontend_path_is_rom_root(fe, fe->current_dir)) {
      show_rom_systems(fe);
      return;
   }
   if (strcmp(fe->current_dir, FRONTEND_ROOT) == 0) {
      show_launch(fe);
      return;
   }
   unifrog_text_copy(parent, sizeof(parent), fe->current_dir);
   slash = strrchr(parent, '/');
   if (slash && slash > parent)
      *slash = '\0';
   else
      unifrog_text_copy(parent, sizeof(parent), FRONTEND_ROOT);
   if (frontend_path_is_inside_rom_root(fe, fe->current_dir) &&
       !frontend_path_is_inside_rom_root(fe, parent)) {
      show_rom_systems(fe);
      return;
   }
   unifrog_log("frontend nav back explore parent=%s\n", parent);
   show_explore(fe, parent);
}

static void move_selection(struct native_frontend *fe, int delta)
{
   unsigned old_selected;
   unsigned old_scroll;

   if (fe->item_count == 0)
      return;
   old_selected = fe->selected;
   old_scroll = fe->scroll;
   if (delta < 0)
      fe->selected = fe->selected == 0 ? fe->item_count - 1u :
         fe->selected - 1u;
   else
      fe->selected = fe->selected + 1u >= fe->item_count ? 0 :
         fe->selected + 1u;
   if (fe->selected < fe->scroll)
      fe->scroll = fe->selected;
   if (fe->selected >= fe->scroll + FRONTEND_ROWS)
      fe->scroll = fe->selected - FRONTEND_ROWS + 1u;
   if (fe->selected != old_selected || fe->scroll != old_scroll)
      fe->needs_draw = 1;
   log_selection(fe, delta < 0 ? "up" : "down");
}

static char jump_key_for_item(const struct frontend_item *item)
{
   const char *name = item ? item->name : "";

   while (*name == ' ' || *name == '_' || *name == '-' || *name == '[' ||
          *name == '(')
      name++;
   if (!*name)
      return '#';
   if (ascii_is_digit(*name))
      return '#';
   return ascii_lower(*name);
}

static void jump_selection_group(struct native_frontend *fe, int dir)
{
   char current;
   unsigned start;
   unsigned fallback;

   if (!fe || fe->item_count <= 1 || fe->selected >= fe->item_count)
      return;
   start = fe->selected;
   current = jump_key_for_item(&fe->items[fe->selected]);
   for (unsigned step = 1; step < fe->item_count; step++) {
      unsigned index;
      char key;

      if (dir < 0)
         index = (start + fe->item_count - step) % fe->item_count;
      else
         index = (start + step) % fe->item_count;
      if (is_back_item(&fe->items[index]))
         continue;
      key = jump_key_for_item(&fe->items[index]);
      if (key == current)
         continue;
      fe->selected = index;
      clamp_selection(fe);
      log_selection(fe, dir < 0 ? "jump_prev" : "jump_next");
      set_status(fe, "jump %c", key);
      return;
   }
   fallback = fe->item_count < FRONTEND_JUMP_FALLBACK_STEP ?
      fe->item_count : FRONTEND_JUMP_FALLBACK_STEP;
   if (fallback <= 1)
      return;
   if (dir < 0)
      fe->selected = (start + fe->item_count - fallback) % fe->item_count;
   else
      fe->selected = (start + fallback) % fe->item_count;
   if (is_back_item(&fe->items[fe->selected]))
      fe->selected = dir < 0 ? fe->item_count - 1u : 1u;
   clamp_selection(fe);
   log_selection(fe, dir < 0 ? "jump_prev" : "jump_next");
   set_status(fe, "jump %u", fe->selected + 1u);
}

static void frontend_sound_shutdown(void)
{
   unifrog_audio_set_system_output_enabled(0);
}

static void launch_game(struct native_frontend *fe, struct frontend_item *item)
{
   const char *core;
   struct stat st;
   int ret;

   if (!item || !item->path[0])
      return;
   if (fe->view != FRONTEND_VIEW_OPEN_WITH &&
       (!item->core[0] || is_zip_file(item->path))) {
      set_parent_view(fe);
      show_open_with(fe, item);
      return;
   }
   if (stat(item->path, &st) != 0) {
      set_status(fe, "missing: %s", item->name);
      unifrog_log("frontend launch missing path=%s errno=%d\n", item->path,
         errno);
      return;
   }
   if (!S_ISREG(st.st_mode) || st.st_size <= 0) {
      set_status(fe, "invalid game file");
      unifrog_log("frontend launch invalid_file path=%s mode=0x%lx size=%ld\n",
         item->path, (unsigned long)st.st_mode, (long)st.st_size);
      return;
   }
   if (item->core[0]) {
      const char *safe_core = safe_core_for_path(item->path, item->core);

      if (safe_core != item->core)
         unifrog_text_copy(item->core, sizeof(item->core), safe_core);
   }
   unifrog_log("frontend launch game path=%s core=%s\n", item->path,
      item->core);
   if (item->core[0])
      unifrog_text_copy(fe->run_options.core_id,
         sizeof(fe->run_options.core_id), item->core);
   else
      fe->run_options.core_id[0] = '\0';
   core = fe->run_options.core_id[0] ? fe->run_options.core_id : "";
   if (fe->launch_splash)
      frontend_loading_show(fe, "Launching", item->name,
         core[0] ? core : "auto core", 8);
   unifrog_diag_memory_snapshot("native_frontend.launch");
   frontend_sound_shutdown();
   (void)unifrog_log_flush();
   ret = unifrog_libretro_run_path_ex(item->path, &fe->run_options);
   record_history(fe, item->path, core);
   set_status(fe, "returned %d", ret);
   (void)unifrog_ui_open(&fe->ui, 0);
   unifrog_input_clear();
}

static void launch_media(struct native_frontend *fe, struct frontend_item *item)
{
   struct unifrog_media_video_options options;
   int ret = -1;

   if (!item || !item->path[0])
      return;
   if (fe->view != FRONTEND_VIEW_OPEN_WITH &&
       media_path_has_open_with_choices(item->path)) {
      set_parent_view(fe);
      show_open_with(fe, item);
      return;
   }
   unifrog_log("frontend launch media path=%s\n", item->path);
   if (fe->launch_splash)
      frontend_loading_show(fe, "Media", item->name, "playing", 8);
   memset(&options, 0, sizeof(options));
   options.preset = -1;
   if (strcmp(item->core, "native") == 0) {
      options.force_native = 1;
   } else if (strcmp(item->core, "hcplayer") == 0) {
      options.force_hcplayer = 1;
   } else if (strcmp(item->core, "hcplayer-audio") == 0) {
      options.force_hcplayer = 1;
      options.force_audio = 1;
   } else if (strcmp(item->core, "hcplayer-muted") == 0) {
      options.force_hcplayer = 1;
      options.disable_audio = 1;
   }
   frontend_sound_shutdown();
   (void)unifrog_log_flush();
#if UNIFROG_HCRTOS_MEDIA_FIRMWARE
   ret = unifrog_media_play_video_ex(item->path, &options);
#else
   ret = unifrog_media_play_video_ex(item->path, &options);
#endif
   set_status(fe, "media returned %d", ret);
   (void)unifrog_ui_open(&fe->ui, 0);
   unifrog_input_clear();
}

static void launch_script(struct native_frontend *fe, struct frontend_item *item)
{
   struct stat st;
   int ret;
   int stat_ret;

   if (!item || !item->path[0])
      return;
   stat_ret = stat(item->path, &st);
   if (stat_ret != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0) {
      set_status(fe, "invalid script");
      unifrog_log("frontend script invalid path=%s errno=%d size=%ld\n",
         item->path, errno, stat_ret == 0 ? (long)st.st_size : -1l);
      return;
   }
   unifrog_log("frontend script launch path=%s size=%ld\n",
      item->path, (long)st.st_size);
   if (fe->launch_splash)
      frontend_loading_show(fe, "Script", item->name, "running", 8);
   frontend_sound_shutdown();
   (void)unifrog_log_flush();
   unifrog_ui_close(&fe->ui);
   ret = js2300_run_script_file(item->path);
   set_status(fe, "script returned %d", ret);
   (void)unifrog_ui_open(&fe->ui, 0);
   unifrog_input_clear();
}

static void launch_last_game(struct native_frontend *fe)
{
   struct frontend_item last;

   if (!fe->last_path[0]) {
      set_status(fe, "no recent game");
      return;
   }
   memset(&last, 0, sizeof(last));
   unifrog_text_copy(last.name, sizeof(last.name), basename_const(fe->last_path));
   unifrog_text_copy(last.path, sizeof(last.path), fe->last_path);
   unifrog_text_copy(last.core, sizeof(last.core), fe->last_core);
   last.kind = FRONTEND_ITEM_GAME;
   launch_game(fe, &last);
}

static void change_config(struct native_frontend *fe, int dir)
{
   struct frontend_item *item;
   unsigned backlight = 0;
   unsigned selected;

   if (fe->selected >= fe->item_count)
      return;
   selected = fe->selected;
   item = &fe->items[fe->selected];
   if (strcmp(item->path, "rom_systems") == 0 ||
       strcmp(item->path, "back_config") == 0)
      return;
   if (strcmp(item->path, "audio") == 0)
      fe->run_options.audio_enabled = !fe->run_options.audio_enabled;
   else if (strcmp(item->path, "gain") == 0) {
      static const unsigned gains[] = { 0, 1, 2, 3, 4 };
      unsigned index = 1;

      for (unsigned i = 0; i < ARRAY_SIZE(gains); i++) {
         if (gains[i] == fe->run_options.audio_gain)
            index = i;
      }
      if (dir < 0)
         index = index == 0 ? ARRAY_SIZE(gains) - 1u : index - 1u;
      else
         index = (index + 1u) % ARRAY_SIZE(gains);
      fe->run_options.audio_gain = gains[index];
   }
   else if (strcmp(item->path, "cpu") == 0) {
      static const unsigned cpus[] = { 198, 297, 396, 594, 702, 756, 810 };
      unsigned index = 0;

      for (unsigned i = 0; i < ARRAY_SIZE(cpus); i++) {
         if (cpus[i] == fe->run_options.scpu_mhz)
            index = i;
      }
      if (dir < 0)
         index = index == 0 ? ARRAY_SIZE(cpus) - 1u : index - 1u;
      else
         index = (index + 1u) % ARRAY_SIZE(cpus);
      fe->run_options.scpu_mhz = cpus[index];
   } else if (strcmp(item->path, "ge_clock") == 0) {
      static const int clocks[] = { -1, 0, 1, 2, 3 };
      unsigned index = 0;

      for (unsigned i = 0; i < ARRAY_SIZE(clocks); i++) {
         if (clocks[i] == fe->run_options.ge_clock)
            index = i;
      }
      if (dir < 0)
         index = index == 0 ? ARRAY_SIZE(clocks) - 1u : index - 1u;
      else
         index = (index + 1u) % ARRAY_SIZE(clocks);
      fe->run_options.ge_clock = clocks[index];
   } else if (strcmp(item->path, "frameskip") == 0) {
      fe->run_options.frameskip++;
      if (fe->run_options.frameskip > UNIFROG_LIBRETRO_FRAMESKIP_FIXED_2)
         fe->run_options.frameskip = UNIFROG_LIBRETRO_FRAMESKIP_OFF;
   } else if (strcmp(item->path, "display") == 0) {
      fe->run_options.display_mode++;
      if (fe->run_options.display_mode > UNIFROG_LIBRETRO_DISPLAY_ORIGINAL)
         fe->run_options.display_mode = UNIFROG_LIBRETRO_DISPLAY_FIT;
   } else if (strcmp(item->path, "keymap") == 0) {
      static const int profiles[] = {
         UNIFROG_LIBRETRO_INPUT_DEFAULT,
         UNIFROG_LIBRETRO_INPUT_RETROARCH,
         UNIFROG_LIBRETRO_INPUT_GENESIS,
         UNIFROG_LIBRETRO_INPUT_SWAP_AB,
         UNIFROG_LIBRETRO_INPUT_SWAP_XY,
      };
      unsigned index = 0;

      for (unsigned i = 0; i < ARRAY_SIZE(profiles); i++) {
         if (profiles[i] == fe->run_options.input_profile)
            index = i;
      }
      if (dir < 0)
         index = index == 0 ? ARRAY_SIZE(profiles) - 1u : index - 1u;
      else
         index = (index + 1u) % ARRAY_SIZE(profiles);
      fe->run_options.input_profile = profiles[index];
   } else if (strcmp(item->path, "state_slot") == 0) {
      if (dir < 0)
         fe->run_options.state_slot =
            fe->run_options.state_slot == 0 ? 9u :
            fe->run_options.state_slot - 1u;
      else
         fe->run_options.state_slot =
            (fe->run_options.state_slot + 1u) % 10u;
   } else if (strcmp(item->path, "state_auto_load") == 0) {
      fe->run_options.state_auto_load = !fe->run_options.state_auto_load;
   } else if (strcmp(item->path, "state_auto_save") == 0) {
      fe->run_options.state_auto_save = !fe->run_options.state_auto_save;
   } else if (strcmp(item->path, "backlight") == 0) {
      if (unifrog_backlight_get(&backlight) != 0)
         backlight = 40;
      backlight = dir < 0 ? (backlight <= 10 ? 100 : backlight - 10) :
         (backlight >= 100 ? 10 : backlight + 10);
      (void)unifrog_backlight_set(backlight);
      fe->run_options.backlight_level = (int)backlight;
   } else if (strcmp(item->path, "back") == 0) {
      show_launch(fe);
      return;
   }
   save_settings(fe);
   show_launch_settings(fe);
   fe->selected = selected;
   clamp_selection(fe);
}

static void activate(struct native_frontend *fe)
{
   struct frontend_item item;
   unsigned selected;

   if (fe->selected >= fe->item_count)
      return;
   selected = fe->selected;
   item = fe->items[selected];
   unifrog_log("frontend activate view=%d selected=%u name=%s path=%s kind=%d\n",
      fe->view, selected, item.name, item.path, item.kind);
   if (fe->view == FRONTEND_VIEW_LAUNCH_SETTINGS) {
      if (strcmp(item.path, "rom_systems") == 0) {
         clear_parent_view(fe);
         show_rom_systems(fe);
         return;
      }
      if (strcmp(item.path, "back_config") == 0) {
         restore_parent_view(fe, FRONTEND_VIEW_CONFIG);
         return;
      }
      change_config(fe, 1);
      return;
   }
   if (fe->view == FRONTEND_VIEW_OPEN_WITH) {
      struct frontend_item pending = fe->pending_open_item;

      if (strcmp(item.path, "open_with_core") == 0 &&
          fe->pending_open_valid) {
         unifrog_text_copy(pending.core, sizeof(pending.core), item.core);
         launch_game(fe, &pending);
         return;
      }
      if (strcmp(item.path, "open_with_media_native") == 0 &&
          fe->pending_open_valid) {
         unifrog_text_copy(pending.core, sizeof(pending.core), "native");
         launch_media(fe, &pending);
         return;
      }
      if (strcmp(item.path, "open_with_media_hcplayer") == 0 &&
          fe->pending_open_valid) {
         unifrog_text_copy(pending.core, sizeof(pending.core), "hcplayer");
         launch_media(fe, &pending);
         return;
      }
      if (strcmp(item.path, "open_with_media_hcplayer_audio") == 0 &&
          fe->pending_open_valid) {
         unifrog_text_copy(pending.core, sizeof(pending.core),
            "hcplayer-audio");
         launch_media(fe, &pending);
         return;
      }
      if (strcmp(item.path, "open_with_media_hcplayer_muted") == 0 &&
          fe->pending_open_valid) {
         unifrog_text_copy(pending.core, sizeof(pending.core),
            "hcplayer-muted");
         launch_media(fe, &pending);
         return;
      }
      if (strcmp(item.path, "back") == 0) {
         browser_back(fe);
         return;
      }
   }
   if (item.kind == FRONTEND_ITEM_GAME) {
      launch_game(fe, &item);
      return;
   }
   if (item.kind == FRONTEND_ITEM_MEDIA) {
      launch_media(fe, &item);
      return;
   }
   if (item.kind == FRONTEND_ITEM_SCRIPT) {
      launch_script(fe, &item);
      return;
   }
   if (item.kind == FRONTEND_ITEM_FIRMWARE) {
      char rel[FRONTEND_MAX_PATH];
      int supported;
      int ret;

      if (sd_relative_path(rel, sizeof(rel), item.path) != 0) {
         set_status(fe, "not on SD root");
         unifrog_log("frontend firmware invalid_full path=%s\n", item.path);
         return;
      }
      supported = unifrog_boot_asd_path_supported(rel);
      unifrog_log("frontend firmware boot full=%s relative=%s supported=%d\n",
         item.path, rel, supported);
      if (!supported) {
         set_status(fe, "unsupported .asd name");
         return;
      }
      frontend_loading_show(fe, "Firmware", item.name, "rebooting", 90);
      ret = unifrog_boot_asd_path(rel);
      set_status(fe, "firmware boot failed %d", ret);
      return;
   }
   if (item.kind == FRONTEND_ITEM_THEME_ARCHIVE) {
      char installed[96];
      struct frontend_install_progress progress;
      int ret;

      memset(&progress, 0, sizeof(progress));
      progress.fe = fe;
      unifrog_text_copy(progress.title, sizeof(progress.title), "Theme");
      unifrog_text_copy(progress.name, sizeof(progress.name), item.name);
      frontend_install_progress_update(&progress, "installing", 0, 100);
      unifrog_log("frontend theme archive activate path=%s name=%s\n",
         item.path, item.name);
      ret = install_theme_archive(item.path, installed, sizeof(installed),
         frontend_install_progress_update, &progress);
      unifrog_log("frontend theme archive activate done path=%s ret=%d installed=%s\n",
         item.path, ret, ret == 0 ? installed : "");
      if (ret == 0) {
         unifrog_text_copy(fe->theme_name, sizeof(fe->theme_name), installed);
         fe->resource_cache_key[0] = '\0';
         fe->theme_loaded = 0;
         load_theme(fe);
         save_settings(fe);
         set_status(fe, "installed %s", installed);
      } else {
         set_status(fe, "theme install failed %d", ret);
      }
      show_theme_list(fe);
      return;
   }
   if (item.kind == FRONTEND_ITEM_UPDATE_ARCHIVE) {
      char slot[64];
      int ret;

      frontend_loading_show(fe, "Update", item.name, "installing", 5);
      ret = install_update_archive(item.path, slot, sizeof(slot));
      if (ret == 0)
         set_status(fe, "installed %s", slot);
      else
         set_status(fe, "update install failed %d", ret);
      show_updates(fe);
      return;
   }
   if (item.kind == FRONTEND_ITEM_VERSION) {
      int ret;

      frontend_loading_show(fe, "Update", item.name, "activating", 50);
      ret = activate_installed_version(item.core[0] ? item.core : item.name);
      if (ret == 0) {
         frontend_loading_show(fe, "Update", item.name, "rebooting", 95);
         (void)unifrog_log_flush();
         unifrog_boot_reboot();
      }
      set_status(fe, "activation failed %d", ret);
      show_updates(fe);
      return;
   }
   if (item.kind == FRONTEND_ITEM_CORE_MODULE) {
      show_core_info(fe, item.path);
      return;
   }
   if (item.kind == FRONTEND_ITEM_DIR) {
      if (item.path[0] && is_content_file(item.path)) {
         item.kind = catalog_for_path(item.path) ? FRONTEND_ITEM_GAME :
            FRONTEND_ITEM_MEDIA;
         unifrog_text_copy(item.core, sizeof(item.core),
            catalog_for_path(item.path) ? catalog_for_path(item.path)->core :
            "");
         if (item.kind == FRONTEND_ITEM_GAME)
            launch_game(fe, &item);
         else
            launch_media(fe, &item);
      } else if (item.path[0]) {
         nav_push(fe);
         if (fe->view == FRONTEND_VIEW_FIRMWARE)
            show_firmware_browser(fe, item.path);
         else if (fe->view == FRONTEND_VIEW_SCRIPTS)
            show_script_browser(fe, item.path);
         else
            show_explore(fe, item.path);
      } else {
         browser_back(fe);
      }
      return;
   }
   if (strcmp(item.path, "explore") == 0) {
      clear_parent_view(fe);
      nav_reset(fe);
      show_rom_systems(fe);
   } else if (strcmp(item.path, "history") == 0) {
      set_parent_view(fe);
      nav_reset(fe);
      show_file_list(fe, "History", FRONTEND_HISTORY_PATH, FRONTEND_VIEW_HISTORY);
   } else if (strcmp(item.path, "favorites") == 0) {
      set_parent_view(fe);
      nav_reset(fe);
      show_file_list(fe, "Collection", FRONTEND_FAVORITES_PATH,
         FRONTEND_VIEW_FAVORITES);
   } else if (strcmp(item.path, "config") == 0) {
      clear_parent_view(fe);
      show_config(fe);
   } else if (strcmp(item.path, "connect") == 0) {
      set_parent_view(fe);
      show_connect(fe);
   } else if (strcmp(item.path, "custom") == 0) {
      set_parent_view(fe);
      show_custom(fe);
   } else if (strcmp(item.path, "theme") == 0) {
      set_parent_view(fe);
      show_theme_list(fe);
   } else if (strcmp(item.path, "language") == 0) {
      set_parent_view(fe);
      show_language_list(fe);
   } else if (strcmp(item.path, "interface") == 0) {
      set_parent_view(fe);
      show_visual(fe);
   } else if (strcmp(item.path, "launch_settings") == 0) {
      set_parent_view(fe);
      show_launch_settings(fe);
   } else if (strcmp(item.path, "power") == 0) {
      set_parent_view(fe);
      show_power(fe);
   } else if (strcmp(item.path, "storage") == 0) {
      set_parent_view(fe);
      show_storage(fe);
   } else if (strcmp(item.path, "storage_mode") == 0) {
      show_storage_mode(fe);
   } else if (strcmp(item.path, "info") == 0) {
      clear_parent_view(fe);
      show_info(fe);
   } else if (strcmp(item.path, "sysinfo") == 0) {
      set_parent_view(fe);
      show_sysinfo(fe);
   } else if (strcmp(item.path, "apps") == 0) {
      clear_parent_view(fe);
      show_apps(fe);
   } else if (strcmp(item.path, "scripts") == 0) {
      set_parent_view(fe);
      nav_reset(fe);
      show_script_browser(fe, FRONTEND_SCRIPT_ROOT);
   } else if (strcmp(item.path, "updates") == 0) {
      set_parent_view(fe);
      show_updates(fe);
   } else if (strcmp(item.path, "cores") == 0) {
      set_parent_view(fe);
      show_core_manager(fe);
   } else if (strcmp(item.path, "package_check") == 0) {
      set_parent_view(fe);
      show_package_check(fe);
   } else if (strcmp(item.path, "resume") == 0) {
      launch_last_game(fe);
   } else if (strcmp(item.path, "flush_log") == 0) {
      size_t pending = unifrog_log_pending();

      unifrog_log("frontend log marker view=%d title=%s pending_before=%lu\n",
         fe->view, fe->title, (unsigned long)pending);
      set_status(fe, "log marked %lu bytes", (unsigned long)pending);
   } else if (strcmp(item.path, "storage_profile") == 0) {
      show_storage_confirm(fe, item.core[0] ? item.core : "boot");
   } else if (strcmp(item.path, "theme_select") == 0) {
      unifrog_text_copy(fe->theme_name, sizeof(fe->theme_name),
         item.core[0] ? item.core : "muos");
      load_theme(fe);
      save_settings(fe);
      set_status(fe, "theme %s", active_theme_label(fe));
      show_theme_list(fe);
      restore_view_selection(fe, selected, fe->scroll);
   } else if (strcmp(item.path, "language_select") == 0) {
      unifrog_text_copy(fe->language_name, sizeof(fe->language_name),
         item.core[0] ? item.core : "english");
      if (strcmp(fe->language_name, "english") == 0)
         fe->language_index = 0;
      load_language(fe);
      load_theme(fe);
      save_settings(fe);
      set_status(fe, "language %s", active_language_label(fe));
      restore_parent_view(fe, FRONTEND_VIEW_CONFIG);
   } else if (strcmp(item.path, "storage_apply_pending") == 0) {
      char previous_profile[sizeof(fe->storage_profile)];
      int ret;

      if (!fe->storage_pending_profile[0])
         unifrog_text_copy(fe->storage_pending_profile,
            sizeof(fe->storage_pending_profile), "boot");
      unifrog_text_copy(previous_profile, sizeof(previous_profile),
         fe->storage_profile);
      unifrog_text_copy(fe->storage_profile, sizeof(fe->storage_profile),
         fe->storage_pending_profile);
      frontend_loading_show(fe, "Storage", fe->storage_profile,
         storage_profile_label(fe->storage_profile), 20);
      ret = apply_storage_profile(fe, "menu");
      if (ret == 0) {
         save_settings(fe);
      } else {
         unifrog_text_copy(fe->storage_profile, sizeof(fe->storage_profile),
            previous_profile);
         (void)apply_storage_profile(fe, "menu-rollback");
         set_status(fe, "SD switch failed %d", ret);
      }
      show_storage_mode(fe);
   } else if (strcmp(item.path, "storage_recover") == 0) {
      int ret = unifrog_platform_recover_storage("native_frontend", 4, 100);
      set_status(fe, "storage recover %d", ret);
   } else if (strcmp(item.path, "storage_fast_probe") == 0) {
      char summary[64];
      int ret;

      frontend_loading_show(fe, "Fast SD Probe", "Testing profiles",
         "Report in /unifrog", 5);
      ret = unifrog_storage_fast_probe_run(fast_probe_progress_cb, fe,
         summary, sizeof(summary));
      set_status(fe, "fast SD probe %d  %s", ret, summary);
   } else if (strcmp(item.path, "battery_refresh") == 0) {
      int ret = unifrog_battery_update(&fe->battery, 0);
      set_status(fe, "battery refresh %d", ret);
      show_power(fe);
      fe->selected = selected;
      clamp_selection(fe);
   } else if (strcmp(item.path, "sort") == 0) {
      fe->sort_desc = !fe->sort_desc;
      save_settings(fe);
      show_visual(fe);
      fe->selected = selected;
      clamp_selection(fe);
   } else if (strcmp(item.path, "hidden") == 0) {
      fe->show_hidden = !fe->show_hidden;
      save_settings(fe);
      show_visual(fe);
      fe->selected = selected;
      clamp_selection(fe);
   } else if (strcmp(item.path, "folder_counts") == 0) {
      fe->folder_counts = !fe->folder_counts;
      save_settings(fe);
      show_visual(fe);
      fe->selected = selected;
      clamp_selection(fe);
   } else if (strcmp(item.path, "empty_folder") == 0) {
      fe->display_empty_folder = !fe->display_empty_folder;
      save_settings(fe);
      show_visual(fe);
      fe->selected = selected;
      clamp_selection(fe);
   } else if (strcmp(item.path, "counter_folder") == 0) {
      fe->menu_counter_folder = !fe->menu_counter_folder;
      save_settings(fe);
      show_visual(fe);
      fe->selected = selected;
      clamp_selection(fe);
   } else if (strcmp(item.path, "counter_file") == 0) {
      fe->menu_counter_file = !fe->menu_counter_file;
      save_settings(fe);
      show_visual(fe);
      fe->selected = selected;
      clamp_selection(fe);
   } else if (strcmp(item.path, "content_collect") == 0) {
      fe->content_collect = !fe->content_collect;
      save_settings(fe);
      show_visual(fe);
      fe->selected = selected;
      clamp_selection(fe);
   } else if (strcmp(item.path, "content_history") == 0) {
      fe->content_history = !fe->content_history;
      save_settings(fe);
      show_visual(fe);
      fe->selected = selected;
      clamp_selection(fe);
   } else if (strcmp(item.path, "mixed_content") == 0) {
      fe->mixed_content = !fe->mixed_content;
      save_settings(fe);
      show_visual(fe);
      fe->selected = selected;
      clamp_selection(fe);
   } else if (strcmp(item.path, "clock") == 0) {
      fe->clock_enabled = !fe->clock_enabled;
      save_settings(fe);
      show_visual(fe);
      fe->selected = selected;
      clamp_selection(fe);
   } else if (strcmp(item.path, "title_root") == 0) {
      fe->title_include_root = !fe->title_include_root;
      save_settings(fe);
      show_visual(fe);
      fe->selected = selected;
      clamp_selection(fe);
   } else if (strcmp(item.path, "theme_alternate") == 0) {
      fe->theme_alternate = !fe->theme_alternate;
      load_theme(fe);
      save_settings(fe);
      show_custom(fe);
      fe->selected = selected;
      clamp_selection(fe);
   } else if (strcmp(item.path, "boxart_hide") == 0) {
      fe->boxart_hidden = !fe->boxart_hidden;
      save_settings(fe);
      show_custom(fe);
      fe->selected = selected;
      clamp_selection(fe);
   } else if (strcmp(item.path, "launch_splash") == 0) {
      fe->launch_splash = !fe->launch_splash;
      save_settings(fe);
      show_custom(fe);
      fe->selected = selected;
      clamp_selection(fe);
   } else if (strcmp(item.path, "sound") == 0) {
      fe->sound_enabled = !fe->sound_enabled;
      save_settings(fe);
      show_custom(fe);
      fe->selected = selected;
      clamp_selection(fe);
   } else if (strcmp(item.path, "log_flush_every") == 0) {
      fe->log_flush_every = !fe->log_flush_every;
      unifrog_log_set_auto_flush_bytes(fe->log_flush_every ? 1u :
         (size_t)UNIFROG_LOG_AUTO_FLUSH_BYTES);
      save_settings(fe);
      set_status(fe, "log flush %s", fe->log_flush_every ? "every log" :
         "buffered");
      show_custom(fe);
      fe->selected = selected;
      clamp_selection(fe);
   } else if (strcmp(item.path, "screenshot") == 0) {
      unifrog_diag_memory_snapshot("muos.info.screenshot");
      set_status(fe, "snapshot logged");
   } else if (strcmp(item.path, "news") == 0) {
      set_status(fe, "news unavailable offline");
   } else if (strcmp(item.path, "netinfo") == 0) {
      set_status(fe, "network unsupported on SF2000");
   } else if (strcmp(item.path, "chrony") == 0) {
      set_status(fe, "uptime %lus",
         (unsigned long)(unifrog_perf_time_ms() / 1000u));
   } else if (strcmp(item.path, "explore_sd") == 0) {
      clear_parent_view(fe);
      nav_reset(fe);
      show_explore(fe, FRONTEND_ROOT);
   } else if (strcmp(item.path, "explore_unifrog") == 0) {
      clear_parent_view(fe);
      nav_reset(fe);
      show_explore(fe, FRONTEND_DIST_ROOT);
   } else if (strcmp(item.path, "explore_data") == 0) {
      clear_parent_view(fe);
      nav_reset(fe);
      show_explore(fe, FRONTEND_DATA_ROOT);
   } else if (strcmp(item.path, "explore_saves") == 0) {
      clear_parent_view(fe);
      nav_reset(fe);
      show_explore(fe, FRONTEND_DATA_ROOT "/saves");
   } else if (strcmp(item.path, "explore_bios") == 0) {
      clear_parent_view(fe);
      nav_reset(fe);
      show_explore(fe, FRONTEND_ROOT "/bios");
   } else if (strcmp(item.path, "firmware") == 0) {
      show_firmware(fe);
   } else if (strcmp(item.path, "firmware_stock") == 0) {
      frontend_loading_show(fe, "Firmware", "stock", "rebooting", 90);
      (void)unifrog_log_flush();
      (void)unifrog_boot_firmware_asd("stock");
      set_status(fe, "firmware boot failed");
   } else if (strcmp(item.path, "firmware_unifrog") == 0) {
      frontend_loading_show(fe, "Firmware", "unifrog", "rebooting", 90);
      (void)unifrog_log_flush();
      (void)unifrog_boot_firmware_asd("unifrog");
      set_status(fe, "firmware boot failed");
   } else if (strcmp(item.path, "reboot") == 0) {
      frontend_loading_show(fe, "Reboot", "system", "rebooting", 90);
      (void)unifrog_log_flush();
      unifrog_boot_reboot();
   } else if (strcmp(item.path, "shutdown") == 0) {
      set_status(fe, "shutdown unsupported; use reboot");
   } else if (strcmp(item.path, "back_config") == 0) {
      restore_parent_view(fe, FRONTEND_VIEW_CONFIG);
   } else if (strcmp(item.path, "back_apps") == 0) {
      show_apps(fe);
   } else if (strcmp(item.path, "back_storage") == 0) {
      show_storage(fe);
   } else if (strcmp(item.path, "colour") == 0 ||
              strcmp(item.path, "overlay") == 0 ||
              strcmp(item.path, "backup") == 0 ||
              strcmp(item.path, "noop") == 0) {
      set_status(fe, "%s unavailable on SF2000", item.name);
   } else if (strcmp(item.path, "back") == 0) {
      if (fe->view == FRONTEND_VIEW_APPS)
         show_launch(fe);
      else
         show_launch(fe);
   }
}

static void draw(struct native_frontend *fe)
{
   char detail[48];
   uint32_t now = unifrog_perf_time_ms();
   unsigned end;

   fe->needs_draw = 0;
   if (now - fe->battery_ms > 5000u) {
      (void)unifrog_battery_update(&fe->battery, 0);
      fe->battery_ms = now;
   }
   if ((fe->view == FRONTEND_VIEW_EXPLORE || fe->view == FRONTEND_VIEW_FIRMWARE) &&
       fe->selected < fe->item_count &&
       ((fe->items[fe->selected].kind == FRONTEND_ITEM_DIR &&
         !fe->menu_counter_folder) ||
        (fe->items[fe->selected].kind != FRONTEND_ITEM_DIR &&
         !fe->menu_counter_file))) {
      detail[0] = '\0';
   } else {
      snprintf(detail, sizeof(detail), "%u/%u",
         fe->item_count ? fe->selected + 1u : 0u, fe->item_count);
   }
   if (fe->battery.available) {
      char with_battery[48];

      if (detail[0])
         snprintf(with_battery, sizeof(with_battery), "%.38s  %u%%", detail,
            fe->battery.bars * 25u);
      else
         snprintf(with_battery, sizeof(with_battery), "%u%%",
            fe->battery.bars * 25u);
      unifrog_text_copy(detail, sizeof(detail), with_battery);
   }
   {
      char with_storage[48];
      const char *sd = unifrog_platform_sd_active_profile();

      if (detail[0])
         snprintf(with_storage, sizeof(with_storage), "%.30s  SD:%s",
            detail, sd);
      else
         snprintf(with_storage, sizeof(with_storage), "SD:%s", sd);
      unifrog_text_copy(detail, sizeof(detail), with_storage);
   }
   {
      uint32_t signature = 2166136261u;

      signature = frontend_hash_u32(signature, (uint32_t)fe->view);
      signature = frontend_hash_u32(signature, (uint32_t)fe->selected);
      signature = frontend_hash_u32(signature, (uint32_t)fe->scroll);
      signature = frontend_hash_u32(signature, (uint32_t)fe->item_count);
      signature = frontend_hash_u32(signature, fe->item_generation);
      signature = frontend_hash_u32(signature, (uint32_t)fe->applied_style_id);
      signature = frontend_hash_string(signature, fe->title);
      signature = frontend_hash_string(signature, fe->status);
      signature = frontend_hash_string(signature, detail);
      signature = frontend_hash_string(signature, fe->resource_cache_key);
      if (fe->last_draw_valid && fe->last_draw_signature == signature)
         return;
      fe->last_draw_signature = signature;
      fe->last_draw_valid = 1;
   }
   if (fe->view == FRONTEND_VIEW_LAUNCH) {
      apply_frontend_style(fe, (int)UNIFROG_FRONTEND_LVGL_LAUNCH,
         frontend_screen_style(fe, UNIFROG_FRONTEND_LVGL_LAUNCH));
      if (unifrog_frontend_lvgl_draw_launcher(&fe->ui, fe->theme, fe->selected,
            detail, fe->status[0] ? fe->status :
            (fe->last_path[0] ? "A open  SELECT+A resume" : NULL)) == 0)
         return;

      unifrog_ui_begin(&fe->ui, fe->theme->background);
      unifrog_ui_header(&fe->ui, fe->theme, fe->title, detail);
      struct unifrog_surface surface = unifrog_ui_surface(&fe->ui);
      unsigned visible = fe->item_count < 8u ? fe->item_count : 8u;
      unsigned page = visible ? fe->selected / visible : 0;
      unsigned start = page * visible;
      unsigned stop = start + visible;

      if (visible == 0)
         stop = 0;
      if (stop > fe->item_count)
         stop = fe->item_count;
      unifrog_gfx_fill_rect(&surface, 0, 36, surface.width, 1,
         fe->theme->accent);
      for (unsigned i = start; i < stop; i++) {
         unsigned local = i - start;
         int col = (int)(local % 4u);
         int row = (int)(local / 4u);
         int x = 12 + col * 76;
         int y = 54 + row * 72;
         int focused = i == fe->selected;
         uint16_t tile = focused ? fe->theme->focus : fe->theme->panel;
         uint16_t icon = focused ? fe->theme->accent : UNIFROG_RGB565(84, 94, 104);
         char glyph[2] = { fe->items[i].name[0], '\0' };

         unifrog_gfx_fill_rect(&surface, x, y, 68, 58, tile);
         unifrog_gfx_fill_rect(&surface, x + 4, y + 4, 60, 26, icon);
         unifrog_gfx_draw_text(&surface, x + 28, y + 12, glyph,
            UNIFROG_RGB565(8, 9, 12), 1);
         unifrog_ui_text_clipped(&fe->ui, x + 6, y + 37, 10,
            fe->items[i].name, focused ? UNIFROG_RGB565(255, 255, 255) :
            fe->theme->text, 1);
      }
      if (fe->item_count > visible) {
         char page_text[24];

         snprintf(page_text, sizeof(page_text), "%u-%u/%u", start + 1u,
            stop, fe->item_count);
         unifrog_ui_text_clipped(&fe->ui, 132, 204, 12, page_text,
            fe->theme->muted, 1);
      }
      unifrog_ui_footer(&fe->ui, fe->theme,
         fe->status[0] ? fe->status :
         (fe->last_path[0] ? "A open  SELECT+A resume" : "A open  L/R page"),
         "B back");
      unifrog_ui_present(&fe->ui);
      return;
   }
   if (fe->view == FRONTEND_VIEW_CONFIG || fe->view == FRONTEND_VIEW_INFO ||
       fe->view == FRONTEND_VIEW_POWER || fe->view == FRONTEND_VIEW_SYSINFO ||
       fe->view == FRONTEND_VIEW_CONNECT || fe->view == FRONTEND_VIEW_CUSTOM ||
       fe->view == FRONTEND_VIEW_VISUAL || fe->view == FRONTEND_VIEW_STORAGE ||
       fe->view == FRONTEND_VIEW_UPDATES || fe->view == FRONTEND_VIEW_CORES ||
       fe->view == FRONTEND_VIEW_CORE_INFO ||
       fe->view == FRONTEND_VIEW_PACKAGE_CHECK) {
      enum unifrog_frontend_lvgl_screen screen = UNIFROG_FRONTEND_LVGL_CONFIG;
      const struct unifrog_frontend_lvgl_style *style;
      const char *labels[FRONTEND_MAX_ITEMS];
      const char *values[FRONTEND_MAX_ITEMS];
      unsigned glyph_start;
      unsigned glyph_stop;

      if (fe->view == FRONTEND_VIEW_CONNECT)
         screen = UNIFROG_FRONTEND_LVGL_CONNECT;
      else if (fe->view == FRONTEND_VIEW_CUSTOM)
         screen = UNIFROG_FRONTEND_LVGL_CUSTOM;
      else if (fe->view == FRONTEND_VIEW_INFO)
         screen = UNIFROG_FRONTEND_LVGL_INFO;
      else if (fe->view == FRONTEND_VIEW_POWER)
         screen = UNIFROG_FRONTEND_LVGL_POWER;
      else if (fe->view == FRONTEND_VIEW_STORAGE)
         screen = UNIFROG_FRONTEND_LVGL_STORAGE;
      else if (fe->view == FRONTEND_VIEW_UPDATES)
         screen = UNIFROG_FRONTEND_LVGL_STORAGE;
      else if (fe->view == FRONTEND_VIEW_CORES ||
          fe->view == FRONTEND_VIEW_CORE_INFO ||
          fe->view == FRONTEND_VIEW_PACKAGE_CHECK)
         screen = UNIFROG_FRONTEND_LVGL_STORAGE;
      else if (fe->view == FRONTEND_VIEW_SYSINFO)
         screen = UNIFROG_FRONTEND_LVGL_SYSINFO;
      else if (fe->view == FRONTEND_VIEW_VISUAL)
         screen = UNIFROG_FRONTEND_LVGL_VISUAL;
      style = frontend_screen_style(fe, screen);
      apply_frontend_style(fe, (int)screen, style);
      for (unsigned i = 0; i < fe->item_count; i++) {
         labels[i] = fe->items[i].name;
         values[i] = fe->items[i].meta;
      }
      visible_item_range(fe->item_count, fe->selected,
         visible_rows_for_style(style), &glyph_start, &glyph_stop);
      fill_visible_item_glyphs(fe, lvgl_screen_module(screen), glyph_start,
         glyph_stop,
         fe->item_glyph_path, fe->item_glyph);
      if (unifrog_frontend_lvgl_draw_menu(&fe->ui, fe->theme, screen, fe->title,
            fe->selected, detail,
            fe->status[0] ? fe->status : "A select  B back", labels, values,
            fe->item_glyph, fe->item_count) == 0)
         return;
   }
   {
      const char *labels[FRONTEND_MAX_ITEMS];
      const char *values[FRONTEND_MAX_ITEMS];
      const struct unifrog_frontend_lvgl_style *style;
      unsigned glyph_start;
      unsigned glyph_stop;

      style = frontend_view_style(fe, fe->view);
      apply_frontend_style(fe, 100 + (int)fe->view, style);
      for (unsigned i = 0; i < fe->item_count; i++) {
         labels[i] = fe->items[i].name;
         values[i] = fe->items[i].meta;
      }
      visible_item_range(fe->item_count, fe->selected,
         visible_rows_for_style(style), &glyph_start, &glyph_stop);
      fill_visible_item_glyphs(fe, list_view_glyph_module(fe->view),
         glyph_start, glyph_stop,
         fe->item_glyph_path, fe->item_glyph);
      if (unifrog_frontend_lvgl_draw_list(&fe->ui, fe->theme, fe->title,
            fe->selected, detail,
            fe->status[0] ? fe->status : "A select  L/R page  Y jump",
            labels, values, fe->item_glyph, fe->item_count) == 0)
         return;
   }
   unifrog_ui_begin(&fe->ui, fe->theme->background);
   unifrog_ui_header(&fe->ui, fe->theme, fe->title, detail);
   end = fe->scroll + FRONTEND_ROWS;
   if (end > fe->item_count)
      end = fe->item_count;
   for (unsigned i = fe->scroll; i < end; i++) {
      char meta[72];

      unifrog_text_copy(meta, sizeof(meta), fe->items[i].meta);
      if (fe->items[i].kind == FRONTEND_ITEM_GAME && is_favorite(fe->items[i].path)) {
         char marked[72];

         snprintf(marked, sizeof(marked), "* %.68s", meta);
         unifrog_text_copy(meta, sizeof(meta), marked);
      }
      unifrog_ui_list_row(&fe->ui, fe->theme, 48 + (int)(i - fe->scroll) * 22,
         fe->items[i].name, meta, i == fe->selected);
   }
   if (fe->item_count == 0)
      unifrog_ui_text(&fe->ui, 20, 72, "No entries", fe->theme->muted, 1);
   unifrog_ui_footer(&fe->ui, fe->theme,
      fe->status[0] ? fe->status : "A launch  L/R page  Y jump",
      "B back");
   unifrog_ui_present(&fe->ui);
}

static void loop_once(struct native_frontend *fe)
{
   uint32_t now = unifrog_perf_time_ms();
   int nav_handled = 0;
   int select_down;
   int combo_handled = 0;

   unifrog_ui_poll(&fe->ui);
   select_down = unifrog_ui_down(&fe->ui, UNIFROG_UI_SELECT);
   if (now - fe->battery_ms > 5000u) {
      (void)unifrog_battery_update(&fe->battery, 0);
      fe->battery_ms = now;
      fe->needs_draw = 1;
   }
   if (select_down &&
       (unifrog_ui_pressed(&fe->ui, UNIFROG_UI_A) ||
        (unifrog_ui_down(&fe->ui, UNIFROG_UI_A) &&
         unifrog_ui_pressed(&fe->ui, UNIFROG_UI_SELECT)))) {
      unifrog_log("frontend shortcut action=last_game combo=SELECT+A\n");
      launch_last_game(fe);
      combo_handled = 1;
   } else if (select_down &&
       (unifrog_ui_pressed(&fe->ui, UNIFROG_UI_Y) ||
        (unifrog_ui_down(&fe->ui, UNIFROG_UI_Y) &&
         unifrog_ui_pressed(&fe->ui, UNIFROG_UI_SELECT)))) {
      int ret;

      unifrog_log("frontend shortcut action=flush_logs combo=SELECT+Y\n");
      ret = unifrog_log_flush_force();
      set_status(fe, "log flush ret %d", ret);
      combo_handled = 1;
   } else if (select_down &&
       (unifrog_ui_pressed(&fe->ui, UNIFROG_UI_X) ||
        (unifrog_ui_down(&fe->ui, UNIFROG_UI_X) &&
         unifrog_ui_pressed(&fe->ui, UNIFROG_UI_SELECT)))) {
      unifrog_frontend_lvgl_request_screenshot();
      set_status(fe, "screenshot log queued");
      combo_handled = 1;
   }

   if (!combo_handled &&
       unifrog_ui_repeated(&fe->ui, UNIFROG_UI_UP, now, 420, 140)) {
      move_selection(fe, -1);
      nav_handled = 1;
   } else if (!combo_handled &&
       unifrog_ui_repeated(&fe->ui, UNIFROG_UI_DOWN, now, 420, 140)) {
      move_selection(fe, 1);
      nav_handled = 1;
   }
   if (!combo_handled && !nav_handled &&
       unifrog_ui_pressed(&fe->ui, UNIFROG_UI_L)) {
      for (unsigned i = 0; i < FRONTEND_ROWS; i++)
         move_selection(fe, -1);
      nav_handled = 1;
   }
   if (!combo_handled && !nav_handled &&
       unifrog_ui_pressed(&fe->ui, UNIFROG_UI_R)) {
      for (unsigned i = 0; i < FRONTEND_ROWS; i++)
         move_selection(fe, 1);
      nav_handled = 1;
   }
   if (!combo_handled && unifrog_ui_pressed(&fe->ui, UNIFROG_UI_Y)) {
      if (fe->selected < fe->item_count &&
          fe->items[fe->selected].kind == FRONTEND_ITEM_GAME)
         toggle_favorite(fe, &fe->items[fe->selected]);
      else if (fe->view == FRONTEND_VIEW_EXPLORE || fe->view == FRONTEND_VIEW_FIRMWARE ||
          fe->view == FRONTEND_VIEW_HISTORY || fe->view == FRONTEND_VIEW_FAVORITES)
         jump_selection_group(fe, 1);
   }
   if (!combo_handled && unifrog_ui_pressed(&fe->ui, UNIFROG_UI_B))
      browser_back(fe);
   if (!combo_handled && fe->view == FRONTEND_VIEW_LAUNCH_SETTINGS &&
       unifrog_ui_pressed(&fe->ui, UNIFROG_UI_LEFT)) {
      change_config(fe, -1);
      fe->needs_draw = 1;
   }
   if (!combo_handled && fe->view == FRONTEND_VIEW_LAUNCH_SETTINGS &&
       unifrog_ui_pressed(&fe->ui, UNIFROG_UI_RIGHT)) {
      change_config(fe, 1);
      fe->needs_draw = 1;
   }
   if (!combo_handled && !select_down &&
       unifrog_ui_pressed(&fe->ui, UNIFROG_UI_X)) {
      clear_parent_view(fe);
      show_config(fe);
   }
   if (!combo_handled &&
       (unifrog_ui_pressed(&fe->ui, UNIFROG_UI_A) ||
        unifrog_ui_pressed(&fe->ui, UNIFROG_UI_START)))
      activate(fe);
   if (fe->needs_draw)
      draw(fe);
   usleep(16000);
}

int unifrog_native_frontend_main(void)
{
   static struct native_frontend fe;
   int ret;

   memset(&fe, 0, sizeof(fe));
   fe.theme = &frontend_theme;
   ensure_data_dirs();
   unifrog_battery_status_init(&fe.battery);
   unifrog_libretro_run_options_init(&fe.run_options);
   fe.run_options.audio_enabled = 1;
   fe.run_options.audio_gain = 1;
   fe.run_options.scpu_mhz = 702;
   fe.run_options.ge_clock = -1;
   fe.run_options.backlight_level = -1;
   fe.run_options.frameskip = UNIFROG_LIBRETRO_FRAMESKIP_AUTO;
   fe.run_options.display_mode = UNIFROG_LIBRETRO_DISPLAY_FIT;
   fe.mixed_content = 1;
   fe.display_empty_folder = 1;
   fe.menu_counter_folder = 1;
   fe.menu_counter_file = 1;
   fe.content_collect = 1;
   fe.content_history = 1;
   fe.boxart_hidden = 1;
   fe.launch_splash = 1;
   fe.log_flush_every = UNIFROG_LOG_FLUSH_EVERY ? 1 : 0;
   unifrog_text_copy(fe.theme_name, sizeof(fe.theme_name), "muos");
   unifrog_text_copy(fe.language_name, sizeof(fe.language_name), "english");
   unifrog_text_copy(fe.storage_profile, sizeof(fe.storage_profile), "boot");
   unifrog_text_copy(fe.rom_root, sizeof(fe.rom_root), FRONTEND_ROMS_ROOT);
   unifrog_text_copy(fe.rom_root_label, sizeof(fe.rom_root_label), "ROMs");
   load_settings(&fe);
   unifrog_log_set_auto_flush_bytes(fe.log_flush_every ? 1u :
      (size_t)UNIFROG_LOG_AUTO_FLUSH_BYTES);
   if (fe.language_index < 0)
      fe.language_index = 0;
   load_language(&fe);
   load_theme(&fe);
   if (fe.run_options.backlight_level >= 0)
      (void)unifrog_backlight_set((unsigned)fe.run_options.backlight_level);
   mark_boot_ok();

   ret = unifrog_ui_open(&fe.ui, unifrog_boot_logo_is_active());
   if (ret != 0) {
      printf("unifrog native_frontend fb_open failed ret=%d\n", ret);
      return ret;
   }
   printf("unifrog native_frontend start commit=%s dirty=%d theme=%s sdk=%s cores=%s media=%s\n",
      UNIFROG_GIT_COMMIT, UNIFROG_GIT_DIRTY,
      UNIFROG_NATIVE_FRONTEND_GIT_COMMIT, UNIFROG_SDK_GIT_COMMIT,
      UNIFROG_CORES_GIT_COMMIT, UNIFROG_HCRTOS_MEDIA);
   if (strcmp(fe.storage_profile, "boot") != 0) {
      frontend_loading_show(&fe, "Storage", fe.storage_profile,
         storage_profile_label(fe.storage_profile), 20);
      (void)apply_storage_profile(&fe, "startup");
   }
   show_launch(&fe);
   fe.running = 1;
   while (fe.running)
      loop_once(&fe);
   unifrog_ui_close(&fe.ui);
   return 0;
}
