#include "frontend_internal.h"

/* Shared progress presentation for operations that temporarily own the UI. */
void frontend_loading_show(struct frontend_state *fe, const char *title,
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
   title = tr(fe, title ? title : "LOADING");
   stage = tr(fe, stage ? stage : "");
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
   if (title && strcmp(title, "Now Playing") == 0) {
      unifrog_gfx_draw_text(&surface, 18, (int)surface.height - 44,
         "B stop    Left/Right seek", UNIFROG_RGB565(236, 241, 246), 1);
      unifrog_ui_present(&fe->ui);
      return;
   }

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

void frontend_loading_handoff_black(struct frontend_state *fe,
   const char *tag)
{
   if (!fe)
      return;

   frontend_invalidate_draw(fe);
   if (fe->ui.fb.pixels) {
      unifrog_ui_begin(&fe->ui, UNIFROG_RGB565(0, 0, 0));
      unifrog_ui_present(&fe->ui);
      unifrog_ui_close(&fe->ui);
      unifrog_log("frontend loading handoff tag=%s closed=1\n",
         tag && tag[0] ? tag : "");
   } else {
      unifrog_ui_close(&fe->ui);
      unifrog_log("frontend loading handoff tag=%s closed=0\n",
         tag && tag[0] ? tag : "");
   }
}

void frontend_install_progress_update(void *userdata, const char *stage,
   unsigned done, unsigned total)
{
   struct frontend_install_progress *progress = userdata;
   struct frontend_state *fe;
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
