#include "frontend_internal.h"

static int ascii_is_digit(char c)
{
   return c >= '0' && c <= '9';
}

static char ascii_lower(char c)
{
   return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c;
}

static void move_selection(struct frontend_state *fe, int delta)
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

void frontend_input_jump_selection_group(struct frontend_state *fe, int dir)
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
      frontend_set_status(fe, "jump %c", key);
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
   frontend_set_status(fe, "jump %u", fe->selected + 1u);
}

int frontend_input_handle_menu_navigation(struct frontend_state *fe,
   uint32_t now, int combo_handled)
{
   if (combo_handled)
      return 0;
   if (unifrog_ui_repeated(&fe->ui, UNIFROG_UI_UP, now, 420, 140)) {
      move_selection(fe, -1);
      return 1;
   }
   if (unifrog_ui_repeated(&fe->ui, UNIFROG_UI_DOWN, now, 420, 140)) {
      move_selection(fe, 1);
      return 1;
   }
   if (unifrog_ui_pressed(&fe->ui, UNIFROG_UI_L)) {
      for (unsigned i = 0; i < FRONTEND_ROWS; i++)
         move_selection(fe, -1);
      return 1;
   }
   if (unifrog_ui_pressed(&fe->ui, UNIFROG_UI_R)) {
      for (unsigned i = 0; i < FRONTEND_ROWS; i++)
         move_selection(fe, 1);
      return 1;
   }
   return 0;
}
