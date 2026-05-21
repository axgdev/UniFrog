#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 320
#define H 240

struct style {
   uint8_t bg[3];
   uint8_t header[3];
   uint8_t footer[3];
   uint8_t list[3];
   uint8_t focus[3];
   uint8_t text[3];
   uint8_t focus_text[3];
   int bg_alpha;
   int header_alpha;
   int footer_alpha;
   int list_alpha;
   int focus_alpha;
   int text_alpha;
   int focus_text_alpha;
};

static char *trim(char *s)
{
   char *e;

   while (*s == ' ' || *s == '\t')
      s++;
   e = s + strlen(s);
   while (e > s && (e[-1] == ' ' || e[-1] == '\t' ||
          e[-1] == '\n' || e[-1] == '\r'))
      *--e = '\0';
   return s;
}

static int hex_color(const char *s, uint8_t out[3])
{
   char *end;
   unsigned long v;

   if (!s)
      return -1;
   while (*s == ' ' || *s == '\t' || *s == '#')
      s++;
   v = strtoul(s, &end, 16);
   if (!end || *trim(end) || v > 0xfffffful)
      return -1;
   out[0] = (uint8_t)((v >> 16) & 0xffu);
   out[1] = (uint8_t)((v >> 8) & 0xffu);
   out[2] = (uint8_t)(v & 0xffu);
   return 0;
}

static int alpha_value(const char *s, int fallback)
{
   char *end;
   long v;

   if (!s)
      return fallback;
   v = strtol(s, &end, 10);
   if (!end || *trim(end))
      return fallback;
   if (v < 0)
      return 0;
   if (v > 255)
      return 255;
   return (int)v;
}

static void set3(uint8_t dst[3], unsigned r, unsigned g, unsigned b)
{
   dst[0] = (uint8_t)r;
   dst[1] = (uint8_t)g;
   dst[2] = (uint8_t)b;
}

static void blend(uint8_t dst[3], const uint8_t src[3], int alpha)
{
   int inv = 255 - alpha;

   for (int i = 0; i < 3; i++)
      dst[i] = (uint8_t)((src[i] * alpha + dst[i] * inv + 127) / 255);
}

static void rect(uint8_t *pix, int x, int y, int w, int h,
   const uint8_t color[3], int alpha)
{
   for (int yy = y; yy < y + h; yy++) {
      for (int xx = x; xx < x + w; xx++) {
         uint8_t *p;

         if (xx < 0 || yy < 0 || xx >= W || yy >= H)
            continue;
         p = pix + ((yy * W + xx) * 3);
         blend(p, color, alpha);
      }
   }
}

static int parse_ini(const char *path, struct style *style)
{
   FILE *file = fopen(path, "rb");
   char section[64] = "";
   char line[512];

   if (!file)
      return -1;
   while (fgets(line, sizeof(line), file)) {
      char *key;
      char *value;
      char *eq;

      key = trim(line);
      if (!key[0] || key[0] == '#' || key[0] == ';')
         continue;
      if (key[0] == '[') {
         char *end = strchr(key + 1, ']');

         if (end) {
            *end = '\0';
            snprintf(section, sizeof(section), "%s", trim(key + 1));
         }
         continue;
      }
      eq = strchr(key, '=');
      if (!eq)
         continue;
      *eq++ = '\0';
      key = trim(key);
      value = trim(eq);
      if (strcmp(section, "background") == 0 &&
          strcmp(key, "BACKGROUND") == 0)
         (void)hex_color(value, style->bg);
      else if (strcmp(section, "background") == 0 &&
               strcmp(key, "BACKGROUND_ALPHA") == 0)
         style->bg_alpha = alpha_value(value, style->bg_alpha);
      else if (strcmp(section, "header") == 0 &&
               strcmp(key, "HEADER_BACKGROUND") == 0)
         (void)hex_color(value, style->header);
      else if (strcmp(section, "header") == 0 &&
               strcmp(key, "HEADER_BACKGROUND_ALPHA") == 0)
         style->header_alpha = alpha_value(value, style->header_alpha);
      else if (strcmp(section, "footer") == 0 &&
               strcmp(key, "FOOTER_BACKGROUND") == 0)
         (void)hex_color(value, style->footer);
      else if (strcmp(section, "footer") == 0 &&
               strcmp(key, "FOOTER_BACKGROUND_ALPHA") == 0)
         style->footer_alpha = alpha_value(value, style->footer_alpha);
      else if (strcmp(section, "list") == 0 &&
               strcmp(key, "LIST_DEFAULT_BACKGROUND") == 0)
         (void)hex_color(value, style->list);
      else if (strcmp(section, "list") == 0 &&
               strcmp(key, "LIST_DEFAULT_BACKGROUND_ALPHA") == 0)
         style->list_alpha = alpha_value(value, style->list_alpha);
      else if (strcmp(section, "list") == 0 &&
               strcmp(key, "LIST_FOCUS_BACKGROUND") == 0)
         (void)hex_color(value, style->focus);
      else if (strcmp(section, "list") == 0 &&
               strcmp(key, "LIST_FOCUS_BACKGROUND_ALPHA") == 0)
         style->focus_alpha = alpha_value(value, style->focus_alpha);
      else if (strcmp(section, "list") == 0 &&
               strcmp(key, "LIST_DEFAULT_TEXT") == 0)
         (void)hex_color(value, style->text);
      else if (strcmp(section, "list") == 0 &&
               strcmp(key, "LIST_DEFAULT_TEXT_ALPHA") == 0)
         style->text_alpha = alpha_value(value, style->text_alpha);
      else if (strcmp(section, "list") == 0 &&
               strcmp(key, "LIST_FOCUS_TEXT") == 0)
         (void)hex_color(value, style->focus_text);
      else if (strcmp(section, "list") == 0 &&
               strcmp(key, "LIST_FOCUS_TEXT_ALPHA") == 0)
         style->focus_text_alpha =
            alpha_value(value, style->focus_text_alpha);
   }
   fclose(file);
   return 0;
}

static int write_ppm(const char *path, const uint8_t *pix)
{
   FILE *file = fopen(path, "wb");

   if (!file)
      return -1;
   fprintf(file, "P6\n%d %d\n255\n", W, H);
   if (fwrite(pix, 1, W * H * 3u, file) != W * H * 3u) {
      fclose(file);
      return -1;
   }
   return fclose(file);
}

int main(int argc, char **argv)
{
   struct style style;
   uint8_t *pix;
   unsigned unique = 0;
   uint32_t seen[32];

   if (argc != 3) {
      fprintf(stderr, "usage: %s scheme.ini preview.ppm\n", argv[0]);
      return 2;
   }
   set3(style.bg, 8, 9, 12);
   set3(style.header, 20, 22, 29);
   set3(style.footer, 20, 22, 29);
   set3(style.list, 20, 22, 29);
   set3(style.focus, 52, 104, 132);
   set3(style.text, 238, 241, 232);
   set3(style.focus_text, 255, 255, 255);
   style.bg_alpha = 255;
   style.header_alpha = 255;
   style.footer_alpha = 255;
   style.list_alpha = 255;
   style.focus_alpha = 255;
   style.text_alpha = 255;
   style.focus_text_alpha = 255;
   if (parse_ini(argv[1], &style) != 0)
      return 1;
   pix = calloc(W * H, 3);
   if (!pix)
      return 1;
   rect(pix, 0, 0, W, H, style.bg, style.bg_alpha);
   rect(pix, 0, 0, W, 38, style.header, style.header_alpha);
   rect(pix, 0, H - 38, W, 38, style.footer, style.footer_alpha);
   for (int i = 0; i < 6; i++)
      rect(pix, 10, 48 + i * 24, 300, 22,
         i == 0 ? style.focus : style.list,
         i == 0 ? style.focus_alpha : style.list_alpha);
   for (int i = 0; i < 6; i++) {
      rect(pix, 34, 55 + i * 24, 62, 2,
         i == 0 ? style.focus_text : style.text,
         i == 0 ? style.focus_text_alpha : style.text_alpha);
      rect(pix, 34, 60 + i * 24, 96, 2,
         i == 0 ? style.focus_text : style.text,
         i == 0 ? style.focus_text_alpha : style.text_alpha);
   }
   for (unsigned i = 0; i < W * H; i += 173) {
      uint8_t *p = pix + i * 3u;
      uint32_t c = ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
      int found = 0;

      for (unsigned j = 0; j < unique; j++) {
         if (seen[j] == c) {
            found = 1;
            break;
         }
      }
      if (!found && unique < sizeof(seen) / sizeof(seen[0]))
         seen[unique++] = c;
   }
   if (write_ppm(argv[2], pix) != 0) {
      free(pix);
      return 1;
   }
   free(pix);
   printf("muos theme visual preview=%s unique_sampled_colors=%u\n",
      argv[2], unique);
   return unique >= 3 ? 0 : 1;
}
