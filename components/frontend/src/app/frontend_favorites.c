#include "frontend_internal.h"

static int favorite_entry_matches(const char *line, const char *path)
{
   char scratch[FRONTEND_MAX_LINE];
   char *sep;

   unifrog_text_copy(scratch, sizeof(scratch), line ? line : "");
   sep = strchr(scratch, '|');
   if (sep)
      *sep = '\0';
   return path && strcmp(scratch, path) == 0;
}

int frontend_favorite_contains(const char *path)
{
   FILE *file;
   char line[FRONTEND_MAX_LINE];

   if (!path || !path[0])
      return 0;
   file = fopen(FRONTEND_FAVORITES_PATH, "rb");
   if (!file)
      return 0;
   while (fgets(line, sizeof(line), file)) {
      frontend_strip_eol(line);
      if (favorite_entry_matches(line, path)) {
         fclose(file);
         return 1;
      }
   }
   fclose(file);
   return 0;
}

void frontend_favorite_toggle(struct frontend_state *fe,
   const struct frontend_item *item)
{
   FILE *in;
   FILE *out;
   char entries[FRONTEND_FAVORITES_MAX][FRONTEND_MAX_LINE];
   char tmp[FRONTEND_MAX_PATH];
   unsigned entry_count = 0;
   int removed = 0;

   if (!item || !item->path[0])
      return;
   if (!fe->content_collect) {
      frontend_set_status(fe, "collection disabled");
      return;
   }
   frontend_ensure_data_dirs();
   in = fopen(FRONTEND_FAVORITES_PATH, "rb");
   if (in) {
      char line[FRONTEND_MAX_LINE];

      while (fgets(line, sizeof(line), in) &&
         entry_count < ARRAY_SIZE(entries)) {
         frontend_strip_eol(line);
         if (favorite_entry_matches(line, item->path)) {
            removed = 1;
            continue;
         }
         unifrog_text_copy(entries[entry_count++], sizeof(entries[0]), line);
      }
      fclose(in);
   }

   snprintf(tmp, sizeof(tmp), "%s.tmp", FRONTEND_FAVORITES_PATH);
   out = fopen(tmp, "wb");
   if (!out)
      return;
   if (!removed)
      fprintf(out, "%s|%s\n", item->path, item->core);
   for (unsigned i = 0; i < entry_count; i++)
      fprintf(out, "%s\n", entries[i]);
   if (fclose(out) == 0) {
      if (unifrog_config_commit(tmp, FRONTEND_FAVORITES_PATH) == 0)
         frontend_set_status(fe, "%s favorite", removed ? "removed" : "added");
      else {
         unlink(tmp);
         frontend_set_status(fe, "favorite save failed");
      }
   } else {
      unlink(tmp);
   }
}
