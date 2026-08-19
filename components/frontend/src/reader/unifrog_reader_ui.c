#include <unifrog/reader.h>

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include <unifrog/fb.h>
#include <unifrog/config.h>
#include <unifrog/ge.h>
#include <unifrog/gfx.h>
#include <unifrog/image.h>
#include <unifrog/log.h>
#include <unifrog/paths.h>
#include <unifrog/perf.h>
#include <unifrog/text.h>
#include <unifrog/ui.h>
#include <unifrog/zip.h>

#define READER_MAX_PAGES 2048u
#define READER_PATH_MAX 512u
#define READER_HEADER_H 20
#define READER_FOOTER_H 18
#define READER_TEXT_LINES_MAX 26u
#define READER_TEXT_COLUMNS_MAX 54u
#define READER_EPUB_XML_MAX (1024u * 1024u)
#define READER_EPUB_ITEMS_MAX 512u

struct reader_page {
   char path[READER_PATH_MAX];
   char title[96];
   int text;
};

struct reader_epub_item {
   char id[96];
   char path[READER_PATH_MAX];
   int text;
};

struct reader {
   struct unifrog_ui ui;
   struct reader_page *pages;
   unsigned page_count;
   unsigned page;
   unifrog_image image;
   int image_loaded;
   int image_cached;
   unifrog_image svg_cache;
   char svg_cache_path[READER_PATH_MAX];
   int zoom_index;
   int pan_x;
   int pan_y;
   int chrome;
   int dirty;
   int menu;
   int menu_selected;
   int rebuild;
   int epub;
   int font_bitmap;
   int text_percent;
   int margin;
   int palette;
   int epub_images;
   char source_path[READER_PATH_MAX];
};

static int reader_join(char *dst, size_t dst_size, const char *dir,
   const char *name);
static char *reader_zip_entry_text(const struct unifrog_zip_archive *zip,
   const struct unifrog_zip_entry *entry, const char *cache_dir);
static int reader_xml_attr(const char *tag, const char *tag_end,
   const char *name, char *out, size_t out_size);

static int reader_mkdir_p(const char *path)
{
   char tmp[READER_PATH_MAX];

   if (!path || !path[0])
      return -1;
   unifrog_text_copy(tmp, sizeof(tmp), path);
   for (char *p = tmp + 1; *p; p++) {
      if (*p == '/') {
         *p = '\0';
         (void)mkdir(tmp, 0777);
         *p = '/';
      }
   }
   return mkdir(tmp, 0777) == 0 || errno == EEXIST ? 0 : -1;
}

static int reader_remove_tree(const char *path)
{
   DIR *dir;
   struct dirent *entry;

   dir = opendir(path);
   if (!dir)
      return errno == ENOENT ? 0 : -1;
   while ((entry = readdir(dir)) != NULL) {
      char child[READER_PATH_MAX];
      struct stat st;

      if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
         continue;
      if (reader_join(child, sizeof(child), path, entry->d_name) != 0)
         continue;
      if (stat(child, &st) == 0 && S_ISDIR(st.st_mode))
         (void)reader_remove_tree(child);
      else
         (void)unlink(child);
   }
   closedir(dir);
   return rmdir(path);
}

static const unsigned reader_zoom_percent[] = { 0u, 100u, 150u, 200u, 300u,
   400u };

static int reader_parse_int(const char *value, int fallback)
{
   char *end;
   long parsed;

   if (!value || !value[0])
      return fallback;
   errno = 0;
   parsed = strtol(value, &end, 10);
   return errno || end == value || *end ? fallback : (int)parsed;
}

static void reader_sanitize_settings(struct reader *reader)
{
   reader->font_bitmap = reader->font_bitmap ? 1 : 0;
   reader->epub_images = reader->epub_images ? 1 : 0;
   if (reader->text_percent != 75 && reader->text_percent != 90 &&
       reader->text_percent != 100 && reader->text_percent != 125 &&
       reader->text_percent != 150)
      reader->text_percent = 100;
   if (reader->margin != 4 && reader->margin != 10 && reader->margin != 16)
      reader->margin = 10;
   if (reader->palette < 0 || reader->palette > 2)
      reader->palette = 0;
}

static int reader_settings_entry(void *userdata, const char *section,
   const char *key, const char *value, unsigned line_number)
{
   struct reader *reader = userdata;
   int parsed;

   (void)line_number;
   if (strcmp(section, "reader") != 0)
      return 0;
   parsed = reader_parse_int(value, -1);
   if (strcmp(key, "font_bitmap") == 0 && (parsed == 0 || parsed == 1))
      reader->font_bitmap = parsed;
   else if (strcmp(key, "text_percent") == 0 &&
            (parsed == 75 || parsed == 90 || parsed == 100 ||
             parsed == 125 || parsed == 150))
      reader->text_percent = parsed;
   else if (strcmp(key, "margin") == 0 &&
            (parsed == 4 || parsed == 10 || parsed == 16))
      reader->margin = parsed;
   else if (strcmp(key, "palette") == 0 && parsed >= 0 && parsed <= 2)
      reader->palette = parsed;
   else if (strcmp(key, "epub_images") == 0 && (parsed == 0 || parsed == 1))
      reader->epub_images = parsed;
   return 0;
}

static void reader_load_settings(struct reader *reader)
{
   reader->font_bitmap = 0;
   reader->text_percent = 100;
   reader->margin = 10;
   reader->palette = 0;
   reader->epub_images = 1;
   (void)unifrog_config_read(UNIFROG_CONFIG_PATH, reader_settings_entry,
      reader, NULL);
   reader_sanitize_settings(reader);
}

static int reader_write_settings(FILE *file, void *userdata)
{
   const struct reader *reader = userdata;

   if (fprintf(file,
       "# Reader preferences. All are configurable in the reader menu.\n"
       "# font_bitmap: 0=vector font, 1=small bitmap font.\n"
       "# text_percent: 75, 90, 100, 125, or 150.\n"
       "# margin: 4, 10, or 16 pixels. palette: 0=light, 1=dark, 2=sepia.\n"
       "# epub_images: 0 hides EPUB images; 1 includes them.\n"
       "font_bitmap=%d\ntext_percent=%d\nmargin=%d\npalette=%d\n"
       "epub_images=%d\n", reader->font_bitmap, reader->text_percent,
       reader->margin, reader->palette, reader->epub_images) < 0)
      return -1;
   return 0;
}

static void reader_save_settings(const struct reader *reader)
{
   int ret = unifrog_config_replace_section(UNIFROG_CONFIG_PATH, "reader",
      reader_write_settings, (void *)reader);

   if (ret != 0)
      unifrog_log("reader settings save_failed path=%s ret=%d\n",
         UNIFROG_CONFIG_PATH, ret);
}

static unsigned reader_text_columns(const struct reader *reader)
{
   unsigned advance = reader->font_bitmap ? 6u :
      (unsigned)unifrog_gfx_font_advance();
   unsigned columns;

   advance = (advance * (unsigned)reader->text_percent + 99u) / 100u;
   columns = (320u - (unsigned)reader->margin * 2u) / (advance ? advance : 6u);
   if (columns < 16u)
      columns = 16u;
   return columns > READER_TEXT_COLUMNS_MAX ? READER_TEXT_COLUMNS_MAX : columns;
}

static unsigned reader_text_lines(const struct reader *reader)
{
   unsigned line_h = reader->font_bitmap ? 10u :
      (unsigned)unifrog_gfx_font_height() + 2u;
   unsigned height = 240u - READER_HEADER_H - READER_FOOTER_H - 12u;
   unsigned lines;

   line_h = (line_h * (unsigned)reader->text_percent + 99u) / 100u;
   lines = height / (line_h ? line_h : 10u);
   return lines > READER_TEXT_LINES_MAX ? READER_TEXT_LINES_MAX : lines;
}

static const char *reader_basename(const char *path)
{
   const char *slash = path ? strrchr(path, '/') : NULL;

   return slash ? slash + 1 : (path ? path : "");
}

static int reader_join(char *dst, size_t dst_size, const char *dir,
   const char *name)
{
   int wrote = snprintf(dst, dst_size, "%s%s%s", dir ? dir : "",
      dir && dir[0] && dir[strlen(dir) - 1u] != '/' ? "/" : "",
      name ? name : "");

   return wrote >= 0 && (size_t)wrote < dst_size ? 0 : -1;
}

static int reader_has_ext(const char *path, const char *ext)
{
   size_t path_len;
   size_t ext_len;

   if (!path || !ext)
      return 0;
   path_len = strlen(path);
   ext_len = strlen(ext);
   if (path_len < ext_len)
      return 0;
   return strcasecmp(path + path_len - ext_len, ext) == 0;
}

static int reader_image_path_supported(const char *path)
{
   return unifrog_reader_path_is_image(path);
}

static int reader_text_path_supported(const char *path)
{
   return unifrog_reader_path_is_text(path);
}

static const char *reader_ext(const char *path)
{
   const char *base = reader_basename(path);
   const char *dot = strrchr(base, '.');

   return dot ? dot : "";
}

static int reader_cache_path(char *dst, size_t dst_size, const char *cache_dir,
   unsigned index, const char *ext)
{
   int wrote = snprintf(dst, dst_size, "%s/%04u%s", cache_dir ? cache_dir : "",
      index, ext ? ext : "");

   return wrote >= 0 && (size_t)wrote < dst_size ? 0 : -1;
}

static int reader_page_compare(const void *a, const void *b)
{
   const struct reader_page *pa = a;
   const struct reader_page *pb = b;

   return strcasecmp(pa->path, pb->path);
}

static int reader_collect_directory(struct reader *reader, const char *path)
{
   char dir_path[READER_PATH_MAX];
   struct stat st;
   DIR *dir;
   struct dirent *entry;

   if (stat(path, &st) != 0)
      return -1;
   if (S_ISDIR(st.st_mode)) {
      unifrog_text_copy(dir_path, sizeof(dir_path), path);
   } else {
      char *slash;

      unifrog_text_copy(dir_path, sizeof(dir_path), path);
      slash = strrchr(dir_path, '/');
      if (slash == dir_path)
         slash[1] = '\0';
      else if (slash)
         *slash = '\0';
      else
         unifrog_text_copy(dir_path, sizeof(dir_path), ".");
   }

   dir = opendir(dir_path);
   if (!dir)
      return -1;
   while (reader->page_count < READER_MAX_PAGES &&
          (entry = readdir(dir)) != NULL) {
      char full[READER_PATH_MAX];
      int text;

      if (entry->d_name[0] == '.' ||
          reader_join(full, sizeof(full), dir_path, entry->d_name) != 0 ||
          (!reader_image_path_supported(full) &&
           !reader_text_path_supported(full)) ||
          reader_has_ext(full, ".cbz") || reader_has_ext(full, ".epub"))
         continue;
      text = reader_text_path_supported(full);
      unifrog_text_copy(reader->pages[reader->page_count].path,
         sizeof(reader->pages[reader->page_count].path), full);
      unifrog_text_copy(reader->pages[reader->page_count].title,
         sizeof(reader->pages[reader->page_count].title),
         reader_basename(full));
      reader->pages[reader->page_count].text = text;
      reader->page_count++;
   }
   closedir(dir);
   qsort(reader->pages, reader->page_count, sizeof(reader->pages[0]),
      reader_page_compare);
   if (!S_ISDIR(st.st_mode)) {
      for (unsigned i = 0; i < reader->page_count; i++) {
         if (strcmp(reader->pages[i].path, path) == 0) {
            reader->page = i;
            break;
         }
      }
   }
   return reader->page_count ? 0 : -1;
}

static int reader_add_extracted_page(struct reader *reader, const char *path,
   const char *title, int text)
{
   if (reader->page_count >= READER_MAX_PAGES)
      return -1;
   unifrog_text_copy(reader->pages[reader->page_count].path,
      sizeof(reader->pages[reader->page_count].path), path);
   unifrog_text_copy(reader->pages[reader->page_count].title,
      sizeof(reader->pages[reader->page_count].title), title);
   reader->pages[reader->page_count].text = text;
   reader->page_count++;
   return 0;
}

static int reader_html_entity(FILE *in, FILE *out)
{
   char entity[16];
   unsigned n = 0;
   int c;

   while ((c = fgetc(in)) != EOF && c != ';' && n + 1u < sizeof(entity))
      entity[n++] = (char)c;
   entity[n] = '\0';
   if (c != ';')
      return -1;
   if (strcmp(entity, "amp") == 0)
      fputc('&', out);
   else if (strcmp(entity, "lt") == 0)
      fputc('<', out);
   else if (strcmp(entity, "gt") == 0)
      fputc('>', out);
   else if (strcmp(entity, "quot") == 0)
      fputc('"', out);
   else if (strcmp(entity, "apos") == 0)
      fputc('\'', out);
   else if (strcmp(entity, "nbsp") == 0)
      fputc(' ', out);
   else
      fputc(' ', out);
   return 0;
}

static int reader_html_to_text(FILE *in, FILE *out)
{
   int c;
   int pending_space = 0;
   int suppress = 0;

   while ((c = fgetc(in)) != EOF) {
      if (c == '<') {
         char tag[32];
         unsigned n = 0;
         int closing = 0;
         int name_done = 0;

         c = fgetc(in);
         if (c == '/') {
            closing = 1;
            c = fgetc(in);
         }
         while (c != EOF && c != '>') {
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n' ||
                c == '/')
               name_done = 1;
            else if (!name_done && n + 1u < sizeof(tag))
               tag[n++] = (char)c;
            c = fgetc(in);
         }
         tag[n] = '\0';
         if (strcasecmp(tag, "script") == 0 || strcasecmp(tag, "style") == 0)
            suppress = closing ? 0 : 1;
         if (!suppress && (strcasecmp(tag, "p") == 0 ||
             strcasecmp(tag, "br") == 0 ||
             strcasecmp(tag, "h1") == 0 || strcasecmp(tag, "h2") == 0 ||
             strcasecmp(tag, "h3") == 0 || strcasecmp(tag, "li") == 0 ||
             strcasecmp(tag, "div") == 0))
            fputc('\n', out);
         continue;
      }
      if (suppress)
         continue;
      if (c == '&') {
         if (pending_space) {
            fputc(' ', out);
            pending_space = 0;
         }
         (void)reader_html_entity(in, out);
         continue;
      }
      if (c == '\r' || c == '\n' || c == '\t' || c == ' ') {
         pending_space = 1;
         continue;
      }
      if (pending_space) {
         fputc(' ', out);
         pending_space = 0;
      }
      fputc(c, out);
   }
   return ferror(in) || ferror(out) ? -1 : 0;
}

static int reader_paginate_text(struct reader *reader, const char *text_path,
   const char *cache_dir, const char *title, unsigned *text_count)
{
   FILE *in = NULL;
   FILE *out = NULL;
   char out_path[READER_PATH_MAX];
   char line[READER_TEXT_COLUMNS_MAX + 1u];
   char word[READER_TEXT_COLUMNS_MAX + 1u];
   unsigned columns = reader_text_columns(reader);
   unsigned page_lines = reader_text_lines(reader);
   unsigned line_len = 0;
   unsigned word_len = 0;
   unsigned lines = 0;
   int c;
   int ret = -1;

   in = fopen(text_path, "rb");
   if (!in)
      goto out;

#define READER_OPEN_TEXT_PAGE() do { \
   if (!out) { \
      if (reader_cache_path(out_path, sizeof(out_path), cache_dir, \
          ++(*text_count), ".txt") != 0) \
         goto out; \
      out = fopen(out_path, "wb"); \
      if (!out) \
         goto out; \
   } \
} while (0)

#define READER_WRITE_TEXT_LINE() do { \
   if (line_len) { \
      READER_OPEN_TEXT_PAGE(); \
      line[line_len] = '\0'; \
      fprintf(out, "%s\n", line); \
      line_len = 0; \
      if (++lines >= page_lines) { \
         if (fclose(out) != 0) { out = NULL; goto out; } \
         out = NULL; \
         if (reader_add_extracted_page(reader, out_path, title, 1) != 0) \
            goto out; \
         lines = 0; \
      } \
   } \
} while (0)

   while ((c = fgetc(in)) != EOF) {
      if (c != ' ' && c != '\n' && c != '\r' && c != '\t') {
         if (word_len < columns)
            word[word_len++] = (char)c;
         continue;
      }
      if (word_len) {
         if (line_len && line_len + 1u + word_len > columns)
            READER_WRITE_TEXT_LINE();
         if (line_len)
            line[line_len++] = ' ';
         memcpy(line + line_len, word, word_len);
         line_len += word_len;
         word_len = 0;
      }
      if (c == '\n')
         READER_WRITE_TEXT_LINE();
   }
   if (word_len) {
      if (line_len && line_len + 1u + word_len > columns)
         READER_WRITE_TEXT_LINE();
      if (line_len)
         line[line_len++] = ' ';
      memcpy(line + line_len, word, word_len);
      line_len += word_len;
   }
   READER_WRITE_TEXT_LINE();
   if (out) {
      if (fclose(out) != 0) {
         out = NULL;
         goto out;
      }
      out = NULL;
      if (reader_add_extracted_page(reader, out_path, title, 1) != 0)
         goto out;
   }
   ret = 0;

out:
   if (out)
      fclose(out);
   if (in)
      fclose(in);
#undef READER_WRITE_TEXT_LINE
#undef READER_OPEN_TEXT_PAGE
   return ret;
}

static int reader_extract_text_entry(struct reader *reader,
   const struct unifrog_zip_archive *zip, const struct unifrog_zip_entry *entry,
   const char *cache_dir, const char *title, unsigned *text_count,
   unsigned *image_count);

static int reader_extract_image_entry(struct reader *reader,
   const struct unifrog_zip_archive *zip, const struct unifrog_zip_entry *entry,
   const char *cache_dir, const char *title, unsigned *image_count)
{
   char out_path[READER_PATH_MAX];
   FILE *out;
   int ret;

   if (!entry || !reader_image_path_supported(entry->name) ||
       reader_cache_path(out_path, sizeof(out_path), cache_dir,
       ++(*image_count), reader_ext(entry->name)) != 0)
      return -1;
   out = fopen(out_path, "wb");
   if (!out)
      return -1;
   ret = unifrog_zip_extract_entry_to_file(zip, entry, out);
   if (fclose(out) != 0)
      ret = -1;
   if (ret == 0)
      ret = reader_add_extracted_page(reader, out_path, title, 0);
   if (ret != 0)
      (void)unlink(out_path);
   return ret;
}

static void reader_epub_normalize_path(char *path)
{
   char result[READER_PATH_MAX];
   size_t out = 0;
   char *part = path;

   while (part && *part) {
      char *slash = strchr(part, '/');
      size_t len = slash ? (size_t)(slash - part) : strlen(part);

      if (len == 1u && part[0] == '.') {
         /* Drop current-directory components. */
      } else if (len == 2u && part[0] == '.' && part[1] == '.') {
         while (out && result[out - 1u] == '/')
            out--;
         while (out && result[out - 1u] != '/')
            out--;
      } else if (len && out + len + 1u < sizeof(result)) {
         memcpy(result + out, part, len);
         out += len;
         if (slash)
            result[out++] = '/';
      }
      part = slash ? slash + 1 : NULL;
   }
   result[out] = '\0';
   unifrog_text_copy(path, READER_PATH_MAX, result);
}

static int reader_epub_resolve(char *out, size_t out_size,
   const char *base_name, const char *href)
{
   const char *slash = strrchr(base_name, '/');
   size_t dir_len = slash ? (size_t)(slash - base_name + 1) : 0u;
   size_t href_len = strcspn(href, "#?");

   if (dir_len + href_len + 1u > out_size)
      return -1;
   memcpy(out, base_name, dir_len);
   memcpy(out + dir_len, href, href_len);
   out[dir_len + href_len] = '\0';
   reader_epub_normalize_path(out);
   return 0;
}

static void reader_extract_epub_referenced_images(struct reader *reader,
   const struct unifrog_zip_archive *zip, const char *xhtml,
   const char *chapter_path, const char *cache_dir, const char *title,
   unsigned *image_count)
{
   const char *p = xhtml;

   if (!reader->epub_images)
      return;
   while ((p = strchr(p, '<')) != NULL && reader->page_count < READER_MAX_PAGES) {
      const char *end = strchr(p, '>');
      char href[READER_PATH_MAX];
      char resolved[READER_PATH_MAX];
      const struct unifrog_zip_entry *entry;

      if (!end)
         break;
      if ((strncmp(p, "<img", 4) != 0 && strncmp(p, "<image", 6) != 0) ||
          (reader_xml_attr(p, end, "src", href, sizeof(href)) != 0 &&
           reader_xml_attr(p, end, "href", href, sizeof(href)) != 0) ||
          reader_epub_resolve(resolved, sizeof(resolved), chapter_path,
             href) != 0) {
         p = end + 1;
         continue;
      }
      entry = unifrog_zip_find(zip, resolved);
      if (entry)
         (void)reader_extract_image_entry(reader, zip, entry, cache_dir,
            title, image_count);
      p = end + 1;
   }
}

static int reader_extract_text_entry(struct reader *reader,
   const struct unifrog_zip_archive *zip, const struct unifrog_zip_entry *entry,
   const char *cache_dir, const char *title, unsigned *text_count,
   unsigned *image_count)
{
   char raw_path[READER_PATH_MAX];
   char text_path[READER_PATH_MAX];
   FILE *raw = NULL;
   FILE *in = NULL;
   FILE *out = NULL;
   int ret = -1;

   if (reader_cache_path(raw_path, sizeof(raw_path), cache_dir,
       *text_count + 1u, ".xhtml") != 0 ||
       reader_cache_path(text_path, sizeof(text_path), cache_dir,
       *text_count + 1u, ".flow") != 0)
      return -1;
   raw = fopen(raw_path, "wb");
   if (!raw)
      goto out;
   if (unifrog_zip_extract_entry_to_file(zip, entry, raw) != 0 ||
       fclose(raw) != 0) {
      raw = NULL;
      goto out;
   }
   raw = NULL;
   in = fopen(raw_path, "rb");
   out = fopen(text_path, "wb");
   if (!in || !out || reader_html_to_text(in, out) != 0)
      goto out;
   fclose(in);
   in = NULL;
   if (fclose(out) != 0) {
      out = NULL;
      goto out;
   }
   out = NULL;
   ret = reader_paginate_text(reader, text_path, cache_dir, title, text_count);
   if (ret == 0) {
      char *xhtml = reader_zip_entry_text(zip, entry, cache_dir);

      if (xhtml) {
         reader_extract_epub_referenced_images(reader, zip, xhtml, entry->name,
            cache_dir, title, image_count);
         free(xhtml);
      }
   }

out:
   if (raw)
      fclose(raw);
   if (in)
      fclose(in);
   if (out)
      fclose(out);
   (void)unlink(raw_path);
   (void)unlink(text_path);
   return ret;
}

static char *reader_zip_entry_text(const struct unifrog_zip_archive *zip,
   const struct unifrog_zip_entry *entry, const char *cache_dir)
{
   char path[READER_PATH_MAX];
   FILE *file;
   char *data = NULL;
   long size;

   if (!zip || !entry || !cache_dir ||
       entry->uncompressed_size > READER_EPUB_XML_MAX ||
       reader_join(path, sizeof(path), cache_dir, "package.opf") != 0)
      return NULL;
   file = fopen(path, "w+b");
   if (!file)
      return NULL;
   if (unifrog_zip_extract_entry_to_file(zip, entry, file) != 0 ||
       fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) < 0 ||
       size > (long)READER_EPUB_XML_MAX || fseek(file, 0, SEEK_SET) != 0)
      goto out;
   data = malloc((size_t)size + 1u);
   if (!data || fread(data, 1, (size_t)size, file) != (size_t)size) {
      free(data);
      data = NULL;
      goto out;
   }
   data[size] = '\0';
out:
   fclose(file);
   (void)unlink(path);
   return data;
}

static int reader_xml_attr(const char *tag, const char *tag_end,
   const char *name, char *out, size_t out_size)
{
   size_t name_len = strlen(name);
   const char *p = tag;

   while (p && p < tag_end && (p = strstr(p, name)) != NULL && p < tag_end) {
      const char *value;
      char quote;
      size_t len;

      if (p > tag && ((p[-1] >= 'a' && p[-1] <= 'z') ||
          (p[-1] >= 'A' && p[-1] <= 'Z') || p[-1] == '-' || p[-1] == '_')) {
         p += name_len;
         continue;
      }
      value = p + name_len;
      while (value < tag_end && (*value == ' ' || *value == '\t'))
         value++;
      if (value >= tag_end || *value++ != '=')
         return -1;
      while (value < tag_end && (*value == ' ' || *value == '\t'))
         value++;
      if (value >= tag_end || (*value != '"' && *value != '\''))
         return -1;
      quote = *value++;
      p = value;
      while (p < tag_end && *p != quote)
         p++;
      if (p >= tag_end)
         return -1;
      len = (size_t)(p - value);
      if (len >= out_size)
         len = out_size - 1u;
      memcpy(out, value, len);
      out[len] = '\0';
      return 0;
   }
   return -1;
}

static int reader_collect_epub_spine(struct reader *reader,
   const struct unifrog_zip_archive *zip,
   const struct unifrog_zip_entry *entries, size_t count,
   const char *cache_dir, unsigned *text_count, unsigned *image_count)
{
   struct reader_epub_item *items = NULL;
   const struct unifrog_zip_entry *opf_entry = NULL;
   char *opf = NULL;
   unsigned item_count = 0;
   unsigned chapter_count = 0;

   for (size_t i = 0; i < count; i++) {
      if (reader_has_ext(entries[i].name, ".opf")) {
         opf_entry = &entries[i];
         break;
      }
   }
   if (!opf_entry ||
       !(opf = reader_zip_entry_text(zip, opf_entry, cache_dir)))
      goto out;
   items = calloc(READER_EPUB_ITEMS_MAX, sizeof(*items));
   if (!items)
      goto out;

   for (const char *p = opf; item_count < READER_EPUB_ITEMS_MAX &&
        (p = strstr(p, "<item")) != NULL; p++) {
      const char *end = strchr(p, '>');
      char href[READER_PATH_MAX];

      if (!end)
         break;
      if (p[5] == 'r')
         continue;
      if (reader_xml_attr(p, end, "id", items[item_count].id,
          sizeof(items[item_count].id)) != 0 ||
          reader_xml_attr(p, end, "href", href, sizeof(href)) != 0 ||
          reader_epub_resolve(items[item_count].path,
          sizeof(items[item_count].path), opf_entry->name, href) != 0)
         continue;
      items[item_count].text =
         reader_text_path_supported(items[item_count].path);
      item_count++;
   }

   for (const char *p = opf; (p = strstr(p, "<itemref")) != NULL; p++) {
      const char *end = strchr(p, '>');
      char idref[96];

      if (!end)
         break;
      if (reader_xml_attr(p, end, "idref", idref, sizeof(idref)) != 0)
         continue;
      for (unsigned i = 0; i < item_count; i++) {
         const struct unifrog_zip_entry *entry;

         if (strcmp(items[i].id, idref) != 0)
            continue;
         entry = unifrog_zip_find(zip, items[i].path);
         if (entry && items[i].text &&
             reader_extract_text_entry(reader, zip, entry, cache_dir,
             reader_basename(items[i].path), text_count, image_count) == 0)
            chapter_count++;
         else if (entry && reader->epub_images &&
                  reader_image_path_supported(items[i].path) &&
                  reader_extract_image_entry(reader, zip, entry, cache_dir,
                  reader_basename(items[i].path), image_count) == 0)
            chapter_count++;
         break;
      }
   }

out:
   unifrog_log("reader epub package opf=%s manifest=%u spine_chapters=%u\n",
      opf_entry ? opf_entry->name : "", item_count, chapter_count);
   free(items);
   free(opf);
   return chapter_count ? 0 : -1;
}

static int reader_collect_archive(struct reader *reader, const char *path)
{
   struct unifrog_zip_archive zip;
   const struct unifrog_zip_entry *entries;
   size_t count;
   char cache_dir[READER_PATH_MAX];
   int epub = reader_has_ext(path, ".epub");
   unsigned text_count = 0;
   unsigned image_count = 0;
   int ret = -1;

   snprintf(cache_dir, sizeof(cache_dir), "%s/reader", UNIFROG_CACHE_ROOT);
   (void)reader_remove_tree(cache_dir);
   if (reader_mkdir_p(cache_dir) != 0)
      return -1;
   if (unifrog_zip_open_path(path, &zip) != 0)
      return -1;
   entries = unifrog_zip_entries(&zip);
   count = unifrog_zip_entry_count(&zip);
   if (epub) {
      /*
       * Reflowable EPUB content is XHTML. Treat it as the primary book rather
       * than showing every decorative image in the package. ZIP central
       * directory order is retained because common EPUB generators emit spine
       * documents in reading order; metadata and navigation documents are
       * skipped explicitly.
       */
      (void)reader_collect_epub_spine(reader, &zip, entries, count, cache_dir,
         &text_count, &image_count);
      for (size_t i = 0; reader->page_count == 0 && i < count &&
           reader->page_count < READER_MAX_PAGES; i++) {
         const char *name = entries[i].name;

         if (!reader_text_path_supported(name) ||
             strstr(name, "META-INF/") != NULL ||
             reader_has_ext(name, ".ncx") ||
             strstr(name, "/nav.") != NULL)
            continue;
         (void)reader_extract_text_entry(reader, &zip, &entries[i], cache_dir,
            reader_basename(name), &text_count, &image_count);
      }
   }
   if (!epub || reader->page_count == 0) {
      for (size_t i = 0; i < count && reader->page_count < READER_MAX_PAGES; i++) {
         char out_path[READER_PATH_MAX];
         FILE *out_file;
         int extract_ret;

         if (!reader_image_path_supported(entries[i].name))
            continue;
         if (reader_cache_path(out_path, sizeof(out_path), cache_dir,
             reader->page_count + 1u, reader_ext(entries[i].name)) != 0)
            continue;
         out_file = fopen(out_path, "wb");
         extract_ret = out_file ?
            unifrog_zip_extract_entry_to_file(&zip, &entries[i], out_file) : -1;
         if (out_file && fclose(out_file) != 0)
            extract_ret = -1;
         if (extract_ret == 0)
            (void)reader_add_extracted_page(reader, out_path,
               reader_basename(entries[i].name), 0);
         else
            (void)unlink(out_path);
      }
      qsort(reader->pages, reader->page_count, sizeof(reader->pages[0]),
         reader_page_compare);
   }
   unifrog_log("reader archive kind=%s entries=%u pages=%u text_pages=%u "
      "referenced_images=%u path=%s\n", epub ? "epub" : "cbz",
      (unsigned)count, reader->page_count, text_count, image_count, path);
   ret = reader->page_count ? 0 : -1;
   unifrog_zip_close(&zip);
   return ret;
}

static void reader_unload(struct reader *reader)
{
   if (reader->image_loaded && !reader->image_cached)
      unifrog_image_free(&reader->image);
   memset(&reader->image, 0, sizeof(reader->image));
   reader->image_loaded = 0;
   reader->image_cached = 0;
}

static void reader_clear_svg_cache(struct reader *reader)
{
   if (reader->svg_cache.pixels)
      unifrog_image_free(&reader->svg_cache);
   memset(&reader->svg_cache, 0, sizeof(reader->svg_cache));
   reader->svg_cache_path[0] = '\0';
}

static int reader_load(struct reader *reader)
{
   uint32_t start = unifrog_perf_time_ms();
   int ret;

   reader_unload(reader);
   if (reader->page >= reader->page_count)
      return -1;
   if (reader->pages[reader->page].text) {
      reader->zoom_index = 0;
      reader->pan_x = 0;
      reader->pan_y = 0;
      reader->dirty = 1;
      return 0;
   }
   if (reader_has_ext(reader->pages[reader->page].path, ".svg") &&
       strcmp(reader->svg_cache_path, reader->pages[reader->page].path) == 0 &&
       reader->svg_cache.pixels) {
      reader->image = reader->svg_cache;
      reader->image_loaded = 1;
      reader->image_cached = 1;
      reader->zoom_index = 0;
      reader->pan_x = 0;
      reader->pan_y = 0;
      reader->dirty = 1;
      unifrog_log("reader load ret=0 backend=svg_memory_cache page=%u count=%u "
         "size=%ux%u ms=%u path=%s\n", reader->page + 1u,
         reader->page_count, reader->image.width, reader->image.height,
         (unsigned)(unifrog_perf_time_ms() - start),
         reader->pages[reader->page].path);
      return 0;
   }
   ret = unifrog_image_load_file(reader->pages[reader->page].path,
      &reader->image);
   reader->image_loaded = ret == 0;
   if (ret == 0 && reader_has_ext(reader->pages[reader->page].path, ".svg")) {
      reader_clear_svg_cache(reader);
      reader->svg_cache = reader->image;
      unifrog_text_copy(reader->svg_cache_path, sizeof(reader->svg_cache_path),
         reader->pages[reader->page].path);
      reader->image_cached = 1;
   }
   reader->zoom_index = 0;
   reader->pan_x = 0;
   reader->pan_y = 0;
   reader->dirty = 1;
   unifrog_log("reader load ret=%d page=%u count=%u size=%ux%u ms=%u path=%s\n",
      ret, reader->page + 1u, reader->page_count, reader->image.width,
      reader->image.height, (unsigned)(unifrog_perf_time_ms() - start),
      reader->pages[reader->page].path);
   return ret;
}

static void reader_draw_scaled_text(const struct reader *reader,
   const struct unifrog_surface *surface, int x, int y, const char *text,
   uint16_t color, uint16_t background)
{
   struct unifrog_surface scratch;
   uint16_t *pixels;
   unsigned src_h = reader->font_bitmap ? 8u :
      (unsigned)unifrog_gfx_font_height();
   unsigned dst_h;
   unsigned dst_w;

   if (reader->text_percent == 100) {
      if (reader->font_bitmap)
         unifrog_gfx_draw_text_bitmap(surface, x, y, text, color, 1);
      else
         unifrog_gfx_draw_text(surface, x, y, text, color, 1);
      return;
   }

   pixels = malloc(320u * src_h * sizeof(*pixels));
   if (!pixels)
      return;
   for (unsigned i = 0; i < 320u * src_h; i++)
      pixels[i] = background;
   scratch.pixels = pixels;
   scratch.width = 320u;
   scratch.height = src_h;
   scratch.stride = 320u;
   if (reader->font_bitmap)
      unifrog_gfx_draw_text_bitmap(&scratch, 0, 0, text, color, 1);
   else
      unifrog_gfx_draw_text(&scratch, 0, 0, text, color, 1);

   dst_w = (320u * (unsigned)reader->text_percent + 99u) / 100u;
   dst_h = (src_h * (unsigned)reader->text_percent + 99u) / 100u;
   for (unsigned dy = 0; dy < dst_h && y + (int)dy < (int)surface->height;
        dy++) {
      unsigned sy = dy * 100u / (unsigned)reader->text_percent;
      uint16_t *dst;

      if (y + (int)dy < 0)
         continue;
      dst = surface->pixels + (unsigned)(y + (int)dy) * surface->stride;
      for (unsigned dx = 0; dx < dst_w && x + (int)dx < (int)surface->width;
           dx++) {
         unsigned sx = dx * 100u / (unsigned)reader->text_percent;

         if (x + (int)dx >= 0)
            dst[x + (int)dx] = pixels[sy * 320u + sx];
      }
   }
   free(pixels);
}

static void reader_draw_text_page(struct reader *reader,
   const struct unifrog_surface *surface)
{
   FILE *file = fopen(reader->pages[reader->page].path, "rb");
   char line[96];
   int y = reader->chrome ? READER_HEADER_H + 6 : 6;
   int max_y = reader->chrome ? 240 - READER_FOOTER_H - 10 : 230;
   int line_h = reader->font_bitmap ? 10 : unifrog_gfx_font_height() + 2;
   uint16_t color = reader->palette == 1 ? UNIFROG_RGB565(55, 42, 28) :
      reader->palette == 2 ? UNIFROG_RGB565(30, 30, 30) :
      UNIFROG_RGB565(235, 235, 225);
   uint16_t background = reader->palette == 1 ?
      UNIFROG_RGB565(239, 224, 190) : reader->palette == 2 ?
      UNIFROG_RGB565(245, 245, 240) : UNIFROG_RGB565(0, 0, 0);

   if (!file) {
      unifrog_gfx_draw_text(surface, 18, 104, "Text page missing",
         UNIFROG_RGB565(255, 120, 120), 1);
      return;
   }
   while (y < max_y && fgets(line, sizeof(line), file)) {
      char *p = line;

      while (*p == ' ')
         p++;
      line[strcspn(line, "\r\n")] = '\0';
      if (!p[0])
         continue;
      reader_draw_scaled_text(reader, surface, reader->margin, y, p, color,
         background);
      y += (line_h * reader->text_percent + 99) / 100;
   }
   fclose(file);
}

static void reader_view_size(const struct reader *reader, int *w, int *h)
{
   *w = 320;
   *h = reader->chrome ? 240 - READER_HEADER_H - READER_FOOTER_H : 240;
}

static void reader_source_rect(struct reader *reader,
   struct unifrog_ge_rect *src, struct unifrog_ge_rect *dst)
{
   int view_w;
   int view_h;
   unsigned zoom = reader_zoom_percent[reader->zoom_index];

   reader_view_size(reader, &view_w, &view_h);
   if (zoom == 0) {
      unsigned sx = (unsigned)view_w * 1000u / reader->image.width;
      unsigned sy = (unsigned)view_h * 1000u / reader->image.height;
      unsigned fit = sx < sy ? sx : sy;

      dst->w = (int)(reader->image.width * fit / 1000u);
      dst->h = (int)(reader->image.height * fit / 1000u);
      if (dst->w < 1)
         dst->w = 1;
      if (dst->h < 1)
         dst->h = 1;
      src->x = 0;
      src->y = 0;
      src->w = (int)reader->image.width;
      src->h = (int)reader->image.height;
   } else {
      src->w = (view_w * 100 + (int)zoom - 1) / (int)zoom;
      src->h = (view_h * 100 + (int)zoom - 1) / (int)zoom;
      if (src->w > (int)reader->image.width)
         src->w = (int)reader->image.width;
      if (src->h > (int)reader->image.height)
         src->h = (int)reader->image.height;
      if (reader->pan_x > (int)reader->image.width - src->w)
         reader->pan_x = (int)reader->image.width - src->w;
      if (reader->pan_y > (int)reader->image.height - src->h)
         reader->pan_y = (int)reader->image.height - src->h;
      if (reader->pan_x < 0)
         reader->pan_x = 0;
      if (reader->pan_y < 0)
         reader->pan_y = 0;
      src->x = reader->pan_x;
      src->y = reader->pan_y;
      dst->w = src->w * (int)zoom / 100;
      dst->h = src->h * (int)zoom / 100;
      if (dst->w > view_w)
         dst->w = view_w;
      if (dst->h > view_h)
         dst->h = view_h;
   }
   dst->x = (view_w - dst->w) / 2;
   dst->y = (reader->chrome ? READER_HEADER_H : 0) + (view_h - dst->h) / 2;
}

static void reader_draw(struct reader *reader)
{
   struct unifrog_surface surface;
   char status[64];
   uint16_t background = reader->palette == 1 ? UNIFROG_RGB565(239, 224, 190) :
      reader->palette == 2 ? UNIFROG_RGB565(245, 245, 240) :
      UNIFROG_RGB565(0, 0, 0);

   unifrog_ui_begin(&reader->ui, background);
   surface = unifrog_ui_surface(&reader->ui);
   if (reader->pages[reader->page].text) {
      reader_draw_text_page(reader, &surface);
   } else if (reader->image_loaded) {
      struct unifrog_ge_rect src;
      struct unifrog_ge_rect dst;

      reader_source_rect(reader, &src, &dst);
      if (reader->ui.ge_ready) {
         struct unifrog_ge_surface ge_dst =
            unifrog_fb_ge_surface_for_buffer(&reader->ui.fb,
               reader->ui.draw_buffer);
         struct unifrog_ge_surface ge_src = {
            reader->image.pixels, reader->image.width, reader->image.height,
            reader->image.width * sizeof(uint16_t), UNIFROG_GE_FORMAT_RGB565,
         };

         (void)unifrog_ge_stretch(&reader->ui.ge, &ge_dst, &dst, &ge_src,
            &src, UNIFROG_GE_FLUSH_SOURCE);
      } else {
         unifrog_png_draw(&surface, &reader->image, dst.x, dst.y, dst.w, dst.h);
      }
   } else {
      unifrog_gfx_draw_text(&surface, 18, 104, "Image decode failed",
         UNIFROG_RGB565(255, 120, 120), 1);
   }
   if (reader->chrome) {
      unifrog_gfx_fill_rect(&surface, 0, 0, 320, READER_HEADER_H,
         UNIFROG_RGB565(20, 22, 26));
      unifrog_gfx_fill_rect(&surface, 0, 240 - READER_FOOTER_H, 320,
         READER_FOOTER_H, UNIFROG_RGB565(20, 22, 26));
      unifrog_ui_text_clipped(&reader->ui, 6, 6, 40,
         reader->pages[reader->page].title[0] ?
            reader->pages[reader->page].title :
            reader_basename(reader->pages[reader->page].path),
         UNIFROG_RGB565(240, 240, 232), 1);
      if (reader->pages[reader->page].text)
         snprintf(status, sizeof(status), "%u/%u  reflow",
            reader->page + 1u, reader->page_count);
      else
         snprintf(status, sizeof(status), "%u/%u  zoom:%s",
            reader->page + 1u, reader->page_count,
            reader->zoom_index ? "manual" : "fit");
      unifrog_ui_text_clipped(&reader->ui, 6, 228, 30, status,
         UNIFROG_RGB565(180, 190, 190), 1);
      unifrog_ui_text_clipped(&reader->ui, 196, 228, 20,
         reader->pages[reader->page].text ?
            "L/R page Y settings" : "L/R page Y settings",
         UNIFROG_RGB565(180, 190, 190), 1);
   }
   if (reader->menu) {
      static const char *const labels[] = {
         "Font", "Text Size", "Margins", "Colors", "EPUB Images",
      };
      char values[5][24];

      snprintf(values[0], sizeof(values[0]), "%s",
         reader->font_bitmap ? "bitmap" : "theme");
      snprintf(values[1], sizeof(values[1]), "%d%%", reader->text_percent);
      snprintf(values[2], sizeof(values[2]), "%s",
         reader->margin == 4 ? "narrow" : reader->margin == 16 ? "wide" :
         "normal");
      snprintf(values[3], sizeof(values[3]), "%s",
         reader->palette == 1 ? "sepia" : reader->palette == 2 ? "light" :
         "dark");
      snprintf(values[4], sizeof(values[4]), "%s",
         reader->epub_images ? "shown" : "hidden");
      unifrog_gfx_fill_rect(&surface, 28, 28, 264, 184,
         UNIFROG_RGB565(22, 24, 28));
      unifrog_gfx_draw_text_bitmap(&surface, 42, 40, "Reader Settings",
         UNIFROG_RGB565(245, 245, 235), 1);
      for (int i = 0; i < 5; i++) {
         int y = 66 + i * 24;

         if (i == reader->menu_selected)
            unifrog_gfx_fill_rect(&surface, 36, y - 5, 248, 20,
               UNIFROG_RGB565(55, 72, 82));
         unifrog_gfx_draw_text_bitmap(&surface, 44, y, labels[i],
            UNIFROG_RGB565(235, 235, 225), 1);
         unifrog_gfx_draw_text_bitmap(&surface, 190, y, values[i],
            UNIFROG_RGB565(180, 215, 205), 1);
      }
      unifrog_gfx_draw_text_bitmap(&surface, 42, 194,
         "Left/Right change  Y/B close", UNIFROG_RGB565(175, 180, 180), 1);
   }
   unifrog_ui_present(&reader->ui);
   reader->dirty = 0;
}

static void reader_change_page(struct reader *reader, int delta)
{
   if (!reader->page_count)
      return;
   if (delta < 0)
      reader->page = reader->page ? reader->page - 1u :
         reader->page_count - 1u;
   else
      reader->page = reader->page + 1u < reader->page_count ?
         reader->page + 1u : 0u;
   (void)reader_load(reader);
}

static void reader_change_chapter(struct reader *reader, int delta)
{
   unsigned page = reader->page;
   const char *title = reader->pages[reader->page].title;

   if (delta > 0) {
      while (page + 1u < reader->page_count &&
             strcmp(reader->pages[page + 1u].title, title) == 0)
         page++;
      if (page + 1u < reader->page_count)
         reader->page = page + 1u;
   } else {
      while (page > 0 && strcmp(reader->pages[page - 1u].title, title) == 0)
         page--;
      if (page > 0) {
         title = reader->pages[page - 1u].title;
         page--;
         while (page > 0 &&
                strcmp(reader->pages[page - 1u].title, title) == 0)
            page--;
         reader->page = page;
      }
   }
   (void)reader_load(reader);
}

static void reader_settings_change(struct reader *reader, int delta)
{
   switch (reader->menu_selected) {
   case 0:
      reader->font_bitmap = !reader->font_bitmap;
      reader->rebuild = 1;
      break;
   case 1:
      if (delta > 0)
         reader->text_percent = reader->text_percent == 75 ? 90 :
            reader->text_percent == 90 ? 100 :
            reader->text_percent == 100 ? 125 :
            reader->text_percent == 125 ? 150 : 75;
      else
         reader->text_percent = reader->text_percent == 150 ? 125 :
            reader->text_percent == 125 ? 100 :
            reader->text_percent == 100 ? 90 :
            reader->text_percent == 90 ? 75 : 150;
      reader->rebuild = 1;
      break;
   case 2:
      if (delta > 0)
         reader->margin = reader->margin == 4 ? 10 :
            reader->margin == 10 ? 16 : 4;
      else
         reader->margin = reader->margin == 16 ? 10 :
            reader->margin == 10 ? 4 : 16;
      reader->rebuild = 1;
      break;
   case 3:
      reader->palette = (reader->palette + (delta > 0 ? 1 : 2)) % 3;
      break;
   case 4:
      reader->epub_images = !reader->epub_images;
      reader->rebuild = 1;
      break;
   }
   reader_save_settings(reader);
   reader->dirty = 1;
}

static void reader_handle_input(struct reader *reader, int *running)
{
   int move = reader->zoom_index ? 18 : 0;

   unifrog_ui_poll(&reader->ui);
   if (reader->menu) {
      if (unifrog_ui_pressed(&reader->ui, UNIFROG_UI_UP)) {
         reader->menu_selected = reader->menu_selected ?
            reader->menu_selected - 1 : 4;
         reader->dirty = 1;
      }
      if (unifrog_ui_pressed(&reader->ui, UNIFROG_UI_DOWN)) {
         reader->menu_selected = (reader->menu_selected + 1) % 5;
         reader->dirty = 1;
      }
      if (unifrog_ui_pressed(&reader->ui, UNIFROG_UI_LEFT))
         reader_settings_change(reader, -1);
      if (unifrog_ui_pressed(&reader->ui, UNIFROG_UI_RIGHT) ||
          unifrog_ui_pressed(&reader->ui, UNIFROG_UI_A))
         reader_settings_change(reader, 1);
      if (unifrog_ui_pressed(&reader->ui, UNIFROG_UI_Y) ||
          unifrog_ui_pressed(&reader->ui, UNIFROG_UI_B)) {
         reader->menu = 0;
         reader->dirty = 1;
      }
      return;
   }
   if (unifrog_ui_pressed(&reader->ui, UNIFROG_UI_Y)) {
      reader->menu = 1;
      reader->dirty = 1;
      return;
   }
   if (unifrog_ui_pressed(&reader->ui, UNIFROG_UI_B))
      *running = 0;
   if (unifrog_ui_pressed(&reader->ui, UNIFROG_UI_L))
      reader_change_page(reader, -1);
   if (unifrog_ui_pressed(&reader->ui, UNIFROG_UI_R))
      reader_change_page(reader, 1);
   if (unifrog_ui_pressed(&reader->ui, UNIFROG_UI_A)) {
      if (reader->pages[reader->page].text)
         reader_change_chapter(reader, 1);
      else if (reader->zoom_index + 1 < (int)(sizeof(reader_zoom_percent) /
          sizeof(reader_zoom_percent[0])))
         reader->zoom_index++;
      reader->dirty = 1;
   }
   if (unifrog_ui_pressed(&reader->ui, UNIFROG_UI_X)) {
      if (reader->pages[reader->page].text)
         reader_change_chapter(reader, -1);
      else if (reader->zoom_index > 0)
         reader->zoom_index--;
      reader->dirty = 1;
   }
   if (unifrog_ui_pressed(&reader->ui, UNIFROG_UI_SELECT)) {
      reader->zoom_index = 0;
      reader->pan_x = 0;
      reader->pan_y = 0;
      reader->dirty = 1;
   }
   if (unifrog_ui_pressed(&reader->ui, UNIFROG_UI_START)) {
      reader->chrome = !reader->chrome;
      reader->dirty = 1;
   }
   if (move && unifrog_ui_down(&reader->ui, UNIFROG_UI_LEFT)) {
      reader->pan_x -= move;
      reader->dirty = 1;
   }
   if (move && unifrog_ui_down(&reader->ui, UNIFROG_UI_RIGHT)) {
      reader->pan_x += move;
      reader->dirty = 1;
   }
   if (move && unifrog_ui_down(&reader->ui, UNIFROG_UI_UP)) {
      reader->pan_y -= move;
      reader->dirty = 1;
   }
   if (move && unifrog_ui_down(&reader->ui, UNIFROG_UI_DOWN)) {
      reader->pan_y += move;
      reader->dirty = 1;
   }
}

static int reader_rebuild_epub(struct reader *reader)
{
   unsigned old_page = reader->page;

   if (!reader->epub)
      return 0;
   reader_unload(reader);
   reader_clear_svg_cache(reader);
   memset(reader->pages, 0, READER_MAX_PAGES * sizeof(reader->pages[0]));
   reader->page = 0;
   reader->page_count = 0;
   if (reader_collect_archive(reader, reader->source_path) != 0)
      return -1;
   if (old_page < reader->page_count)
      reader->page = old_page;
   reader->rebuild = 0;
   return reader_load(reader);
}

int unifrog_reader_run(const char *path)
{
   struct reader reader;
   int running = 1;
   int ret = -1;

   if (!path || !path[0])
      return -1;
   memset(&reader, 0, sizeof(reader));
   reader.chrome = 1;
   reader.dirty = 1;
   reader.epub = reader_has_ext(path, ".epub");
   unifrog_text_copy(reader.source_path, sizeof(reader.source_path), path);
   reader_load_settings(&reader);
   reader.pages = calloc(READER_MAX_PAGES, sizeof(reader.pages[0]));
   if (!reader.pages)
      goto out;
   if (reader_has_ext(path, ".cbz") || reader_has_ext(path, ".epub")) {
      if (reader_collect_archive(&reader, path) != 0)
         goto out;
   } else if (reader_collect_directory(&reader, path) != 0)
      goto out;
   if (unifrog_ui_open(&reader.ui, 0) != 0)
      goto out;
   (void)reader_load(&reader);
   while (running) {
      reader_handle_input(&reader, &running);
      if (reader.rebuild && !reader.menu)
         (void)reader_rebuild_epub(&reader);
      if (reader.dirty)
         reader_draw(&reader);
      else
         unifrog_perf_delay_us(16000u);
   }
   ret = 0;

out:
   reader_unload(&reader);
   reader_clear_svg_cache(&reader);
   if (reader.ui.fb.pixels)
      unifrog_ui_close(&reader.ui);
   free(reader.pages);
   return ret;
}
