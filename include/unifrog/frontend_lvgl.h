#ifndef UNIFROG_FRONTEND_LVGL_H
#define UNIFROG_FRONTEND_LVGL_H

#include <stdint.h>

#include <unifrog/ui.h>

#ifdef __cplusplus
extern "C" {
#endif

enum unifrog_frontend_lvgl_screen {
   UNIFROG_FRONTEND_LVGL_LAUNCH = 0,
   UNIFROG_FRONTEND_LVGL_CONFIG,
   UNIFROG_FRONTEND_LVGL_CONNECT,
   UNIFROG_FRONTEND_LVGL_CUSTOM,
   UNIFROG_FRONTEND_LVGL_INFO,
   UNIFROG_FRONTEND_LVGL_POWER,
   UNIFROG_FRONTEND_LVGL_STORAGE,
   UNIFROG_FRONTEND_LVGL_SYSINFO,
   UNIFROG_FRONTEND_LVGL_VISUAL,
};

struct unifrog_frontend_lvgl_style {
   uint16_t background;
   uint16_t header_background;
   uint16_t header_text;
   uint16_t footer_background;
   uint16_t footer_text;
   uint16_t list_background;
   uint16_t list_text;
   uint16_t list_indicator;
   uint16_t list_focus_background;
   uint16_t list_focus_text;
   uint16_t list_focus_indicator;
   uint8_t background_alpha;
   uint8_t header_alpha;
   uint8_t header_text_alpha;
   uint8_t footer_alpha;
   uint8_t footer_text_alpha;
   uint8_t list_alpha;
   uint8_t list_text_alpha;
   uint8_t list_indicator_alpha;
   uint8_t list_glyph_alpha;
   uint8_t list_focus_alpha;
   uint8_t list_focus_text_alpha;
   uint8_t list_focus_indicator_alpha;
   uint8_t list_focus_glyph_alpha;
   int header_height;
   int footer_height;
   int list_x;
   int list_y;
   int list_w;
   int list_h;
   int list_gap;
   int list_radius;
   int list_border_width;
   int list_glyph_w;
   int list_glyph_h;
   int list_glyph_x;
   int label_x;
   int label_w;
   int value_w;
   int launch_cols;
   int launch_tile_w;
   int launch_tile_h;
   int launch_gap_x;
   int launch_gap_y;
   int launch_x;
   int launch_y;
   int launch_icon_w;
   int launch_icon_h;
   int navigation_type;
   int grid_enabled;
   int grid_column_count;
   int grid_row_count;
   uint8_t theme_chrome;
   char wallpaper[256];
   char static_image[256];
   char launch_wallpaper[8][256];
   char launch_icon[8][256];
};

void unifrog_frontend_lvgl_style_default(struct unifrog_frontend_lvgl_style *style,
   const struct unifrog_ui_theme *theme);
void unifrog_frontend_lvgl_set_label_translator(
   const char *(*translate)(const char *key));
void unifrog_frontend_lvgl_set_style(
   const struct unifrog_frontend_lvgl_style *style);
void unifrog_frontend_lvgl_clear_resource_cache(void);
void unifrog_frontend_lvgl_preload_style_images(
   const struct unifrog_frontend_lvgl_style *style);
void unifrog_frontend_lvgl_request_screenshot(void);
int unifrog_frontend_lvgl_draw_launcher(struct unifrog_ui *ui,
   const struct unifrog_ui_theme *theme, unsigned selected,
   const char *detail, const char *status);
int unifrog_frontend_lvgl_draw_menu(struct unifrog_ui *ui,
   const struct unifrog_ui_theme *theme, enum unifrog_frontend_lvgl_screen screen,
   const char *title, unsigned selected, const char *detail,
   const char *status, const char *const *labels,
   const char *const *values, const char *const *glyphs, unsigned count);
int unifrog_frontend_lvgl_draw_list(struct unifrog_ui *ui,
   const struct unifrog_ui_theme *theme, const char *title, unsigned selected,
   const char *detail, const char *status, const char *const *labels,
   const char *const *values, const char *const *glyphs, unsigned count);
int unifrog_frontend_lvgl_draw_list_preview(struct unifrog_ui *ui,
   const struct unifrog_ui_theme *theme, const char *title, unsigned selected,
   const char *detail, const char *status, const char *const *labels,
   const char *const *values, const char *const *glyphs, unsigned count,
   const char *box_path, const char *preview_path, const char *description);
int unifrog_frontend_lvgl_animation_active(void);

#ifdef __cplusplus
}
#endif

#endif
