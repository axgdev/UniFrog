#include "frontend_internal.h"

/* Shared frontend text, INI, and theme-value parsing helpers. */
void frontend_strip_eol(char *text)
{
   size_t len;

   if (!text)
      return;
   len = strlen(text);
   while (len > 0 && (text[len - 1u] == '\n' || text[len - 1u] == '\r' ||
          text[len - 1u] == ' ' || text[len - 1u] == '\t'))
      text[--len] = '\0';
}

const char *frontend_read_key_value(const char *line, const char *key)
{
   size_t key_len;

   if (!line || !key)
      return NULL;
   key_len = strlen(key);
   if (strncmp(line, key, key_len) == 0 && line[key_len] == '=')
      return line + key_len + 1u;
   return NULL;
}

int frontend_read_file_key(char *dst, size_t dst_size, const char *path,
   const char *key)
{
   FILE *file;
   char line[FRONTEND_MAX_LINE];
   int ret = -1;

   if (!dst || dst_size == 0 || !path || !key)
      return -1;
   dst[0] = '\0';
   file = fopen(path, "rb");
   if (!file)
      return -1;
   while (fgets(line, sizeof(line), file)) {
      const char *value;

      frontend_strip_eol(line);
      value = frontend_read_key_value(line, key);
      if (!value)
         continue;
      unifrog_text_copy(dst, dst_size, value);
      ret = 0;
      break;
   }
   fclose(file);
   return ret;
}

int frontend_parse_int(const char *text, int fallback)
{
   char *end = NULL;
   long value;

   if (!text || !text[0])
      return fallback;
   errno = 0;
   value = strtol(text, &end, 10);
   if (errno == ERANGE || !end || *end || value < INT_MIN || value > INT_MAX)
      return fallback;
   return (int)value;
}

unsigned frontend_parse_unsigned_setting(const char *text, unsigned fallback)
{
   char *end = NULL;
   unsigned long value;

   if (!text || !text[0])
      return fallback;
   errno = 0;
   value = strtoul(text, &end, 10);
   if (errno == ERANGE || !end || *end || value > UINT_MAX)
      return fallback;
   return (unsigned)value;
}

size_t frontend_parse_size_setting(const char *text, size_t fallback)
{
   char *end = NULL;
   unsigned long value;

   if (!text || !text[0])
      return fallback;
   errno = 0;
   value = strtoul(text, &end, 10);
   if (errno == ERANGE || !end || *end || value > SIZE_MAX)
      return fallback;
   return (size_t)value;
}

char *frontend_trim_ascii(char *text)
{
   char *end;

   if (!text)
      return text;
   while (*text == ' ' || *text == '\t')
      text++;
   end = text + strlen(text);
   while (end > text && (end[-1] == ' ' || end[-1] == '\t'))
      *--end = '\0';
   return text;
}

void frontend_cycle_string_choice(char *value, size_t value_size,
   const char *const *choices, unsigned count)
{
   unsigned current = 0;

   if (!value || !value_size || !choices || !count)
      return;
   for (unsigned i = 0; i < count; i++) {
      if (strcmp(value, choices[i]) == 0) {
         current = i;
         break;
      }
   }
   unifrog_text_copy(value, value_size, choices[(current + 1u) % count]);
}

uint8_t frontend_parse_alpha(const char *text, uint8_t fallback)
{
   int value = frontend_parse_int(text, fallback);

   if (value < 0)
      return 0;
   if (value > 255)
      return 255;
   return (uint8_t)value;
}

int frontend_parse_rgb565_hex(const char *text, uint16_t *out)
{
   char *end = NULL;
   unsigned long value;

   if (!text || !text[0] || !out)
      return -1;
   value = strtoul(text, &end, 16);
   if (!end || *end || value > 0xfffful)
      return -1;
   *out = (uint16_t)value;
   return 0;
}

uint16_t frontend_rgb888_to_rgb565(uint32_t color)
{
   unsigned r = (color >> 16) & 0xffu;
   unsigned g = (color >> 8) & 0xffu;
   unsigned b = color & 0xffu;

   return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

int frontend_parse_theme_hex(const char *text, uint16_t *out)
{
   char *end = NULL;
   unsigned long value;

   if (!text || !text[0] || !out)
      return -1;
   while (*text == '#' || *text == ' ' || *text == '\t')
      text++;
   value = strtoul(text, &end, 16);
   if (!end || *end)
      return -1;
   if (value <= 0xfffful)
      *out = (uint16_t)value;
   else if (value <= 0xfffffful)
      *out = frontend_rgb888_to_rgb565((uint32_t)value);
   else
      return -1;
   return 0;
}

void frontend_strip_ini_suffix(char *name)
{
   size_t len = name ? strlen(name) : 0;

   if (len > 4u && strcasecmp(name + len - 4u, ".ini") == 0)
      name[len - 4u] = '\0';
}

void frontend_strip_known_suffix(char *name, const char *suffix)
{
   size_t name_len = name ? strlen(name) : 0;
   size_t suffix_len = suffix ? strlen(suffix) : 0;

   if (name_len > suffix_len &&
       strcasecmp(name + name_len - suffix_len, suffix) == 0)
      name[name_len - suffix_len] = '\0';
}

void frontend_sanitize_slot_name(char *name)
{
   int wrote = 0;

   if (!name)
      return;
   for (char *p = name; *p; p++) {
      char c = *p;

      if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')
         name[wrote++] = c;
      else if (wrote > 0 && name[wrote - 1] != '-')
         name[wrote++] = '-';
   }
   while (wrote > 0 && (name[wrote - 1] == '-' || name[wrote - 1] == '.'))
      wrote--;
   name[wrote] = '\0';
   if (!name[0])
      unifrog_text_copy(name, 16, "package");
}
