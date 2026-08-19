#include <unifrog/frontend_model.h>
#include <unifrog/text.h>

#include <stdio.h>
#include <string.h>

static int has_action(const struct unifrog_frontend_model *model,
   enum unifrog_frontend_action action)
{
   for (unsigned i = 0; i < model->count; i++) {
      if (model->items[i].action == action)
         return 1;
   }
   return 0;
}

static const struct unifrog_frontend_model_item *find_label(
   const struct unifrog_frontend_model *model, const char *label)
{
   for (unsigned i = 0; i < model->count; i++) {
      if (strcmp(model->items[i].label, label) == 0)
         return &model->items[i];
   }
   return NULL;
}

int main(void)
{
   struct unifrog_frontend_model_settings settings;
   struct unifrog_frontend_model model;
   const struct unifrog_frontend_model_item *item;
   char marquee[16];

   memset(&settings, 0, sizeof(settings));
   settings.theme = "muos";
   settings.language = "english";
   settings.rom_root_label = "ROMs";
   settings.rom_root = "/ROMS";
   settings.active_storage_profile = "wide25";
   settings.configured_storage_profile = "wide25";
   settings.boot_storage_profile = "wide25";

   unifrog_frontend_model_build(&model, UNIFROG_FRONTEND_MODEL_LAUNCH,
      &settings);
   if (model.count != 8u || !has_action(&model, UNIFROG_FRONTEND_ACTION_SHUTDOWN))
      return 1;
   unifrog_frontend_model_move(&model, -1);
   if (model.selected != 7u)
      return 1;

   unifrog_frontend_model_build(&model, UNIFROG_FRONTEND_MODEL_VISUAL,
      &settings);
   if (find_label(&model, "Folder Item Count") ||
       find_label(&model, "Display Empty Folder"))
      return 1;

   unifrog_frontend_model_build(&model, UNIFROG_FRONTEND_MODEL_STORAGE,
      &settings);
   item = find_label(&model, "ROMs");
   if (!item || strcmp(item->detail, "/ROMS") != 0)
      return 1;

   unifrog_frontend_model_build(&model, UNIFROG_FRONTEND_MODEL_STORAGE_MODE,
      &settings);
   item = find_label(&model, "Signal");
   if (!item || strcmp(item->detail, "3.3 V") != 0)
      return 1;
   item = find_label(&model, "wide25");
   if (!item || strstr(item->detail, " *") == NULL)
      return 1;

   if (unifrog_text_marquee(marquee, sizeof(marquee), "short", 8u, 5000u) ||
       strcmp(marquee, "short") != 0)
      return 1;
   if (!unifrog_text_marquee(marquee, sizeof(marquee),
         "abcdefghijkl", 8u, 0u) || strcmp(marquee, "abcdefgh") != 0)
      return 1;
   if (!unifrog_text_marquee(marquee, sizeof(marquee),
         "abcdefghijkl", 8u, 1080u) || strcmp(marquee, "bcdefghi") != 0)
      return 1;

   puts("OK frontend model");
   return 0;
}
