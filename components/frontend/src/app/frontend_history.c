#include "frontend_internal.h"

void frontend_history_record(struct frontend_state *fe, const char *path,
   const char *core)
{
   FILE *in;
   FILE *out;
   char tmp[FRONTEND_MAX_PATH];
   char entries[FRONTEND_HISTORY_MAX][FRONTEND_MAX_LINE];
   unsigned entry_count = 0;

   if (!path || !path[0])
      return;
   if (!fe->content_history) {
      unifrog_text_copy(fe->last_path, sizeof(fe->last_path), path);
      unifrog_text_copy(fe->last_core, sizeof(fe->last_core), core ? core : "");
      save_settings(fe);
      return;
   }
   frontend_ensure_data_dirs();
   unifrog_text_copy(fe->last_path, sizeof(fe->last_path), path);
   unifrog_text_copy(fe->last_core, sizeof(fe->last_core), core ? core : "");

   in = fopen(FRONTEND_HISTORY_PATH, "rb");
   if (in) {
      char line[FRONTEND_MAX_LINE];

      while (fgets(line, sizeof(line), in) &&
         entry_count < ARRAY_SIZE(entries)) {
         char *sep;

         frontend_strip_eol(line);
         sep = strchr(line, '|');
         if (sep)
            *sep = '\0';
         if (strcmp(line, path) == 0)
            continue;
         if (sep)
            *sep = '|';
         unifrog_text_copy(entries[entry_count++], sizeof(entries[0]), line);
      }
      fclose(in);
   }

   snprintf(tmp, sizeof(tmp), "%s.tmp", FRONTEND_HISTORY_PATH);
   out = fopen(tmp, "wb");
   if (!out)
      return;
   fprintf(out, "%s|%s\n", path, core ? core : "");
   for (unsigned i = 0; i < entry_count && i + 1u < ARRAY_SIZE(entries); i++)
      fprintf(out, "%s\n", entries[i]);
   if (fclose(out) == 0) {
      if (unifrog_config_commit(tmp, FRONTEND_HISTORY_PATH) != 0)
         unlink(tmp);
   } else {
      unlink(tmp);
   }
   save_settings(fe);
}
