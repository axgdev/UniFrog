#include "frontend_internal.h"

const struct unifrog_ui_theme frontend_theme = {
   UNIFROG_RGB565(8, 9, 12),
   UNIFROG_RGB565(20, 22, 29),
   UNIFROG_RGB565(52, 104, 132),
   UNIFROG_RGB565(238, 241, 232),
   UNIFROG_RGB565(151, 159, 157),
   UNIFROG_RGB565(238, 188, 70),
   UNIFROG_RGB565(214, 72, 77),
};

/* Private frontend language, theme, style, and loading UI helpers. */
const char *active_language_label(struct frontend_state *fe)
{
   if (fe && fe->language_name[0])
      return fe->language_name;
   return "english";
}

const char *active_theme_label(struct frontend_state *fe)
{
   if (fe && fe->theme_name[0])
      return fe->theme_name;
   return "muos";
}

const char *tr(struct frontend_state *fe, const char *key)
{
   if (!fe || !key)
      return key ? key : "";
   for (unsigned i = 0; i < fe->i18n_count; i++) {
      if (strcmp(fe->i18n_key[i], key) == 0)
         return fe->i18n_value[i];
   }
   return key;
}

void load_language(struct frontend_state *fe)
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
   if (frontend_path_join_ini(path, sizeof(path), FRONTEND_LANGUAGE_ROOT,
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
      frontend_strip_eol(line);
      if (!line[0] || line[0] == '#' || line[0] == ';')
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

const char *lvgl_screen_module(enum unifrog_frontend_lvgl_screen screen)
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

const char *list_view_glyph_module(enum frontend_view view)
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
      uint8_t alpha = frontend_parse_alpha(value, 255);

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
   if (frontend_parse_theme_hex(value, &color) != 0)
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

static int scale_scheme_dimension(int value, unsigned scale_div)
{
   if (scale_div <= 1u || value == 0)
      return value;
   if (value > 0)
      return (value + (int)scale_div / 2) / (int)scale_div;
   return -((-value + (int)scale_div / 2) / (int)scale_div);
}

static int scheme_set_layout(struct unifrog_frontend_lvgl_style *style,
   const char *section, const char *key, const char *value,
   unsigned scale_div)
{
   int v;

   if (!style || !section || !key || !value)
      return 0;
   v = frontend_parse_int(value, -32768);
   if (v == -32768)
      return 0;
   if (strcmp(section, "header") == 0 &&
       strcmp(key, "HEADER_HEIGHT") == 0)
      style->header_height = scale_scheme_dimension(v, scale_div);
   else if (strcmp(section, "footer") == 0 &&
            strcmp(key, "FOOTER_HEIGHT") == 0)
      style->footer_height = scale_scheme_dimension(v, scale_div);
   else if (strcmp(section, "list") == 0 &&
            strcmp(key, "LIST_DEFAULT_RADIUS") == 0)
      style->list_radius = scale_scheme_dimension(v, scale_div);
   else if (strcmp(section, "list") == 0 &&
            strcmp(key, "LIST_FOCUS_RADIUS") == 0)
      style->list_radius = scale_scheme_dimension(v, scale_div);
   else if (strcmp(section, "list") == 0 &&
            strcmp(key, "LIST_DEFAULT_BORDER_WIDTH") == 0)
      style->list_border_width = scale_scheme_dimension(v, scale_div);
   else if (strcmp(section, "misc") == 0 &&
            strcmp(key, "NAVIGATION_TYPE") == 0)
      style->navigation_type = v;
   else if (strcmp(section, "misc") == 0 &&
            strcmp(key, "CONTENT_ITEM_HEIGHT") == 0) {
      if (v > 0)
         style->list_h = scale_scheme_dimension(v, scale_div);
   } else if (strcmp(section, "misc") == 0 &&
            strcmp(key, "CONTENT_PADDING_LEFT") == 0)
      style->list_x = scale_scheme_dimension(v, scale_div);
   else if (strcmp(section, "misc") == 0 &&
            strcmp(key, "CONTENT_PADDING_TOP") == 0)
      style->list_y = scale_scheme_dimension(v, scale_div);
   else if (strcmp(section, "misc") == 0 &&
            strcmp(key, "CONTENT_WIDTH") == 0) {
      if (v > 0)
         style->list_w = scale_scheme_dimension(v, scale_div);
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
      style->launch_x = scale_scheme_dimension(v, scale_div);
   else if (strcmp(section, "grid") == 0 &&
            strcmp(key, "LOCATION_Y") == 0)
      style->launch_y = scale_scheme_dimension(v, scale_div);
   else if (strcmp(section, "grid") == 0 &&
            strcmp(key, "COLUMN_PADDING") == 0)
      style->launch_gap_x = scale_scheme_dimension(v, scale_div);
   else if (strcmp(section, "grid") == 0 &&
            strcmp(key, "ROW_PADDING") == 0)
      style->launch_gap_y = scale_scheme_dimension(v, scale_div);
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
   unsigned scale_div = 2;

   file = fopen(path, "rb");
   if (!file)
      return -1;
   if (strstr(path, "/320x240/scheme/"))
      scale_div = 1;
   while (fgets(line, sizeof(line), file)) {
      char *eq;

      lines++;
      frontend_strip_eol(line);
      if (!line[0] || line[0] == '#' || line[0] == ';')
         continue;
      if (line[0] == '[') {
         char *end = strchr(line + 1, ']');

         if (end) {
            *end = '\0';
            unifrog_text_copy(section, sizeof(section),
               frontend_trim_ascii(line + 1));
         }
         continue;
      }
      eq = strchr(line, '=');
      if (!eq || eq == line)
         continue;
      *eq++ = '\0';
      scheme_set_color(theme, style, section, frontend_trim_ascii(line),
         frontend_trim_ascii(eq));
      scheme_set_layout(style, section, frontend_trim_ascii(line), frontend_trim_ascii(eq),
         scale_div);
      keys++;
   }
   fclose(file);
   if (unifrog_perf_time_ms() - start_ms > 50u)
      unifrog_log("frontend theme scheme slow path=%s ms=%u lines=%u keys=%u scale=%u\n",
         path, (unsigned)(unifrog_perf_time_ms() - start_ms), lines, keys,
         scale_div);
   return 0;
}

static void theme_try_wallpaper(struct unifrog_frontend_lvgl_style *style,
   const char *dir, const char *name)
{
   char path[FRONTEND_MAX_PATH];

   if (!style || style->wallpaper[0] || !dir || !name)
      return;
   if (frontend_path_join(path, sizeof(path), dir, name) == 0 && frontend_file_exists(path))
      unifrog_text_copy(style->wallpaper, sizeof(style->wallpaper), path);
}

static void theme_override_wallpaper(struct unifrog_frontend_lvgl_style *style,
   const char *dir, const char *name)
{
   char path[FRONTEND_MAX_PATH];

   if (!style || !dir || !name)
      return;
   if (frontend_path_join(path, sizeof(path), dir, name) == 0 && frontend_file_exists(path))
      unifrog_text_copy(style->wallpaper, sizeof(style->wallpaper), path);
}

static void theme_try_static_image(struct unifrog_frontend_lvgl_style *style,
   const char *dir, const char *name)
{
   char path[FRONTEND_MAX_PATH];

   if (!style || style->static_image[0] || !dir || !name)
      return;
   if (frontend_path_join(path, sizeof(path), dir, name) == 0 && frontend_file_exists(path))
      unifrog_text_copy(style->static_image, sizeof(style->static_image),
         path);
}

static void theme_override_static_image(struct unifrog_frontend_lvgl_style *style,
   const char *dir, const char *name)
{
   char path[FRONTEND_MAX_PATH];

   if (!style || !dir || !name)
      return;
   if (frontend_path_join(path, sizeof(path), dir, name) == 0 && frontend_file_exists(path))
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
   if (frontend_path_join(path, sizeof(path), dir, name) == 0 && frontend_file_exists(path))
      unifrog_text_copy(style->launch_wallpaper[index],
         sizeof(style->launch_wallpaper[index]), path);
}

static void theme_try_launch_wallpaper_all(struct unifrog_frontend_lvgl_style *style,
   const char *dir, const char *name)
{
   char path[FRONTEND_MAX_PATH];

   if (!style || !dir || !name)
      return;
   if (frontend_path_join(path, sizeof(path), dir, name) != 0 || !frontend_file_exists(path))
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
   if (frontend_path_join(path, sizeof(path), dir, name) == 0 && frontend_file_exists(path))
      unifrog_text_copy(style->launch_icon[index],
         sizeof(style->launch_icon[index]), path);
}

static const char *item_glyph_key(const struct frontend_item *item)
{
   static const struct {
      const char *action;
      const char *glyph;
   } action_glyphs[] = {
      { "launch_settings", "general" },
      { "theme_alternate", "alternate" },
      { "launch_splash", "splash" },
      { "boxart_hide", "boxarthide" },
      { "title_root", "titleincluderootdrive" },
      { "empty_folder", "folderempty" },
      { "folder_counts", "folderitemcount" },
      { "counter_folder", "counterfolder" },
      { "counter_file", "counterfile" },
      { "content_collect", "collection" },
      { "content_history", "history" },
      { "mixed_content", "mixedcontent" },
      { "storage_fast_probe", "tester" },
      { "package_check", "tester" },
      { "cores", "core" },
      { "storage_recover", "storage" },
      { "storage_mode", "storage" },
      { "storage_profile", "storage" },
      { "storage_auto_normal", "storage" },
      { "storage_auto_fallback", "storage" },
      { "flush_log", "log" },
      { "explore_unifrog", "init" },
      { "explore_data", "folder" },
      { "explore_bios", "bios" },
      { "explore_saves", "save" },
      { "theme_select", "theme" },
      { "language_select", "language" },
      { "firmware", "reboot" },
   };

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
   if (item->kind == FRONTEND_ITEM_READER)
      return "media";
   if (item->kind == FRONTEND_ITEM_FIRMWARE)
      return "archive";
   if (item->kind == FRONTEND_ITEM_THEME_ARCHIVE)
      return "theme";
   if (item->kind == FRONTEND_ITEM_CORE_MODULE)
      return "core";
   for (unsigned i = 0; i < ARRAY_SIZE(action_glyphs); i++) {
      if (strcmp(item->path, action_glyphs[i].action) == 0)
         return action_glyphs[i].glyph;
   }
   if (strcmp(item->path, "back") == 0 ||
       strncmp(item->path, "back_", 5) == 0)
      return "back";
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

static int theme_glyph_path(struct frontend_state *fe, const char *module,
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
       frontend_path_join(dir, sizeof(dir), FRONTEND_THEME_ROOT, fe->theme_name) != 0)
      return -1;
   sanitize_glyph_key(clean, sizeof(clean), key);
   if (!clean[0])
      return -1;
   for (unsigned i = 0; i < ARRAY_SIZE(prefixes); i++) {
      snprintf(rel, sizeof(rel), "%s/%s/%s.png", prefixes[i], module, clean);
      if (frontend_path_join(out, out_size, dir, rel) == 0 && frontend_file_exists(out))
         return 0;
   }
   for (unsigned i = 0; i < ARRAY_SIZE(prefixes); i++) {
      snprintf(rel, sizeof(rel), "%s/%s.png", prefixes[i], clean);
      if (frontend_path_join(out, out_size, dir, rel) == 0 && frontend_file_exists(out))
         return 0;
   }
   out[0] = '\0';
   return -1;
}

unsigned visible_rows_for_style(
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

void visible_item_range(unsigned count, unsigned selected,
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

void fill_visible_item_glyphs(struct frontend_state *fe,
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
   "scheme",
};

static void add_theme_scheme(struct frontend_state *fe, const char *name)
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

static void scan_theme_schemes(struct frontend_state *fe, const char *dir)
{
   uint32_t start_ms = unifrog_perf_time_ms();

   if (!fe || !dir)
      return;
   fe->scheme_count = 0;
   for (unsigned i = 0; i < ARRAY_SIZE(frontend_scheme_prefixes); i++) {
      char path[FRONTEND_MAX_PATH];
      DIR *scan;
      struct dirent *entry;

      if (frontend_path_join(path, sizeof(path), dir, frontend_scheme_prefixes[i]) != 0)
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

static int frontend_has_scheme(const struct frontend_state *fe,
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
      if (frontend_path_join(path, sizeof(path), dir, rel) == 0 &&
          load_muos_scheme_file(path, theme, style) == 0)
         return 0;
   }
   return -1;
}

static int load_muos_alternate_scheme(const char *dir,
   struct unifrog_ui_theme *theme, struct unifrog_frontend_lvgl_style *style)
{
   char alternate_dir[FRONTEND_MAX_PATH];
   char active_path[FRONTEND_MAX_PATH];
   char active[64] = "";
   char path[FRONTEND_MAX_PATH];
   FILE *file;

   if (!dir || !theme || !style ||
       frontend_path_join(alternate_dir, sizeof(alternate_dir), dir, "alternate") != 0)
      return -1;
   if (frontend_path_join(active_path, sizeof(active_path), dir, "active.txt") == 0) {
      file = fopen(active_path, "rb");
      if (file) {
         if (fgets(active, sizeof(active), file))
            frontend_strip_eol(active);
         fclose(file);
      }
   }
   if (active[0]) {
      char rel[96];

      snprintf(rel, sizeof(rel), "%s.ini", active);
      if (frontend_path_join(path, sizeof(path), alternate_dir, rel) == 0 &&
          load_muos_scheme_file(path, theme, style) == 0) {
         unifrog_log("frontend theme alternate loaded active=%s path=%s\n",
            active, path);
         return 0;
      }
   }
   {
      DIR *scan = opendir(alternate_dir);
      struct dirent *entry;

      if (!scan)
         return -1;
      while ((entry = readdir(scan)) != NULL) {
         if (entry->d_name[0] == '.' ||
             !unifrog_text_ends_with_ci(entry->d_name, ".ini"))
            continue;
         if (frontend_path_join(path, sizeof(path), alternate_dir,
             entry->d_name) == 0 &&
             load_muos_scheme_file(path, theme, style) == 0) {
            closedir(scan);
            unifrog_log("frontend theme alternate loaded path=%s\n", path);
            return 0;
         }
      }
      closedir(scan);
   }
   return -1;
}

static int apply_muos_alternate(struct frontend_state *fe,
   struct unifrog_ui_theme *theme, struct unifrog_frontend_lvgl_style *style)
{
   char dir[FRONTEND_MAX_PATH];

   if (!fe || !fe->theme_alternate || !theme || !style)
      return 0;
   if (strcmp(fe->theme_name, "muos") != 0 &&
       frontend_path_join(dir, sizeof(dir), FRONTEND_THEME_ROOT, fe->theme_name) == 0 &&
       load_muos_alternate_scheme(dir, theme, style) == 0)
      return 1;
   {
      uint16_t tmp = theme->focus;

      theme->focus = theme->accent;
      theme->accent = tmp;
      alternate_style(style);
   }
   return 0;
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
       frontend_path_join(dir, sizeof(dir), FRONTEND_THEME_ROOT, name) != 0)
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
       frontend_path_join(path, sizeof(path), dir, "theme.ini") == 0 &&
       load_muos_scheme_file(path, theme, style) == 0)
      loaded++;
   theme_apply_wallpapers(style, dir, module, module == NULL || !module[0]);
   return loaded > 0 ? 0 : -1;
}

static int load_frontend_module_from_base(struct frontend_state *fe,
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
       frontend_path_join(dir, sizeof(dir), FRONTEND_THEME_ROOT, name) != 0)
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

void alternate_style(struct unifrog_frontend_lvgl_style *style)
{
   uint16_t style_tmp;

   if (!style)
      return;
   style_tmp = style->list_focus_background;
   style->list_focus_background = style->list_focus_indicator;
   style->list_focus_indicator = style_tmp;
}

void apply_frontend_style(struct frontend_state *fe, int id,
   const struct unifrog_frontend_lvgl_style *style)
{
   if (!fe || !style)
      return;
   if (fe->applied_style_id == id)
      return;
   unifrog_frontend_lvgl_set_style(style);
   fe->applied_style_id = id;
}

const struct unifrog_frontend_lvgl_style *frontend_screen_style(
   struct frontend_state *fe, enum unifrog_frontend_lvgl_screen screen)
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
      apply_muos_alternate(fe, &style_theme, &fe->screen_style[screen]);
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

const struct unifrog_frontend_lvgl_style *frontend_view_style(
   struct frontend_state *fe, enum frontend_view view)
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

   if (!dir || !rel || frontend_path_join(path, sizeof(path), dir, rel) != 0 ||
       !frontend_file_exists(path))
      return -1;
   ret = unifrog_gfx_load_font5x7_file(path);
   unifrog_log("frontend theme font load path=%s ret=%d\n", path, ret);
   return ret > 0 ? 0 : -1;
}

struct theme_font_load_context {
   struct frontend_state *fe;
   char section[64];
};

static int theme_font_config_entry(void *userdata, const char *section,
   const char *key, const char *value, unsigned line_number)
{
   struct theme_font_load_context *context = userdata;

   (void)line_number;
   if (strcmp(section, context->section) == 0 && strcmp(key, "font") == 0)
      unifrog_text_copy(context->fe->theme_font,
         sizeof(context->fe->theme_font), value);
   return 0;
}

void load_theme_font_preference(struct frontend_state *fe)
{
   struct theme_font_load_context context;
   unsigned errors = 0;

   if (!fe)
      return;
   unifrog_text_copy(fe->theme_font, sizeof(fe->theme_font), "auto");
   memset(&context, 0, sizeof(context));
   context.fe = fe;
   snprintf(context.section, sizeof(context.section), "theme.%s",
      active_theme_label(fe));
   (void)unifrog_config_read(UNIFROG_CONFIG_PATH, theme_font_config_entry,
      &context, &errors);
}

static int try_load_preferred_font(struct frontend_state *fe,
   const char *theme_dir)
{
   char path[FRONTEND_MAX_PATH];

   if (!fe || !theme_dir || !fe->theme_font[0] ||
       strcmp(fe->theme_font, "auto") == 0)
      return -1;
   if (strncmp(fe->theme_font, "theme:", 6) == 0) {
      if (frontend_path_join(path, sizeof(path), theme_dir, fe->theme_font + 6) != 0)
         return -1;
   } else if (strncmp(fe->theme_font, "user:", 5) == 0) {
      if (frontend_path_join(path, sizeof(path), UNIFROG_FONT_ROOT,
          fe->theme_font + 5) != 0)
         return -1;
   } else {
      return -1;
   }
   if (!frontend_file_exists(path))
      return -1;
   {
      int ret = unifrog_gfx_load_font5x7_file(path);

      unifrog_log("frontend preferred font load path=%s ret=%d\n", path, ret);
      return ret > 0 ? 0 : -1;
   }
}

static void load_theme_font(struct frontend_state *fe)
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
       frontend_path_join(dir, sizeof(dir), FRONTEND_THEME_ROOT, fe->theme_name) != 0)
      return;
   if (try_load_preferred_font(fe, dir) == 0)
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
      }
   }
   if (strcmp(language, "english") != 0) {
      char fallback[FRONTEND_MAX_PATH];

      if (frontend_path_join(fallback, sizeof(fallback), UNIFROG_FONT_ROOT,
          "unifrog-ui.ttf") == 0 && frontend_file_exists(fallback) &&
          unifrog_gfx_load_font5x7_file(fallback) > 0) {
         unifrog_log("frontend language fallback font path=%s\n", fallback);
         return;
      }
   }
   for (unsigned r = 0; r < ARRAY_SIZE(roots); r++) {
      for (unsigned e = 0; e < ARRAY_SIZE(exts); e++) {
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

void load_theme(struct frontend_state *fe)
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
   load_theme_font_preference(fe);
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
      if (frontend_path_join(path, sizeof(path), FRONTEND_THEME_ROOT, fe->theme_name) == 0)
         scan_theme_schemes(fe, path);
   } else if (strcmp(fe->theme_name, "muos") != 0 &&
              frontend_path_join_ini(path, sizeof(path), FRONTEND_THEME_ROOT,
              fe->theme_name) == 0) {
      file = fopen(path, "rb");
      if (file) {
         while (fgets(line, sizeof(line), file)) {
            const char *value;
            uint16_t color;

            frontend_strip_eol(line);
            if (!line[0] || line[0] == '#')
               continue;
            if ((value = frontend_read_key_value(line, "background")) != NULL &&
                frontend_parse_rgb565_hex(value, &color) == 0) {
               theme.background = color;
               style.background = color;
            }
            else if ((value = frontend_read_key_value(line, "panel")) != NULL &&
                     frontend_parse_rgb565_hex(value, &color) == 0) {
               theme.panel = color;
               style.header_background = color;
               style.footer_background = color;
               style.list_background = color;
            }
            else if (((value = frontend_read_key_value(line, "focus")) != NULL ||
                     (value = frontend_read_key_value(line, "row")) != NULL) &&
                     frontend_parse_rgb565_hex(value, &color) == 0) {
               theme.focus = color;
               style.list_focus_background = color;
            }
            else if (((value = frontend_read_key_value(line, "text")) != NULL ||
                     (value = frontend_read_key_value(line, "text_primary")) != NULL ||
                     (value = frontend_read_key_value(line, "text_title")) != NULL) &&
                     frontend_parse_rgb565_hex(value, &color) == 0) {
               theme.text = color;
               style.header_text = color;
               style.list_text = color;
            }
            else if (((value = frontend_read_key_value(line, "muted")) != NULL ||
                     (value = frontend_read_key_value(line, "text_muted")) != NULL ||
                     (value = frontend_read_key_value(line, "text_footer")) != NULL) &&
                     frontend_parse_rgb565_hex(value, &color) == 0) {
               theme.muted = color;
               style.footer_text = color;
               style.list_indicator = color;
            }
            else if ((value = frontend_read_key_value(line, "accent")) != NULL &&
                     frontend_parse_rgb565_hex(value, &color) == 0) {
               theme.accent = color;
               style.list_focus_text = color;
               style.list_focus_indicator = color;
            }
            else if (((value = frontend_read_key_value(line, "danger")) != NULL ||
                     (value = frontend_read_key_value(line, "text_warning")) != NULL) &&
                     frontend_parse_rgb565_hex(value, &color) == 0)
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
      apply_muos_alternate(fe, &theme, &style);
   }
   fe->active_theme = theme;
   fe->base_theme = base_theme;
   fe->active_style = style;
   fe->base_style = base_style;
   fe->dir_theme_loaded = dir_theme_loaded;
   if (!dir_theme_loaded)
      fe->list_style = style;
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

      snprintf(cache_key, sizeof(cache_key), "%s", fe->theme_name);
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
