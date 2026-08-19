#include <unifrog/png.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <malloc.h>

#include <unifrog/perf.h>
#include <unifrog/surface_alloc.h>
#include <unifrog/zlib_port.h>

#define PNG_MAX_FILE_BYTES (4u * 1024u * 1024u)
#define PNG_MAX_PIXELS (640u * 480u)
#define PNG_PIXEL_ALIGNMENT 32u

static uint32_t be32(const uint8_t *p)
{
   return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
      ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static int read_file(const char *path, uint8_t **out, size_t *out_size)
{
   FILE *file;
   long size;
   uint8_t *data;

   if (!path || !out || !out_size)
      return -1;
   file = fopen(path, "rb");
   if (!file)
      return -1;
   if (fseek(file, 0, SEEK_END) != 0) {
      fclose(file);
      return -1;
   }
   size = ftell(file);
   if (size <= 0 || size > (long)PNG_MAX_FILE_BYTES) {
      fclose(file);
      return -1;
   }
   if (fseek(file, 0, SEEK_SET) != 0) {
      fclose(file);
      return -1;
   }
   data = malloc((size_t)size);
   if (!data) {
      fclose(file);
      return -1;
   }
   if (fread(data, 1, (size_t)size, file) != (size_t)size) {
      free(data);
      fclose(file);
      return -1;
   }
   fclose(file);
   *out = data;
   *out_size = (size_t)size;
   return 0;
}

static int append_idat(uint8_t **data, size_t *size, const uint8_t *chunk,
   size_t chunk_size)
{
   uint8_t *next;

   if (chunk_size == 0)
      return 0;
   next = realloc(*data, *size + chunk_size);
   if (!next)
      return -1;
   memcpy(next + *size, chunk, chunk_size);
   *data = next;
   *size += chunk_size;
   return 0;
}

static uint8_t paeth(uint8_t a, uint8_t b, uint8_t c)
{
   int p = (int)a + (int)b - (int)c;
   int pa = abs(p - (int)a);
   int pb = abs(p - (int)b);
   int pc = abs(p - (int)c);

   if (pa <= pb && pa <= pc)
      return a;
   if (pb <= pc)
      return b;
   return c;
}

static int unfilter_png(uint8_t *dst, const uint8_t *src,
   size_t row_bytes, unsigned height, unsigned filter_bpp)
{
   const uint8_t *in = src;

   for (unsigned y = 0; y < height; y++) {
      unsigned filter = *in++;
      uint8_t *row = dst + (size_t)y * row_bytes;
      const uint8_t *prev = y ? row - row_bytes : NULL;

      memcpy(row, in, row_bytes);
      in += row_bytes;
      for (size_t x = 0; x < row_bytes; x++) {
         uint8_t left = x >= filter_bpp ? row[x - filter_bpp] : 0;
         uint8_t up = prev ? prev[x] : 0;
         uint8_t up_left = prev && x >= filter_bpp ?
            prev[x - filter_bpp] : 0;

         if (filter == 1)
            row[x] = (uint8_t)(row[x] + left);
         else if (filter == 2)
            row[x] = (uint8_t)(row[x] + up);
         else if (filter == 3)
            row[x] = (uint8_t)(row[x] + (uint8_t)(((unsigned)left + up) >> 1));
         else if (filter == 4)
            row[x] = (uint8_t)(row[x] + paeth(left, up, up_left));
         else if (filter != 0)
            return -1;
      }
   }
   return 0;
}

static unsigned png_channels(unsigned color_type)
{
   if (color_type == 0 || color_type == 3)
      return 1;
   if (color_type == 2)
      return 3;
   if (color_type == 4)
      return 2;
   if (color_type == 6)
      return 4;
   return 0;
}

static unsigned png_filter_bpp(unsigned color_type, unsigned bit_depth)
{
   unsigned channels = png_channels(color_type);
   unsigned bits = channels * bit_depth;

   return bits <= 8 ? 1u : (bits + 7u) / 8u;
}

static size_t png_row_bytes(unsigned width, unsigned color_type,
   unsigned bit_depth)
{
   unsigned channels = png_channels(color_type);

   return ((size_t)width * channels * bit_depth + 7u) / 8u;
}

static unsigned packed_sample(const uint8_t *row, unsigned x,
   unsigned bit_depth)
{
   if (bit_depth == 8)
      return row[x];
   {
      unsigned bit = x * bit_depth;
      unsigned shift = 8u - bit_depth - (bit & 7u);
      unsigned mask = (1u << bit_depth) - 1u;

      return (row[bit >> 3] >> shift) & mask;
   }
}

static uint8_t scale_sample(unsigned sample, unsigned bit_depth)
{
   if (bit_depth == 8)
      return (uint8_t)sample;
   return (uint8_t)((sample * 255u + ((1u << bit_depth) - 1u) / 2u) /
      ((1u << bit_depth) - 1u));
}

static void png_store_pixel(struct unifrog_png_image *image, size_t dst,
   const uint8_t *row, unsigned x, unsigned color_type, unsigned bit_depth,
   const uint8_t *palette, unsigned palette_entries, const uint8_t *trns,
   unsigned trns_entries)
{
   uint8_t r = 0;
   uint8_t g = 0;
   uint8_t b = 0;
   uint8_t a = 255;

   if (color_type == 0) {
      unsigned sample = packed_sample(row, x, bit_depth);

      r = g = b = scale_sample(sample, bit_depth);
      if (trns_entries >= 2u && sample == ((unsigned)trns[0] << 8 | trns[1]))
         a = 0;
   } else if (color_type == 2) {
      const uint8_t *p = row + (size_t)x * 3u;

      r = p[0];
      g = p[1];
      b = p[2];
      if (trns_entries >= 6u &&
          p[0] == trns[1] && p[1] == trns[3] && p[2] == trns[5])
         a = 0;
   } else if (color_type == 3) {
      unsigned idx = packed_sample(row, x, bit_depth);

      if (idx < palette_entries) {
         const uint8_t *p = palette + idx * 3u;

         r = p[0];
         g = p[1];
         b = p[2];
         if (idx < trns_entries)
            a = trns[idx];
      }
   } else if (color_type == 4) {
      const uint8_t *p = row + (size_t)x * 2u;

      r = g = b = p[0];
      a = p[1];
   } else if (color_type == 6) {
      const uint8_t *p = row + (size_t)x * 4u;

      r = p[0];
      g = p[1];
      b = p[2];
      a = p[3];
   }
   image->pixels[dst] = UNIFROG_RGB565(r, g, b);
   image->alpha[dst] = a;
}

static int png_decode_pass(struct unifrog_png_image *image,
   const uint8_t *src, size_t *src_pos, unsigned width, unsigned height,
   unsigned color_type, unsigned bit_depth, unsigned x_start, unsigned y_start,
   unsigned x_step, unsigned y_step, const uint8_t *palette,
   unsigned palette_entries, const uint8_t *trns, unsigned trns_entries)
{
   unsigned pass_w;
   unsigned pass_h;
   size_t row_bytes;
   uint8_t *rows;
   const uint8_t *pass_src;

   if (x_start >= width || y_start >= height)
      return 0;
   pass_w = (width - x_start + x_step - 1u) / x_step;
   pass_h = (height - y_start + y_step - 1u) / y_step;
   if (pass_w == 0 || pass_h == 0)
      return 0;
   row_bytes = png_row_bytes(pass_w, color_type, bit_depth);
   if (row_bytes == 0)
      return -1;
   pass_src = src + *src_pos;
   *src_pos += (row_bytes + 1u) * pass_h;
   rows = malloc(row_bytes * pass_h);
   if (!rows)
      return -1;
   if (unfilter_png(rows, pass_src, row_bytes, pass_h,
       png_filter_bpp(color_type, bit_depth)) != 0) {
      free(rows);
      return -1;
   }
   for (unsigned py = 0; py < pass_h; py++) {
      const uint8_t *row = rows + (size_t)py * row_bytes;
      unsigned y = y_start + py * y_step;

      for (unsigned px = 0; px < pass_w; px++) {
         unsigned x = x_start + px * x_step;
         size_t dst = (size_t)y * width + x;

         png_store_pixel(image, dst, row, px, color_type, bit_depth,
            palette, palette_entries, trns, trns_entries);
      }
   }
   free(rows);
   return 0;
}

static size_t png_inflated_size(unsigned width, unsigned height,
   unsigned color_type, unsigned bit_depth, unsigned interlace)
{
   static const unsigned adam7[7][4] = {
      { 0, 0, 8, 8 }, { 4, 0, 8, 8 }, { 0, 4, 4, 8 },
      { 2, 0, 4, 4 }, { 0, 2, 2, 4 }, { 1, 0, 2, 2 },
      { 0, 1, 1, 2 },
   };
   size_t total = 0;

   if (interlace == 0) {
      size_t row_bytes = png_row_bytes(width, color_type, bit_depth);

      return (row_bytes + 1u) * height;
   }
   for (unsigned i = 0; i < 7u; i++) {
      unsigned x0 = adam7[i][0];
      unsigned y0 = adam7[i][1];
      unsigned xs = adam7[i][2];
      unsigned ys = adam7[i][3];
      unsigned pass_w;
      unsigned pass_h;

      if (x0 >= width || y0 >= height)
         continue;
      pass_w = (width - x0 + xs - 1u) / xs;
      pass_h = (height - y0 + ys - 1u) / ys;
      total += (png_row_bytes(pass_w, color_type, bit_depth) + 1u) * pass_h;
   }
   return total;
}

static uint16_t blend_rgb565(uint16_t dst, uint16_t src, unsigned alpha)
{
   unsigned inv = 255u - alpha;
   unsigned sr = (src >> 11) & 0x1fu;
   unsigned sg = (src >> 5) & 0x3fu;
   unsigned sb = src & 0x1fu;
   unsigned dr = (dst >> 11) & 0x1fu;
   unsigned dg = (dst >> 5) & 0x3fu;
   unsigned db = dst & 0x1fu;
   unsigned r = (sr * alpha + dr * inv + 127u) / 255u;
   unsigned g = (sg * alpha + dg * inv + 127u) / 255u;
   unsigned b = (sb * alpha + db * inv + 127u) / 255u;

   return (uint16_t)((r << 11) | (g << 5) | b);
}

int unifrog_png_load_file(const char *path, struct unifrog_png_image *image)
{
   static const uint8_t signature[8] = {
      0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'
   };
   uint8_t *file_data = NULL;
   uint8_t *idat = NULL;
   uint8_t *inflated = NULL;
   size_t file_size = 0;
   size_t idat_size = 0;
   size_t pos = 8;
   unsigned width = 0;
   unsigned height = 0;
   unsigned bit_depth = 0;
   unsigned color_type = 0;
   unsigned interlace = 0;
   uint8_t palette[256 * 3];
   unsigned palette_entries = 0;
   uint8_t trns[256];
   unsigned trns_entries = 0;
   int ret = -1;

   if (!image)
      return -1;
   memset(image, 0, sizeof(*image));
   if (read_file(path, &file_data, &file_size) != 0)
      return -1;
   if (file_size < 33 || memcmp(file_data, signature, sizeof(signature)) != 0)
      goto out;

   while (pos + 12 <= file_size) {
      uint32_t len = be32(file_data + pos);
      const uint8_t *type = file_data + pos + 4;
      const uint8_t *chunk = file_data + pos + 8;

      pos += 8;
      if (len > file_size - pos || len > PNG_MAX_FILE_BYTES)
         goto out;
      if (memcmp(type, "IHDR", 4) == 0) {
         if (len != 13)
            goto out;
         width = be32(chunk);
         height = be32(chunk + 4);
         bit_depth = chunk[8];
         color_type = chunk[9];
         interlace = chunk[12];
         if (width == 0 || height == 0 ||
             (uint64_t)width * height > PNG_MAX_PIXELS ||
             chunk[10] != 0 || chunk[11] != 0 || interlace > 1)
            goto out;
         if (!png_channels(color_type))
            goto out;
         if ((color_type == 0 || color_type == 3) &&
             bit_depth != 1 && bit_depth != 2 && bit_depth != 4 &&
             bit_depth != 8)
            goto out;
         if ((color_type == 2 || color_type == 4 || color_type == 6) &&
             bit_depth != 8)
            goto out;
      } else if (memcmp(type, "IDAT", 4) == 0) {
         if (append_idat(&idat, &idat_size, chunk, len) != 0)
            goto out;
      } else if (memcmp(type, "PLTE", 4) == 0) {
         if (len == 0 || len > sizeof(palette) || (len % 3u) != 0)
            goto out;
         memcpy(palette, chunk, len);
         palette_entries = len / 3u;
      } else if (memcmp(type, "tRNS", 4) == 0) {
         if (len > sizeof(trns))
            len = sizeof(trns);
         memcpy(trns, chunk, len);
         trns_entries = len;
      } else if (memcmp(type, "IEND", 4) == 0) {
         break;
      }
      pos += len + 4;
   }

   if (!width || !height || !png_channels(color_type) || !idat_size ||
       (color_type == 3 && palette_entries == 0))
      goto out;

   {
      uLongf inflated_size = (uLongf)png_inflated_size(width, height,
         color_type, bit_depth, interlace);
      size_t inflated_expected = (size_t)inflated_size;
      size_t src_pos = 0;

      inflated = malloc(inflated_expected);
      image->pixels = unifrog_surface_memalign(PNG_PIXEL_ALIGNMENT,
         (size_t)width * height * sizeof(uint16_t));
      image->alpha = malloc((size_t)width * height);
      if (!inflated || !image->pixels || !image->alpha)
         goto out;
      if (uncompress(inflated, &inflated_size, idat, (uLong)idat_size) != Z_OK ||
          inflated_size != (uLongf)inflated_expected)
         goto out;
      if (interlace == 0) {
         if (png_decode_pass(image, inflated, &src_pos, width, height,
             color_type, bit_depth, 0, 0, 1, 1, palette, palette_entries,
             trns, trns_entries) != 0)
            goto out;
      } else {
         static const unsigned adam7[7][4] = {
            { 0, 0, 8, 8 }, { 4, 0, 8, 8 }, { 0, 4, 4, 8 },
            { 2, 0, 4, 4 }, { 0, 2, 2, 4 }, { 1, 0, 2, 2 },
            { 0, 1, 1, 2 },
         };

         for (unsigned i = 0; i < 7u; i++) {
            if (png_decode_pass(image, inflated, &src_pos, width, height,
                color_type, bit_depth, adam7[i][0], adam7[i][1],
                adam7[i][2], adam7[i][3], palette, palette_entries,
                trns, trns_entries) != 0)
               goto out;
         }
      }
      if (src_pos != inflated_expected)
         goto out;
      image->width = width;
      image->height = height;
      ret = 0;
   }

out:
   if (ret != 0)
      unifrog_png_free(image);
   free(inflated);
   free(idat);
   free(file_data);
   return ret;
}

void unifrog_png_free(struct unifrog_png_image *image)
{
   if (!image)
      return;
   unifrog_surface_free(image->pixels);
   free(image->alpha);
   memset(image, 0, sizeof(*image));
}

int unifrog_png_is_opaque(const struct unifrog_png_image *image)
{
   size_t pixels;

   if (!image || !image->alpha || image->width == 0 || image->height == 0)
      return 0;
   pixels = (size_t)image->width * image->height;
   for (size_t i = 0; i < pixels; i++) {
      if (image->alpha[i] != 255)
         return 0;
   }
   return 1;
}

unsigned unifrog_png_alpha_coverage(const struct unifrog_png_image *image)
{
   size_t pixels;
   size_t covered = 0;

   if (!image || !image->alpha || image->width == 0 || image->height == 0)
      return 0;
   pixels = (size_t)image->width * image->height;
   for (size_t i = 0; i < pixels; i++) {
      if (image->alpha[i])
         covered++;
   }
   return (unsigned)((covered * 100u + pixels / 2u) / pixels);
}

int unifrog_png_draw_ge_async(struct unifrog_ge *ge,
   const struct unifrog_ge_surface *dst, const struct unifrog_png_image *image,
   int x, int y, int w, int h)
{
   struct unifrog_ge_surface src;
   struct unifrog_ge_rect src_rect;
   struct unifrog_ge_rect dst_rect;

   if (!ge || !dst || !image || !image->pixels ||
       image->width == 0 || image->height == 0 || !unifrog_png_is_opaque(image))
      return -1;
   if (w <= 0)
      w = (int)image->width;
   if (h <= 0)
      h = (int)image->height;
   if (w <= 0 || h <= 0)
      return -1;

   memset(&src, 0, sizeof(src));
   src.pixels = image->pixels;
   src.width = image->width;
   src.height = image->height;
   src.pitch_bytes = image->width * sizeof(uint16_t);
   src.format = UNIFROG_GE_FORMAT_RGB565;
   src_rect.x = 0;
   src_rect.y = 0;
   src_rect.w = (int)image->width;
   src_rect.h = (int)image->height;
   dst_rect.x = x;
   dst_rect.y = y;
   dst_rect.w = w;
   dst_rect.h = h;
   return unifrog_ge_stretch(ge, dst, &dst_rect, &src, &src_rect,
      UNIFROG_GE_FLUSH_SOURCE);
}

int unifrog_png_draw_ge(struct unifrog_ge *ge,
   const struct unifrog_ge_surface *dst, const struct unifrog_png_image *image,
   int x, int y, int w, int h)
{
   int ret = unifrog_png_draw_ge_async(ge, dst, image, x, y, w, h);

   if (ret == 0) {
      ret = unifrog_ge_sync(ge);
      unifrog_perf_cache_invalidate(dst->pixels,
         (size_t)dst->pitch_bytes * dst->height);
   }
   return ret;
}

void unifrog_png_draw(const struct unifrog_surface *surface,
   const struct unifrog_png_image *image, int x, int y, int w, int h)
{
   if (!surface || !surface->pixels || !image || !image->pixels ||
       !image->alpha || image->width == 0 || image->height == 0)
      return;
   if (w <= 0)
      w = (int)image->width;
   if (h <= 0)
      h = (int)image->height;
   if (w <= 0 || h <= 0)
      return;

   for (int dy = 0; dy < h; dy++) {
      int py = y + dy;
      unsigned sy;

      if (py < 0 || py >= (int)surface->height)
         continue;
      sy = (unsigned)(((uint64_t)(unsigned)dy * image->height) / (unsigned)h);
      if (sy >= image->height)
         sy = image->height - 1;
      for (int dx = 0; dx < w; dx++) {
         int px = x + dx;
         unsigned sx;
         size_t idx;
         unsigned alpha;

         if (px < 0 || px >= (int)surface->width)
            continue;
         sx = (unsigned)(((uint64_t)(unsigned)dx * image->width) / (unsigned)w);
         if (sx >= image->width)
            sx = image->width - 1;
         idx = (size_t)sy * image->width + sx;
         alpha = image->alpha[idx];
         if (alpha == 0)
            continue;
         if (alpha >= 255)
            surface->pixels[(size_t)py * surface->stride + px] =
               image->pixels[idx];
         else {
            uint16_t *dst = &surface->pixels[(size_t)py * surface->stride + px];

            *dst = blend_rgb565(*dst, image->pixels[idx], alpha);
         }
      }
   }
}
