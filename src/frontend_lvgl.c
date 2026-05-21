#include <unifrog/frontend_lvgl.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <unifrog/gfx.h>
#include <unifrog/log.h>
#include <unifrog/png.h>
#include <unifrog/text.h>
#include <unifrog/ui.h>

#define FRONTEND_LVGL_W 320
#define FRONTEND_LVGL_H 240
#define FRONTEND_LVGL_LAUNCH_COUNT 8u
#define FRONTEND_LVGL_LIST_ROWS 8u
#define FRONTEND_LVGL_CACHE_INITIAL 16u

struct frontend_lvgl_cached_image {
   struct unifrog_png_image image;
   char path[256];
};

struct frontend_lvgl_runtime {
   struct unifrog_frontend_lvgl_style style;
   int has_style;
   struct frontend_lvgl_cached_image *cache;
   unsigned cache_count;
   unsigned cache_cap;
   char active_wallpaper[256];
   char active_static_image[256];
   char failed_image[256];
   int screenshot_request;
   unsigned frame_seq;
};

static struct frontend_lvgl_runtime frontend_lvgl;

static const char *const launch_labels[FRONTEND_LVGL_LAUNCH_COUNT] = {
   "Explore",
   "Collection",
   "History",
   "Apps",
   "Info",
   "Config",
   "Reboot",
   "Shutdown",
};

static uint8_t theme_alpha(uint8_t alpha)
{
   return alpha;
}

static unsigned rgb565_luma(uint16_t c)
{
   unsigned r = (unsigned)((c >> 11) & 0x1fu);
   unsigned g = (unsigned)((c >> 5) & 0x3fu);
   unsigned b = (unsigned)(c & 0x1fu);

   r = (r * 255u + 15u) / 31u;
   g = (g * 255u + 31u) / 63u;
   b = (b * 255u + 15u) / 31u;
   return (r * 299u + g * 587u + b * 114u) / 1000u;
}

static uint16_t contrast_text(uint16_t fg, uint16_t bg, uint8_t bg_alpha)
{
   unsigned fl;
   unsigned bl;
   unsigned diff;

   if (bg_alpha < 64u)
      return fg;
   fl = rgb565_luma(fg);
   bl = rgb565_luma(bg);
   diff = fl > bl ? fl - bl : bl - fl;
   if (diff >= 72u)
      return fg;
   return bl < 128u ? UNIFROG_RGB565(248, 248, 240) :
      UNIFROG_RGB565(12, 14, 18);
}

static uint16_t blend_rgb565(uint16_t dst, uint16_t src, uint8_t alpha)
{
   unsigned inv = 255u - alpha;
   unsigned sr = (src >> 11) & 0x1fu;
   unsigned sg = (src >> 5) & 0x3fu;
   unsigned sb = src & 0x1fu;
   unsigned dr = (dst >> 11) & 0x1fu;
   unsigned dg = (dst >> 5) & 0x3fu;
   unsigned db = dst & 0x1fu;

   if (alpha == 0)
      return dst;
   if (alpha == 255)
      return src;
   return (uint16_t)((((sr * alpha + dr * inv + 127u) / 255u) << 11) |
      (((sg * alpha + dg * inv + 127u) / 255u) << 5) |
      ((sb * alpha + db * inv + 127u) / 255u));
}

static void fill_rect_alpha(const struct unifrog_surface *surface,
   int x, int y, int w, int h, uint16_t color, uint8_t alpha)
{
   if (!surface || !surface->pixels || w <= 0 || h <= 0 || alpha == 0)
      return;
   if (alpha == 255) {
      unifrog_gfx_fill_rect(surface, x, y, w, h, color);
      return;
   }
   for (int dy = 0; dy < h; dy++) {
      int py = y + dy;

      if (py < 0 || py >= (int)surface->height)
         continue;
      for (int dx = 0; dx < w; dx++) {
         int px = x + dx;
         uint16_t *dst;

         if (px < 0 || px >= (int)surface->width)
            continue;
         dst = &surface->pixels[(size_t)py * surface->stride + px];
         *dst = blend_rgb565(*dst, color, alpha);
      }
   }
}

static struct unifrog_png_image *cached_image(const char *path)
{
   if (!path || !path[0])
      return NULL;
   for (unsigned i = 0; i < frontend_lvgl.cache_count; i++) {
      if (strcmp(frontend_lvgl.cache[i].path, path) == 0)
         return &frontend_lvgl.cache[i].image;
   }
   return NULL;
}

static struct unifrog_png_image *load_cached_image(const char *path)
{
   struct frontend_lvgl_cached_image *next;
   struct unifrog_png_image *found;

   if (!path || !path[0])
      return NULL;
   if (strcmp(frontend_lvgl.failed_image, path) == 0)
      return NULL;
   found = cached_image(path);
   if (found)
      return found;
   if (frontend_lvgl.cache_count == frontend_lvgl.cache_cap) {
      unsigned new_cap = frontend_lvgl.cache_cap ?
         frontend_lvgl.cache_cap * 2u : FRONTEND_LVGL_CACHE_INITIAL;
      struct frontend_lvgl_cached_image *new_cache =
         realloc(frontend_lvgl.cache, new_cap * sizeof(*frontend_lvgl.cache));

      if (!new_cache) {
         unifrog_log("frontend lvgl cache grow failed path=%s count=%u cap=%u\n",
            path, frontend_lvgl.cache_count, frontend_lvgl.cache_cap);
         return NULL;
      }
      memset(new_cache + frontend_lvgl.cache_cap, 0,
         (new_cap - frontend_lvgl.cache_cap) * sizeof(*new_cache));
      frontend_lvgl.cache = new_cache;
      frontend_lvgl.cache_cap = new_cap;
   }
   next = &frontend_lvgl.cache[frontend_lvgl.cache_count];
   memset(next, 0, sizeof(*next));
   if (unifrog_png_load_file(path, &next->image) != 0) {
      unifrog_text_copy(frontend_lvgl.failed_image,
         sizeof(frontend_lvgl.failed_image), path);
      unifrog_log("frontend lvgl image load failed path=%s\n", path);
      return NULL;
   }
   unifrog_text_copy(next->path, sizeof(next->path), path);
   frontend_lvgl.cache_count++;
   unifrog_log("frontend lvgl image cached slot=%u path=%s size=%ux%u alpha=%u opaque=%d\n",
      frontend_lvgl.cache_count - 1u, path, next->image.width,
      next->image.height, unifrog_png_alpha_coverage(&next->image),
      unifrog_png_is_opaque(&next->image));
   return &next->image;
}

static void draw_image_path(const struct unifrog_surface *surface,
   const char *path, int x, int y, int w, int h)
{
   struct unifrog_png_image *image = load_cached_image(path);

   if (!image)
      return;
   unifrog_png_draw(surface, image, x, y, w, h);
}

static const struct unifrog_frontend_lvgl_style *active_style(
   const struct unifrog_ui_theme *theme)
{
   static struct unifrog_frontend_lvgl_style fallback;

   if (frontend_lvgl.has_style)
      return &frontend_lvgl.style;
   unifrog_frontend_lvgl_style_default(&fallback, theme);
   return &fallback;
}

static void draw_shell(struct unifrog_ui *ui,
   const struct unifrog_frontend_lvgl_style *style, const char *title,
   const char *detail, const char *status)
{
   struct unifrog_surface surface = unifrog_ui_surface(ui);
   int header_h = style->header_height > 0 ? style->header_height : 28;
   int footer_h = style->footer_height > 0 ? style->footer_height : 22;
   int footer_y = FRONTEND_LVGL_H - footer_h;
   uint8_t header_alpha = theme_alpha(style->header_alpha);
   uint8_t footer_alpha = theme_alpha(style->footer_alpha);

   fill_rect_alpha(&surface, 0, 0, FRONTEND_LVGL_W, header_h,
      style->header_background, header_alpha);
   fill_rect_alpha(&surface, 0, footer_y, FRONTEND_LVGL_W, footer_h,
      style->footer_background, footer_alpha);
   if (style->header_text_alpha)
      unifrog_ui_text_clipped(ui, 10, 9, 30, title ? title : "UniFrog",
         contrast_text(style->header_text, style->header_background,
            header_alpha), 1);
   if (style->header_text_alpha && detail && detail[0])
      unifrog_ui_text_clipped(ui, 214, 9, 16, detail,
         contrast_text(style->header_text, style->header_background,
            header_alpha), 1);
   if (style->footer_text_alpha)
      unifrog_ui_text_clipped(ui, 10, footer_y + 7, 30,
         status ? status : "", contrast_text(style->footer_text,
            style->footer_background, footer_alpha), 1);
}

static void begin_frame(struct unifrog_ui *ui,
   const struct unifrog_frontend_lvgl_style *style)
{
   struct unifrog_surface surface;

   unifrog_ui_begin(ui, style->background);
   surface = unifrog_ui_surface(ui);
   if (style->wallpaper[0])
      draw_image_path(&surface, style->wallpaper, 0, 0,
         FRONTEND_LVGL_W, FRONTEND_LVGL_H);
   if (style->static_image[0])
      draw_image_path(&surface, style->static_image, 0, 0,
         FRONTEND_LVGL_W, FRONTEND_LVGL_H);
}

static void draw_row(struct unifrog_ui *ui,
   const struct unifrog_frontend_lvgl_style *style, unsigned row,
   const char *label, const char *value, const char *glyph, int focused)
{
   struct unifrog_surface surface = unifrog_ui_surface(ui);
   int x = style->list_x > 0 ? style->list_x : 8;
   int y = (style->header_height > 0 ? style->header_height : 28) +
      (style->list_y >= 0 ? style->list_y : 8) +
      (int)row * ((style->list_h > 0 ? style->list_h : 20) +
      (style->list_gap >= 0 ? style->list_gap : 3));
   int w = style->list_w > 0 ? style->list_w : 304;
   int h = style->list_h > 0 ? style->list_h : 20;
   uint16_t bg = focused ? style->list_focus_background :
      style->list_background;
   uint16_t fg = focused ? style->list_focus_text : style->list_text;
   uint16_t muted = focused ? style->list_focus_indicator :
      style->list_indicator;
   uint8_t bg_alpha = focused ? theme_alpha(style->list_focus_alpha) :
      theme_alpha(style->list_alpha);
   uint8_t text_alpha = focused ? theme_alpha(style->list_focus_text_alpha) :
      theme_alpha(style->list_text_alpha);
   uint8_t indicator_alpha = focused ?
      theme_alpha(style->list_focus_indicator_alpha) :
      theme_alpha(style->list_indicator_alpha);
   int glyph_w = style->list_glyph_w > 0 ? style->list_glyph_w : 16;
   int glyph_h = style->list_glyph_h > 0 ? style->list_glyph_h : 16;
   int glyph_x = style->list_glyph_x >= 0 ? style->list_glyph_x : 5;
   int label_x = style->label_x > 0 ? style->label_x : 28;
   int value_w = style->value_w > 0 ? style->value_w : 90;

   fill_rect_alpha(&surface, x, y, w, h, bg, bg_alpha);
   if (glyph && glyph[0] &&
       (focused ? style->list_focus_glyph_alpha : style->list_glyph_alpha))
      draw_image_path(&surface, glyph, x + glyph_x, y + (h - glyph_h) / 2,
         glyph_w, glyph_h);
   fg = contrast_text(fg, bg, bg_alpha);
   muted = contrast_text(muted, bg, bg_alpha);
   if (text_alpha)
      unifrog_ui_text_clipped(ui, x + label_x, y + (h - 7) / 2,
         ((w - label_x - value_w - 8) / 6), label, fg, 1);
   if (indicator_alpha && value && value[0])
      unifrog_ui_text_clipped(ui, x + w - value_w, y + (h - 7) / 2,
         value_w / 6, value, muted, 1);
}

static void draw_list_window(struct unifrog_ui *ui,
   const struct unifrog_frontend_lvgl_style *style, unsigned selected,
   const char *const *labels, const char *const *values,
   const char *const *glyphs, unsigned count)
{
   unsigned rows = FRONTEND_LVGL_LIST_ROWS;
   unsigned start = 0;
   unsigned visible;

   if (style->list_h > 0) {
      int header_h = style->header_height > 0 ? style->header_height : 28;
      int footer_h = style->footer_height > 0 ? style->footer_height : 22;
      int gap = style->list_gap >= 0 ? style->list_gap : 3;
      int area = FRONTEND_LVGL_H - header_h - footer_h -
         (style->list_y >= 0 ? style->list_y : 8) - 4;

      if (area > 0)
         rows = (unsigned)(area / (style->list_h + gap));
   }
   if (rows == 0)
      rows = 1;
   if (rows > FRONTEND_LVGL_LIST_ROWS)
      rows = FRONTEND_LVGL_LIST_ROWS;
   if (count > rows && selected >= rows / 2u)
      start = selected - rows / 2u;
   if (start + rows > count)
      start = count > rows ? count - rows : 0;
   visible = count - start;
   if (visible > rows)
      visible = rows;
   for (unsigned i = 0; i < visible; i++) {
      unsigned idx = start + i;

      draw_row(ui, style, i, labels && labels[idx] ? labels[idx] : "",
         values && values[idx] ? values[idx] : "",
         glyphs && glyphs[idx] ? glyphs[idx] : "", idx == selected);
   }
}

static int launcher_uses_list(const struct unifrog_frontend_lvgl_style *style)
{
   if (!style)
      return 0;
   if (style->navigation_type != 0 && style->navigation_type != 2)
      return 1;
   if (style->launch_cols <= 1)
      return 1;
   for (unsigned i = 0; i < FRONTEND_LVGL_LAUNCH_COUNT; i++) {
      if (style->launch_icon[i][0])
         return 0;
      if (style->launch_wallpaper[i][0])
         return 0;
   }
   return style->navigation_type != 0;
}

static void draw_launcher_grid(struct unifrog_ui *ui,
   const struct unifrog_frontend_lvgl_style *style, unsigned selected)
{
   struct unifrog_surface surface = unifrog_ui_surface(ui);
   int cols = style->launch_cols > 0 ? style->launch_cols : 4;
   int tile_w = style->launch_tile_w > 0 ? style->launch_tile_w : 68;
   int tile_h = style->launch_tile_h > 0 ? style->launch_tile_h : 58;
   int gap_x = style->launch_gap_x >= 0 ? style->launch_gap_x : 8;
   int gap_y = style->launch_gap_y >= 0 ? style->launch_gap_y : 14;
   int start_x = style->launch_x >= 0 ? style->launch_x : 12;
   int start_y = style->launch_y >= 0 ? style->launch_y :
      (style->header_height > 0 ? style->header_height : 36) + 18;
   int icon_w = style->launch_icon_w > 0 ? style->launch_icon_w : tile_w - 8;
   int icon_h = style->launch_icon_h > 0 ? style->launch_icon_h : 26;

   if (cols < 1)
      cols = 1;
   for (unsigned i = 0; i < FRONTEND_LVGL_LAUNCH_COUNT; i++) {
      int col = (int)i % cols;
      int row = (int)i / cols;
      int x = start_x + col * (tile_w + gap_x);
      int y = start_y + row * (tile_h + gap_y);
      int focused = i == selected;
      uint16_t bg = focused ? style->list_focus_background :
         style->list_background;
      uint16_t fg = focused ? style->list_focus_text : style->list_text;
      uint8_t bg_alpha = focused ? theme_alpha(style->list_focus_alpha) :
         theme_alpha(style->list_alpha);
      uint8_t text_alpha = focused ?
         theme_alpha(style->list_focus_text_alpha) :
         theme_alpha(style->list_text_alpha);

      if (style->launch_wallpaper[i][0])
         draw_image_path(&surface, style->launch_wallpaper[i], x, y,
            tile_w, tile_h);
      else
         fill_rect_alpha(&surface, x, y, tile_w, tile_h, bg, bg_alpha);
      if (style->launch_icon[i][0])
         draw_image_path(&surface, style->launch_icon[i],
            x + (tile_w - icon_w) / 2, y + 4, icon_w, icon_h);
      else {
         char glyph[2] = { launch_labels[i][0], '\0' };

         fill_rect_alpha(&surface, x + 4, y + 4, tile_w - 8, icon_h,
            focused ? style->list_focus_indicator : style->list_indicator,
            220);
         unifrog_gfx_draw_text(&surface, x + tile_w / 2 - 3,
            y + 4 + icon_h / 2 - 4, glyph, style->background, 1);
      }
      if (text_alpha)
         unifrog_ui_text_clipped(ui, x + 5, y + tile_h - 18,
            (tile_w - 10) / 6, launch_labels[i],
            contrast_text(fg, bg, bg_alpha), 1);
   }
}

void unifrog_frontend_lvgl_style_default(
   struct unifrog_frontend_lvgl_style *style,
   const struct unifrog_ui_theme *theme)
{
   if (!style)
      return;
   if (!theme)
      theme = unifrog_ui_default_theme();
   memset(style, 0, sizeof(*style));
   style->background = theme->background;
   style->header_background = theme->panel;
   style->header_text = theme->text;
   style->footer_background = theme->panel;
   style->footer_text = theme->muted;
   style->list_background = theme->panel;
   style->list_text = theme->text;
   style->list_indicator = theme->muted;
   style->list_focus_background = theme->focus;
   style->list_focus_text = theme->text;
   style->list_focus_indicator = theme->accent;
   style->background_alpha = 255;
   style->header_alpha = 255;
   style->header_text_alpha = 255;
   style->footer_alpha = 255;
   style->footer_text_alpha = 255;
   style->list_alpha = 224;
   style->list_text_alpha = 255;
   style->list_indicator_alpha = 255;
   style->list_glyph_alpha = 255;
   style->list_focus_alpha = 255;
   style->list_focus_text_alpha = 255;
   style->list_focus_indicator_alpha = 255;
   style->list_focus_glyph_alpha = 255;
   style->header_height = 34;
   style->footer_height = 24;
   style->list_x = 8;
   style->list_y = 8;
   style->list_w = 304;
   style->list_h = 20;
   style->list_gap = 3;
   style->list_radius = 0;
   style->list_border_width = 0;
   style->list_glyph_w = 16;
   style->list_glyph_h = 16;
   style->list_glyph_x = 5;
   style->label_x = 28;
   style->label_w = 172;
   style->value_w = 92;
   style->launch_cols = 4;
   style->launch_tile_w = 68;
   style->launch_tile_h = 58;
   style->launch_gap_x = 8;
   style->launch_gap_y = 14;
   style->launch_x = 12;
   style->launch_y = 54;
   style->launch_icon_w = 60;
   style->launch_icon_h = 26;
}

void unifrog_frontend_lvgl_set_style(
   const struct unifrog_frontend_lvgl_style *style)
{
   if (!style) {
      frontend_lvgl.has_style = 0;
      return;
   }
   frontend_lvgl.style = *style;
   frontend_lvgl.has_style = 1;
}

void unifrog_frontend_lvgl_clear_resource_cache(void)
{
   for (unsigned i = 0; i < frontend_lvgl.cache_count; i++) {
      unifrog_png_free(&frontend_lvgl.cache[i].image);
      frontend_lvgl.cache[i].path[0] = '\0';
   }
   free(frontend_lvgl.cache);
   frontend_lvgl.cache = NULL;
   frontend_lvgl.cache_count = 0;
   frontend_lvgl.cache_cap = 0;
   frontend_lvgl.failed_image[0] = '\0';
   frontend_lvgl.active_wallpaper[0] = '\0';
   frontend_lvgl.active_static_image[0] = '\0';
}

void unifrog_frontend_lvgl_preload_style_images(
   const struct unifrog_frontend_lvgl_style *style)
{
   if (!style)
      return;
   (void)load_cached_image(style->wallpaper);
   (void)load_cached_image(style->static_image);
   for (unsigned i = 0; i < FRONTEND_LVGL_LAUNCH_COUNT; i++) {
      (void)load_cached_image(style->launch_wallpaper[i]);
      (void)load_cached_image(style->launch_icon[i]);
   }
}

void unifrog_frontend_lvgl_request_screenshot(void)
{
   frontend_lvgl.screenshot_request = 1;
}

int unifrog_frontend_lvgl_draw_launcher(struct unifrog_ui *ui,
   const struct unifrog_ui_theme *theme, unsigned selected,
   const char *detail, const char *status)
{
   const struct unifrog_frontend_lvgl_style *style = active_style(theme);
   const char *labels[FRONTEND_LVGL_LAUNCH_COUNT];
   const char *values[FRONTEND_LVGL_LAUNCH_COUNT];
   const char *glyphs[FRONTEND_LVGL_LAUNCH_COUNT];

   if (!ui)
      return -1;
   if (selected >= FRONTEND_LVGL_LAUNCH_COUNT)
      selected = 0;
   for (unsigned i = 0; i < FRONTEND_LVGL_LAUNCH_COUNT; i++) {
      labels[i] = launch_labels[i];
      values[i] = "";
      glyphs[i] = style->launch_icon[i];
   }
   begin_frame(ui, style);
   if (style->launch_wallpaper[selected][0]) {
      struct unifrog_surface surface = unifrog_ui_surface(ui);

      draw_image_path(&surface, style->launch_wallpaper[selected], 0, 0,
         FRONTEND_LVGL_W, FRONTEND_LVGL_H);
   }
   draw_shell(ui, style, "UniFrog", detail,
      status ? status : "A open  L/R page");
   if (launcher_uses_list(style))
      draw_list_window(ui, style, selected, labels, values, glyphs,
         FRONTEND_LVGL_LAUNCH_COUNT);
   else
      draw_launcher_grid(ui, style, selected);
   frontend_lvgl.frame_seq++;
   unifrog_ui_present(ui);
   return 0;
}

int unifrog_frontend_lvgl_draw_menu(struct unifrog_ui *ui,
   const struct unifrog_ui_theme *theme, enum unifrog_frontend_lvgl_screen screen,
   const char *title, unsigned selected, const char *detail,
   const char *status, const char *const *labels,
   const char *const *values, const char *const *glyphs, unsigned count)
{
   const struct unifrog_frontend_lvgl_style *style = active_style(theme);

   (void)screen;
   if (!ui)
      return -1;
   if (count && selected >= count)
      selected = count - 1u;
   begin_frame(ui, style);
   draw_shell(ui, style, title ? title : "UniFrog", detail,
      status ? status : "A select  B back");
   draw_list_window(ui, style, selected, labels, values, glyphs, count);
   frontend_lvgl.frame_seq++;
   unifrog_ui_present(ui);
   return 0;
}

int unifrog_frontend_lvgl_draw_list(struct unifrog_ui *ui,
   const struct unifrog_ui_theme *theme, const char *title, unsigned selected,
   const char *detail, const char *status, const char *const *labels,
   const char *const *values, const char *const *glyphs, unsigned count)
{
   return unifrog_frontend_lvgl_draw_menu(ui, theme,
      UNIFROG_FRONTEND_LVGL_CONFIG, title, selected, detail, status,
      labels, values, glyphs, count);
}
