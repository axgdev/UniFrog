#include <unifrog/png.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <kernel/lib/zlib.h>

#define PNG_MAX_FILE_BYTES (256u * 1024u)
#define PNG_MAX_PIXELS (128u * 128u)

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
   unsigned width, unsigned height, unsigned channels)
{
   size_t row_bytes = (size_t)width * channels;
   const uint8_t *in = src;

   for (unsigned y = 0; y < height; y++) {
      unsigned filter = *in++;
      uint8_t *row = dst + (size_t)y * row_bytes;
      const uint8_t *prev = y ? row - row_bytes : NULL;

      memcpy(row, in, row_bytes);
      in += row_bytes;
      for (size_t x = 0; x < row_bytes; x++) {
         uint8_t left = x >= channels ? row[x - channels] : 0;
         uint8_t up = prev ? prev[x] : 0;
         uint8_t up_left = prev && x >= channels ? prev[x - channels] : 0;

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
   uint8_t *rows = NULL;
   size_t file_size = 0;
   size_t idat_size = 0;
   size_t pos = 8;
   unsigned width = 0;
   unsigned height = 0;
   unsigned channels = 0;
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
         unsigned bit_depth;
         unsigned color_type;

         if (len != 13)
            goto out;
         width = be32(chunk);
         height = be32(chunk + 4);
         bit_depth = chunk[8];
         color_type = chunk[9];
         if (width == 0 || height == 0 ||
             (uint64_t)width * height > PNG_MAX_PIXELS ||
             bit_depth != 8 || chunk[10] != 0 || chunk[11] != 0 ||
             chunk[12] != 0)
            goto out;
         if (color_type == 6)
            channels = 4;
         else if (color_type == 2)
            channels = 3;
         else
            goto out;
      } else if (memcmp(type, "IDAT", 4) == 0) {
         if (append_idat(&idat, &idat_size, chunk, len) != 0)
            goto out;
      } else if (memcmp(type, "IEND", 4) == 0) {
         break;
      }
      pos += len + 4;
   }

   if (!width || !height || !channels || !idat_size)
      goto out;

   {
      uLongf inflated_size =
         (uLongf)(((size_t)width * channels + 1u) * height);
      size_t row_bytes = (size_t)width * channels;

      inflated = malloc((size_t)inflated_size);
      rows = malloc(row_bytes * height);
      image->pixels = malloc((size_t)width * height * sizeof(uint16_t));
      image->alpha = malloc((size_t)width * height);
      if (!inflated || !rows || !image->pixels || !image->alpha)
         goto out;
      if (uncompress(inflated, &inflated_size, idat, (uLong)idat_size) != Z_OK ||
          inflated_size != (uLongf)((row_bytes + 1u) * height))
         goto out;
      if (unfilter_png(rows, inflated, width, height, channels) != 0)
         goto out;
      for (unsigned y = 0; y < height; y++) {
         for (unsigned x = 0; x < width; x++) {
            size_t src = ((size_t)y * width + x) * channels;
            size_t dst = (size_t)y * width + x;

            image->pixels[dst] = UNIFROG_RGB565(rows[src],
               rows[src + 1], rows[src + 2]);
            image->alpha[dst] = channels == 4 ? rows[src + 3] : 255u;
         }
      }
      image->width = width;
      image->height = height;
      ret = 0;
   }

out:
   if (ret != 0)
      unifrog_png_free(image);
   free(rows);
   free(inflated);
   free(idat);
   free(file_data);
   return ret;
}

void unifrog_png_free(struct unifrog_png_image *image)
{
   if (!image)
      return;
   free(image->pixels);
   free(image->alpha);
   memset(image, 0, sizeof(*image));
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
