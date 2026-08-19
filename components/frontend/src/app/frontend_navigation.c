#include "frontend_internal.h"

void frontend_nav_reset(struct frontend_state *fe)
{
   fe->nav.count = 0;
}

void frontend_nav_push(struct frontend_state *fe)
{
   if ((fe->view != FRONTEND_VIEW_EXPLORE &&
        fe->view != FRONTEND_VIEW_FIRMWARE &&
        fe->view != FRONTEND_VIEW_SCRIPTS) ||
       !fe->current_dir[0])
      return;
   if (fe->nav.count >= FRONTEND_NAV_MAX) {
      memmove(fe->nav.path[0], fe->nav.path[1],
         (FRONTEND_NAV_MAX - 1u) * sizeof(fe->nav.path[0]));
      memmove(fe->nav.selected, fe->nav.selected + 1,
         (FRONTEND_NAV_MAX - 1u) * sizeof(fe->nav.selected[0]));
      fe->nav.count = FRONTEND_NAV_MAX - 1u;
   }
   unifrog_text_copy(fe->nav.path[fe->nav.count],
      sizeof(fe->nav.path[0]), fe->current_dir);
   fe->nav.selected[fe->nav.count] = fe->selected;
   fe->nav.count++;
   unifrog_log("frontend nav push view=%d depth=%u path=%s selected=%u\n",
      fe->view, fe->nav.count, fe->current_dir, fe->selected);
}

int frontend_nav_pop(struct frontend_state *fe, char *path, size_t path_size,
   unsigned *selected)
{
   if (!fe || !path || path_size == 0 || !selected || fe->nav.count == 0)
      return 0;
   fe->nav.count--;
   unifrog_text_copy(path, path_size, fe->nav.path[fe->nav.count]);
   *selected = fe->nav.selected[fe->nav.count];
   unifrog_log("frontend nav back view=%d depth=%u path=%s selected=%u\n",
      fe->view, fe->nav.count, path, *selected);
   return 1;
}

void frontend_parent_view_push(struct frontend_state *fe)
{
   if (fe->nav.view_count >= FRONTEND_NAV_MAX) {
      memmove(fe->nav.view_stack, fe->nav.view_stack + 1,
         (FRONTEND_NAV_MAX - 1u) * sizeof(fe->nav.view_stack[0]));
      memmove(fe->nav.view_selected, fe->nav.view_selected + 1,
         (FRONTEND_NAV_MAX - 1u) * sizeof(fe->nav.view_selected[0]));
      memmove(fe->nav.view_scroll, fe->nav.view_scroll + 1,
         (FRONTEND_NAV_MAX - 1u) * sizeof(fe->nav.view_scroll[0]));
      fe->nav.view_count = FRONTEND_NAV_MAX - 1u;
   }
   fe->nav.view_stack[fe->nav.view_count] = fe->view;
   fe->nav.view_selected[fe->nav.view_count] = fe->selected;
   fe->nav.view_scroll[fe->nav.view_count] = fe->scroll;
   fe->nav.view_count++;
   fe->nav.parent_view = fe->view;
   fe->nav.has_parent_view = 1;
}

void frontend_parent_view_clear(struct frontend_state *fe)
{
   fe->nav.has_parent_view = 0;
   fe->nav.parent_view = FRONTEND_VIEW_LAUNCH;
   fe->nav.view_count = 0;
}
