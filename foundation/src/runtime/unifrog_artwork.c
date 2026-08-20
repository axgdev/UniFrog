#include <unifrog/artwork.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <unifrog/paths.h>

struct artwork_tokens {
   char rom_dir[UNIFROG_ARTWORK_PATH_MAX];
   char system[64];
   char name[128];
   char filename[128];
};

static int append_text(char *out, size_t size, size_t *used, const char *text)
{
   size_t len = strlen(text);

   if (*used + len >= size)
      return -1;
   memcpy(out + *used, text, len);
   *used += len;
   out[*used] = '\0';
   return 0;
}

static int expand_template(const char *format,
   const struct artwork_tokens *tokens, char *out, size_t size)
{
   size_t used = 0;

   if (!format || !tokens || !out || size == 0)
      return -1;
   out[0] = '\0';
   while (*format) {
      const char *value = NULL;
      size_t token = 0;

      if (strncmp(format, "{rom_dir}", 9) == 0) {
         value = tokens->rom_dir;
         token = 9;
      } else if (strncmp(format, "{system}", 8) == 0) {
         value = tokens->system;
         token = 8;
      } else if (strncmp(format, "{filename}", 10) == 0) {
         value = tokens->filename;
         token = 10;
      } else if (strncmp(format, "{name}", 6) == 0) {
         value = tokens->name;
         token = 6;
      }
      if (value) {
         if (append_text(out, size, &used, value) != 0)
            return -1;
         format += token;
      } else {
         char one[2] = { *format++, '\0' };

         if (append_text(out, size, &used, one) != 0)
            return -1;
      }
   }
   return 0;
}

static void copy_part(char *out, size_t size, const char *start, size_t len)
{
   if (len >= size)
      len = size - 1u;
   memcpy(out, start, len);
   out[len] = '\0';
}

static int prepare_tokens(const char *rom_path, const char *system,
   struct artwork_tokens *tokens)
{
   const char *slash;
   const char *dot;
   const char *parent;

   if (!rom_path || !rom_path[0] || !tokens)
      return -1;
   memset(tokens, 0, sizeof(*tokens));
   slash = strrchr(rom_path, '/');
   if (!slash || slash == rom_path || !slash[1])
      return -1;
   copy_part(tokens->rom_dir, sizeof(tokens->rom_dir), rom_path,
      (size_t)(slash - rom_path));
   copy_part(tokens->filename, sizeof(tokens->filename), slash + 1,
      strlen(slash + 1));
   dot = strrchr(tokens->filename, '.');
   copy_part(tokens->name, sizeof(tokens->name), tokens->filename,
      dot && dot != tokens->filename ? (size_t)(dot - tokens->filename) :
      strlen(tokens->filename));
   if (system && system[0]) {
      copy_part(tokens->system, sizeof(tokens->system), system,
         strlen(system));
      return 0;
   }
   parent = strrchr(tokens->rom_dir, '/');
   system = parent && parent[1] ? parent + 1 : tokens->rom_dir;
   copy_part(tokens->system, sizeof(tokens->system), system, strlen(system));
   return 0;
}

static void resolve_list(const char *templates,
   const struct artwork_tokens *tokens, char *out, size_t size)
{
   const char *part = templates;

   out[0] = '\0';
   while (part && *part) {
      const char *end = strchr(part, '|');
      size_t len = end ? (size_t)(end - part) : strlen(part);
      char format[UNIFROG_ARTWORK_PATH_MAX];
      char expanded[UNIFROG_ARTWORK_PATH_MAX];
      char absolute[UNIFROG_ARTWORK_PATH_MAX];

      while (len && (*part == ' ' || *part == '\t')) {
         part++;
         len--;
      }
      while (len && (part[len - 1u] == ' ' || part[len - 1u] == '\t'))
         len--;
      copy_part(format, sizeof(format), part, len);
      if (format[0] && expand_template(format, tokens, expanded,
          sizeof(expanded)) == 0) {
         const char *candidate = expanded;

         if (expanded[0] != '/') {
            if (snprintf(absolute, sizeof(absolute), "%s/%s", UNIFROG_SD_ROOT,
                expanded) < (int)sizeof(absolute))
               candidate = absolute;
            else
               candidate = NULL;
         }
         if (candidate && access(candidate, R_OK) == 0) {
            snprintf(out, size, "%s", candidate);
            return;
         }
      }
      part = end ? end + 1 : NULL;
   }
}

int unifrog_artwork_resolve(const char *rom_path, const char *system,
   const char *box_templates, const char *preview_templates,
   const char *text_templates, struct unifrog_artwork_paths *paths)
{
   struct artwork_tokens tokens;

   if (!paths || prepare_tokens(rom_path, system, &tokens) != 0)
      return -1;
   memset(paths, 0, sizeof(*paths));
   resolve_list(box_templates, &tokens, paths->box, sizeof(paths->box));
   resolve_list(preview_templates, &tokens, paths->preview,
      sizeof(paths->preview));
   resolve_list(text_templates, &tokens, paths->text, sizeof(paths->text));
   return paths->box[0] || paths->preview[0] || paths->text[0] ? 0 : 1;
}
