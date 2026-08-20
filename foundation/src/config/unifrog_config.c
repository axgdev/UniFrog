#include <unifrog/config.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define CONFIG_LINE_MAX 384u
#define CONFIG_SECTION_MAX 320u

static char *trim_ascii(char *text)
{
   char *end;

   while (*text == ' ' || *text == '\t')
      text++;
   end = text + strlen(text);
   while (end > text && (end[-1] == ' ' || end[-1] == '\t' ||
          end[-1] == '\r' || end[-1] == '\n'))
      *--end = '\0';
   return text;
}

static int read_complete_line(FILE *file, char *line, size_t size,
   unsigned *errors)
{
   size_t length;

   if (!fgets(line, size, file))
      return 0;
   length = strlen(line);
   if (length && line[length - 1u] == '\n')
      return 1;
   if (feof(file))
      return 1;
   while (fgetc(file) != '\n' && !feof(file))
      ;
   if (errors)
      (*errors)++;
   line[0] = '\0';
   return 1;
}

static char *parse_value(char *value, unsigned *errors)
{
   char quote;
   char *src;
   char *dst;

   value = trim_ascii(value);
   quote = value[0];
   if (quote != '\'' && quote != '"') {
      for (char *p = value; *p; p++) {
         if ((*p == '#' || *p == ';') &&
             (p == value || p[-1] == ' ' || p[-1] == '\t')) {
            *p = '\0';
            break;
         }
      }
      return trim_ascii(value);
   }

   src = value + 1;
   dst = value;
   while (*src && *src != quote) {
      if (*src == '\\' && quote == '"' && src[1]) {
         src++;
         if (*src == 'n')
            *dst++ = '\n';
         else if (*src == 'r')
            *dst++ = '\r';
         else if (*src == 't')
            *dst++ = '\t';
         else
            *dst++ = *src;
         src++;
      } else {
         *dst++ = *src++;
      }
   }
   if (*src != quote) {
      if (errors)
         (*errors)++;
      return NULL;
   }
   *dst = '\0';
   src = trim_ascii(src + 1);
   if (*src && *src != '#' && *src != ';') {
      if (errors)
         (*errors)++;
      return NULL;
   }
   return value;
}

int unifrog_config_read(const char *path, unifrog_config_entry_cb callback,
   void *userdata, unsigned *error_count)
{
   FILE *file;
   char line[CONFIG_LINE_MAX];
   char section[CONFIG_SECTION_MAX] = "";
   unsigned errors = 0;
   unsigned line_number = 0;

   if (error_count)
      *error_count = 0;
   if (!path || !callback)
      return -2;
   file = fopen(path, "rb");
   if (!file)
      return -1;
   while (read_complete_line(file, line, sizeof(line), &errors)) {
      char *text;
      char *eq;
      char *key;
      char *value;

      line_number++;
      if (!line[0])
         continue;
      text = trim_ascii(line);
      if (line_number == 1u && strlen(text) >= 3u &&
          (unsigned char)text[0] == 0xefu &&
          (unsigned char)text[1] == 0xbbu &&
          (unsigned char)text[2] == 0xbfu)
         text = trim_ascii(text + 3);
      if (!text[0] || text[0] == '#' || text[0] == ';')
         continue;
      if (text[0] == '[') {
         char *end = strchr(text + 1, ']');

         if (!end) {
            errors++;
            continue;
         }
         *end = '\0';
         text = trim_ascii(text + 1);
         end = trim_ascii(end + 1);
         if (!text[0] || (*end && *end != '#' && *end != ';') ||
             strlen(text) >= sizeof(section)) {
            errors++;
            continue;
         }
         snprintf(section, sizeof(section), "%s", text);
         continue;
      }
      eq = strchr(text, '=');
      if (!eq) {
         errors++;
         continue;
      }
      *eq++ = '\0';
      key = trim_ascii(text);
      value = parse_value(eq, &errors);
      if (!key[0] || !value)
         continue;
      if (callback(userdata, section, key, value, line_number) != 0)
         break;
   }
   fclose(file);
   if (error_count)
      *error_count = errors;
   return 0;
}

static int line_section_name(const char *line, char *section,
   size_t section_size)
{
   const char *start = line;
   const char *end;
   size_t length;

   while (*start == ' ' || *start == '\t')
      start++;
   if (*start != '[')
      return 0;
   end = strchr(start + 1, ']');
   if (!end)
      return 0;
   start++;
   while (start < end && (*start == ' ' || *start == '\t'))
      start++;
   while (end > start && (end[-1] == ' ' || end[-1] == '\t'))
      end--;
   length = (size_t)(end - start);
   if (!length || length >= section_size)
      return 0;
   memcpy(section, start, length);
   section[length] = '\0';
   return 1;
}

static int rewrite_section(const char *path, const char *section,
   unifrog_config_section_writer writer, void *userdata)
{
   char temporary[CONFIG_SECTION_MAX];
   char line[CONFIG_LINE_MAX];
   char current[CONFIG_SECTION_MAX];
   FILE *input;
   FILE *output;
   int skipping = 0;
   int ret = 0;

   if (!path || !section || !section[0])
      return -2;
   if (snprintf(temporary, sizeof(temporary), "%s.tmp", path) >=
       (int)sizeof(temporary))
      return -2;
   input = fopen(path, "rb");
   output = fopen(temporary, "wb");
   if (!output) {
      if (input)
         fclose(input);
      return -1;
   }
   if (input) {
      while (fgets(line, sizeof(line), input)) {
         if (line_section_name(line, current, sizeof(current)))
            skipping = strcmp(current, section) == 0;
         if (!skipping && fputs(line, output) == EOF) {
            ret = -1;
            break;
         }
      }
      if (ferror(input))
         ret = -1;
      fclose(input);
   }
   if (ret == 0 && writer) {
      if (fprintf(output, "\n[%s]\n", section) < 0 ||
          writer(output, userdata) != 0)
         ret = -1;
   }
   if (fclose(output) != 0)
      ret = -1;
   if (ret == 0)
      ret = unifrog_config_commit(temporary, path);
   if (ret != 0)
      unlink(temporary);
   return ret;
}

int unifrog_config_replace_section(const char *path, const char *section,
   unifrog_config_section_writer writer, void *userdata)
{
   return writer ? rewrite_section(path, section, writer, userdata) : -2;
}

int unifrog_config_remove_section(const char *path, const char *section)
{
   if (!path || access(path, F_OK) != 0)
      return -1;
   return rewrite_section(path, section, NULL, NULL);
}

int unifrog_config_commit(const char *temporary, const char *path)
{
   char backup[CONFIG_SECTION_MAX];

   if (!temporary || !path ||
       snprintf(backup, sizeof(backup), "%s.bak", path) >=
       (int)sizeof(backup))
      return -2;
   if (rename(temporary, path) != 0) {
      int moved_old = 0;
      int ret = 0;

      unlink(backup);
      if (rename(path, backup) == 0)
         moved_old = 1;
      else if (errno != ENOENT)
         ret = -1;
      if (ret == 0 && rename(temporary, path) != 0) {
         ret = -1;
         if (moved_old)
            (void)rename(backup, path);
      } else if (ret == 0 && moved_old) {
         unlink(backup);
      }
      return ret;
   }
   return 0;
}
