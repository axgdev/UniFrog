#include "frontend_internal.h"

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

void frontend_invalidate_draw(struct frontend_state *fe)
{
   if (fe)
      fe->last_draw_valid = 0;
}

void frontend_request_return_redraw(struct frontend_state *fe,
   const char *source)
{
   if (!fe)
      return;
   frontend_invalidate_draw(fe);
   fe->needs_draw = 1;
   unifrog_log("frontend return redraw source=%s view=%d selected=%u\n",
      source ? source : "", fe->view, fe->selected);
}

void frontend_set_status(struct frontend_state *fe, const char *fmt, ...)
{
   va_list ap;

   if (!fe)
      return;
   fmt = tr(fe, fmt ? fmt : "");
   va_start(ap, fmt);
   vsnprintf(fe->status, sizeof(fe->status), fmt, ap);
   va_end(ap);
   fe->needs_draw = 1;
}

static void refresh_selected_artwork(struct frontend_state *fe,
   const struct frontend_item *item)
{
   FILE *file;
   size_t read;

   if (!fe || !item || !item->path[0])
      return;
   if (strcmp(fe->artwork_cache_item, item->path) == 0)
      return;
   unifrog_text_copy(fe->artwork_cache_item,
      sizeof(fe->artwork_cache_item), item->path);
   memset(&fe->artwork_cache_paths, 0, sizeof(fe->artwork_cache_paths));
   fe->artwork_cache_text[0] = '\0';
   (void)unifrog_artwork_resolve(item->path, NULL,
      fe->artwork_box_templates, fe->artwork_preview_templates,
      fe->artwork_text_templates, &fe->artwork_cache_paths);
   if (!fe->artwork_cache_paths.text[0])
      return;
   file = fopen(fe->artwork_cache_paths.text, "rb");
   if (!file)
      return;
   read = fread(fe->artwork_cache_text, 1,
      sizeof(fe->artwork_cache_text) - 1u, file);
   fclose(file);
   fe->artwork_cache_text[read] = '\0';
   for (size_t i = 0; i < read; i++) {
      unsigned char c = (unsigned char)fe->artwork_cache_text[i];

      if (c == '\r' || c == '\n' || c == '\t')
         fe->artwork_cache_text[i] = ' ';
      else if (c < 32u)
         fe->artwork_cache_text[i] = '?';
   }
}

static int selected_theme_preview(const struct frontend_state *fe,
   char *path, size_t size)
{
   char dir[FRONTEND_MAX_PATH];
   static const char *const candidates[] = {
      "320x240/preview.png", "640x480/preview.png", "preview.png",
   };
   const struct frontend_item *item;

   if (!fe || fe->view != FRONTEND_VIEW_THEME ||
       fe->selected >= fe->item_count || !path || size == 0)
      return -1;
   item = &fe->items[fe->selected];
   if (strcmp(item->path, "theme_select") != 0 || !item->core[0] ||
       strcmp(item->core, "muos") == 0 ||
       frontend_path_join(dir, sizeof(dir), FRONTEND_THEME_ROOT, item->core) != 0)
      return -1;
   for (unsigned i = 0; i < ARRAY_SIZE(candidates); i++) {
      if (frontend_path_join(path, size, dir, candidates[i]) == 0 && frontend_file_exists(path))
         return 0;
   }
   path[0] = '\0';
   return -1;
}

void frontend_draw(struct frontend_state *fe)
{
   char detail[48];
   uint32_t now = unifrog_perf_time_ms();
   unsigned end;

   fe->needs_draw = 0;
   if (fe->shutdown_safe) {
      unifrog_ui_message(&fe->ui, fe->theme, "Safe to Power Off",
         "All SD writes are complete",
         "Turn off the power switch when ready");
      return;
   }
   if (fe->battery_notice_until_ms &&
       (int32_t)(fe->battery_notice_until_ms - now) > 0) {
      unifrog_ui_message(&fe->ui, fe->theme, "Low Battery",
         "SD stays in high-performance mode",
         "Save your game and charge; any key closes");
      return;
   }
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
            fe->battery.percent);
      else
         snprintf(with_battery, sizeof(with_battery), "%u%%",
            fe->battery.percent);
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
       fe->view == FRONTEND_VIEW_PACKAGE_CHECK ||
       fe->view == FRONTEND_VIEW_CLOCK) {
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
      const char *box = NULL;
      const char *preview = NULL;
      const char *description = NULL;
      char theme_preview[FRONTEND_MAX_PATH];
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
      theme_preview[0] = '\0';
      if (selected_theme_preview(fe, theme_preview,
          sizeof(theme_preview)) == 0) {
         preview = theme_preview;
      } else if (fe->selected < fe->item_count &&
          fe->items[fe->selected].kind == FRONTEND_ITEM_GAME) {
         refresh_selected_artwork(fe, &fe->items[fe->selected]);
         box = fe->boxart_hidden ? NULL : fe->artwork_cache_paths.box;
         preview = fe->artwork_cache_paths.preview;
         description = fe->artwork_cache_text;
      }
      if ((box && box[0]) || (preview && preview[0]) ||
          (description && description[0])) {
         if (unifrog_frontend_lvgl_draw_list_preview(&fe->ui, fe->theme,
               fe->title, fe->selected, detail,
               fe->status[0] ? fe->status : "A select  X open with  Y favorite",
               labels, values, fe->item_glyph, fe->item_count, box, preview,
               description) == 0)
            return;
      }
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
      if (fe->items[i].kind == FRONTEND_ITEM_GAME &&
          frontend_favorite_contains(fe->items[i].path)) {
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
