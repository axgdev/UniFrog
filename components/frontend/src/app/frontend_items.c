#include "frontend_internal.h"

/* Frontend menu item list management and ordering. */
static int frontend_sort_desc;

int is_back_item(const struct frontend_item *item)
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

void reset_items(struct frontend_state *fe, const char *title)
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

static int item_path_already_listed(const struct frontend_state *fe,
   enum frontend_item_kind kind, const char *path)
{
   if (!fe || !path || !path[0])
      return 0;
   for (unsigned i = 0; i < fe->item_count; i++) {
      const struct frontend_item *item = &fe->items[i];

      if (item->kind == kind && item->path[0] &&
          strcmp(item->path, path) == 0)
         return 1;
   }
   return 0;
}

struct frontend_item *add_item(struct frontend_state *fe, const char *name,
   const char *meta, enum frontend_item_kind kind, const char *path,
   const char *core)
{
   struct frontend_item *item;

   if (fe->item_count >= FRONTEND_MAX_ITEMS)
      return NULL;
   if (kind == FRONTEND_ITEM_DIR || kind == FRONTEND_ITEM_GAME ||
       kind == FRONTEND_ITEM_MEDIA || kind == FRONTEND_ITEM_READER ||
       kind == FRONTEND_ITEM_SCRIPT ||
       kind == FRONTEND_ITEM_FIRMWARE) {
      if (item_path_already_listed(fe, kind, path))
         return NULL;
   }
   item = &fe->items[fe->item_count++];
   memset(item, 0, sizeof(*item));
   unifrog_text_copy(item->name, sizeof(item->name),
      tr(fe, name ? name : ""));
   unifrog_text_copy(item->meta, sizeof(item->meta),
      tr(fe, meta ? meta : ""));
   unifrog_text_copy(item->path, sizeof(item->path), path ? path : "");
   unifrog_text_copy(item->core, sizeof(item->core), core ? core : "");
   item->kind = kind;
   item->action = kind == FRONTEND_ITEM_ACTION ?
      unifrog_frontend_action_from_id(item->path) :
      UNIFROG_FRONTEND_ACTION_NONE;
   return item;
}

struct frontend_item *add_info(struct frontend_state *fe,
   const char *name, const char *meta)
{
   return add_item(fe, name, meta, FRONTEND_ITEM_INFO, "", NULL);
}

void frontend_model_settings(const struct frontend_state *fe,
   struct unifrog_frontend_model_settings *settings)
{
   memset(settings, 0, sizeof(*settings));
   settings->sort_desc = fe->sort_desc;
   settings->clock_enabled = fe->clock_enabled;
   settings->title_include_root = fe->title_include_root;
   settings->menu_counter_folder = fe->menu_counter_folder;
   settings->menu_counter_file = fe->menu_counter_file;
   settings->show_hidden = fe->show_hidden;
   settings->content_collect = fe->content_collect;
   settings->content_history = fe->content_history;
   settings->mixed_content = fe->mixed_content;
   settings->theme = active_theme_label((struct frontend_state *)fe);
   settings->language = active_language_label((struct frontend_state *)fe);
   settings->rom_root_label = frontend_rom_root_label(fe);
   settings->rom_root = frontend_rom_root(fe);
   settings->active_storage_profile = unifrog_platform_sd_active_profile();
   settings->configured_storage_profile = fe->storage_profile;
   settings->boot_storage_profile = UNIFROG_SD_MODE;
   settings->storage_normal_profile = fe->storage_normal_profile;
   settings->storage_fallback_profile = fe->storage_fallback_profile;
}

void add_model_items(struct frontend_state *fe,
   const struct unifrog_frontend_model *model)
{
   if (!fe || !model)
      return;
   for (unsigned i = 0; i < model->count; i++) {
      const struct unifrog_frontend_model_item *item = &model->items[i];

      add_item(fe, item->label, item->detail,
         item->action == UNIFROG_FRONTEND_ACTION_NONE ?
            FRONTEND_ITEM_INFO : FRONTEND_ITEM_ACTION,
         unifrog_frontend_action_id(item->action),
         item->payload[0] ? item->payload : NULL);
   }
   if (model->status[0])
      frontend_set_status(fe, "%s", model->status);
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

void sort_items(struct frontend_state *fe)
{
   frontend_sort_desc = fe ? fe->sort_desc : 0;
   qsort(fe->items, fe->item_count, sizeof(fe->items[0]), item_compare);
}

void clamp_selection(struct frontend_state *fe)
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

void restore_view_selection(struct frontend_state *fe, unsigned selected,
   unsigned scroll)
{
   fe->selected = selected;
   fe->scroll = scroll;
   clamp_selection(fe);
   fe->needs_draw = 1;
   log_selection(fe, "back");
}

void log_selection(struct frontend_state *fe, const char *reason)
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

void log_item_sample(struct frontend_state *fe, const char *tag)
{
   unsigned limit = fe->item_count < 8u ? fe->item_count : 8u;

   for (unsigned i = 0; i < limit; i++) {
      struct frontend_item *item = &fe->items[i];

      unifrog_log("frontend %s item=%u/%u name=%s meta=%s path=%s kind=%d core=%s\n",
         tag ? tag : "items", i + 1u, fe->item_count, item->name,
         item->meta, item->path, item->kind, item->core);
   }
}
