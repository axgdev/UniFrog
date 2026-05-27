#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 320u
#define H 240u
#define PNG_MAX_BLOCK 65535u

struct rgb {
   uint8_t r;
   uint8_t g;
   uint8_t b;
};

static void set_pixel(struct rgb *pix, unsigned x, unsigned y, struct rgb c)
{
   if (x < W && y < H)
      pix[y * W + x] = c;
}

static void fill_rect(struct rgb *pix, unsigned x, unsigned y,
   unsigned w, unsigned h, struct rgb c)
{
   for (unsigned yy = 0; yy < h; yy++)
      for (unsigned xx = 0; xx < w; xx++)
         set_pixel(pix, x + xx, y + yy, c);
}

static void frame_rect(struct rgb *pix, unsigned x, unsigned y,
   unsigned w, unsigned h, struct rgb c)
{
   if (!w || !h)
      return;
   fill_rect(pix, x, y, w, 1, c);
   fill_rect(pix, x, y + h - 1u, w, 1, c);
   fill_rect(pix, x, y, 1, h, c);
   fill_rect(pix, x + w - 1u, y, 1, h, c);
}

static void render_frame(struct rgb *pix)
{
   struct rgb bg = { 10, 12, 14 };
   struct rgb panel = { 24, 29, 33 };
   struct rgb focus = { 53, 92, 123 };
   struct rgb green = { 78, 144, 86 };
   struct rgb amber = { 188, 146, 56 };
   struct rgb red = { 154, 72, 72 };
   struct rgb text = { 232, 235, 226 };
   struct rgb muted = { 116, 126, 128 };

   fill_rect(pix, 0, 0, W, H, bg);
   fill_rect(pix, 0, 0, W, 30, panel);
   fill_rect(pix, 0, H - 28u, W, 28, panel);
   fill_rect(pix, 12, 42, 132, 154, (struct rgb){ 18, 22, 24 });
   fill_rect(pix, 156, 42, 152, 154, (struct rgb){ 16, 18, 22 });
   frame_rect(pix, 12, 42, 132, 154, muted);
   frame_rect(pix, 156, 42, 152, 154, muted);

   for (unsigned i = 0; i < 6; i++) {
      unsigned y = 52u + i * 22u;
      fill_rect(pix, 22, y, 112, 17, i == 1 ? focus : panel);
      fill_rect(pix, 30, y + 5u, 50 + i * 7u, 2, text);
      fill_rect(pix, 30, y + 10u, 78 - i * 5u, 2, i == 1 ? text : muted);
   }

   fill_rect(pix, 170, 58, 52, 52, green);
   fill_rect(pix, 232, 58, 52, 52, amber);
   fill_rect(pix, 170, 120, 52, 52, red);
   fill_rect(pix, 232, 120, 52, 52, focus);
   frame_rect(pix, 170, 58, 52, 52, text);
   frame_rect(pix, 232, 58, 52, 52, text);
   frame_rect(pix, 170, 120, 52, 52, text);
   frame_rect(pix, 232, 120, 52, 52, text);

   for (unsigned i = 0; i < 18; i++) {
      fill_rect(pix, 18 + i * 6u, 12, 4, 6 + (i % 4u) * 3u,
         i % 3u == 0 ? green : (i % 3u == 1 ? amber : focus));
   }
   fill_rect(pix, 242, 11, 48, 8, muted);
   fill_rect(pix, 242, 11, 34, 8, green);
   frame_rect(pix, 241, 10, 50, 10, text);

   for (unsigned i = 0; i < 9; i++)
      fill_rect(pix, 22 + i * 12u, 211, 8, 8,
         i < 5 ? text : muted);
   fill_rect(pix, 168, 213, 116, 3, muted);
   fill_rect(pix, 168, 221, 82, 3, muted);
}

static int write_ppm(const char *path, const struct rgb *pix)
{
   FILE *file = fopen(path, "wb");

   if (!file) {
      fprintf(stderr, "open %s: %s\n", path, strerror(errno));
      return -1;
   }
   fprintf(file, "P6\n%u %u\n255\n", W, H);
   if (fwrite(pix, sizeof(*pix), W * H, file) != W * H) {
      fprintf(stderr, "write %s: %s\n", path, strerror(errno));
      fclose(file);
      return -1;
   }
   if (fclose(file) != 0) {
      fprintf(stderr, "close %s: %s\n", path, strerror(errno));
      return -1;
   }
   return 0;
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t len)
{
   crc = ~crc;
   for (size_t i = 0; i < len; i++) {
      crc ^= data[i];
      for (unsigned bit = 0; bit < 8; bit++)
         crc = (crc >> 1) ^ (0xedb88320u & (uint32_t)-(int)(crc & 1u));
   }
   return ~crc;
}

static uint32_t adler32_bytes(const uint8_t *data, size_t len)
{
   uint32_t a = 1;
   uint32_t b = 0;

   for (size_t i = 0; i < len; i++) {
      a = (a + data[i]) % 65521u;
      b = (b + a) % 65521u;
   }
   return (b << 16) | a;
}

static void put_be32(uint8_t out[4], uint32_t v)
{
   out[0] = (uint8_t)(v >> 24);
   out[1] = (uint8_t)(v >> 16);
   out[2] = (uint8_t)(v >> 8);
   out[3] = (uint8_t)v;
}

static int write_chunk(FILE *file, const char type[4],
   const uint8_t *data, size_t len)
{
   uint8_t len_be[4];
   uint8_t crc_be[4];
   uint32_t crc;

   if (len > 0xffffffffu)
      return -1;
   put_be32(len_be, (uint32_t)len);
   if (fwrite(len_be, 1, sizeof(len_be), file) != sizeof(len_be) ||
       fwrite(type, 1, 4, file) != 4)
      return -1;
   if (len && fwrite(data, 1, len, file) != len)
      return -1;
   crc = crc32_update(0, (const uint8_t *)type, 4);
   crc = crc32_update(crc, data, len);
   put_be32(crc_be, crc);
   return fwrite(crc_be, 1, sizeof(crc_be), file) == sizeof(crc_be) ? 0 : -1;
}

static int build_png_idat(const struct rgb *pix, uint8_t **out,
   size_t *out_size)
{
   size_t row = 1u + W * 3u;
   size_t raw_size = row * H;
   size_t max_blocks = (raw_size + PNG_MAX_BLOCK - 1u) / PNG_MAX_BLOCK;
   size_t idat_cap = 2u + raw_size + max_blocks * 5u + 4u;
   uint8_t *raw = malloc(raw_size);
   uint8_t *idat = malloc(idat_cap);
   uint8_t *p;
   size_t pos = 0;
   uint32_t adler;

   if (!raw || !idat) {
      free(raw);
      free(idat);
      return -1;
   }
   for (unsigned y = 0; y < H; y++) {
      uint8_t *dst = raw + y * row;

      *dst++ = 0;
      for (unsigned x = 0; x < W; x++) {
         const struct rgb *src = pix + y * W + x;

         *dst++ = src->r;
         *dst++ = src->g;
         *dst++ = src->b;
      }
   }

   p = idat;
   *p++ = 0x78;
   *p++ = 0x01;
   while (pos < raw_size) {
      size_t n = raw_size - pos;
      uint16_t len;
      uint16_t nlen;

      if (n > PNG_MAX_BLOCK)
         n = PNG_MAX_BLOCK;
      len = (uint16_t)n;
      nlen = (uint16_t)~len;
      *p++ = (pos + n == raw_size) ? 1u : 0u;
      *p++ = (uint8_t)len;
      *p++ = (uint8_t)(len >> 8);
      *p++ = (uint8_t)nlen;
      *p++ = (uint8_t)(nlen >> 8);
      memcpy(p, raw + pos, n);
      p += n;
      pos += n;
   }
   adler = adler32_bytes(raw, raw_size);
   put_be32(p, adler);
   p += 4;

   free(raw);
   *out = idat;
   *out_size = (size_t)(p - idat);
   return 0;
}

static int write_png(const char *path, const struct rgb *pix)
{
   static const uint8_t sig[8] = {
      0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'
   };
   FILE *file = fopen(path, "wb");
   uint8_t ihdr[13];
   uint8_t *idat = NULL;
   size_t idat_size = 0;
   int ret = -1;

   if (!file) {
      fprintf(stderr, "open %s: %s\n", path, strerror(errno));
      return -1;
   }
   put_be32(ihdr, W);
   put_be32(ihdr + 4, H);
   ihdr[8] = 8;
   ihdr[9] = 2;
   ihdr[10] = 0;
   ihdr[11] = 0;
   ihdr[12] = 0;

   if (build_png_idat(pix, &idat, &idat_size) != 0)
      goto out;
   if (fwrite(sig, 1, sizeof(sig), file) != sizeof(sig))
      goto out;
   if (write_chunk(file, "IHDR", ihdr, sizeof(ihdr)) != 0 ||
       write_chunk(file, "IDAT", idat, idat_size) != 0 ||
       write_chunk(file, "IEND", NULL, 0) != 0)
      goto out;
   ret = 0;

out:
   free(idat);
   if (fclose(file) != 0)
      ret = -1;
   if (ret != 0)
      fprintf(stderr, "write %s: %s\n", path, strerror(errno));
   return ret;
}

static unsigned sampled_unique_colors(const struct rgb *pix)
{
   uint32_t seen[64];
   unsigned unique = 0;

   for (unsigned i = 0; i < W * H; i += 151u) {
      const struct rgb *p = pix + i;
      uint32_t c = ((uint32_t)p->r << 16) | ((uint32_t)p->g << 8) | p->b;
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
   return unique;
}

int main(int argc, char **argv)
{
   struct rgb *pix;
   unsigned unique;

   if (argc != 3) {
      fprintf(stderr, "usage: %s out.ppm out.png\n", argv[0]);
      return 2;
   }
   pix = calloc(W * H, sizeof(*pix));
   if (!pix)
      return 1;
   render_frame(pix);
   unique = sampled_unique_colors(pix);
   if (unique < 8) {
      fprintf(stderr, "visual output is too uniform: %u colors\n", unique);
      free(pix);
      return 1;
   }
   if (write_ppm(argv[1], pix) != 0 || write_png(argv[2], pix) != 0) {
      free(pix);
      return 1;
   }
   printf("host visual ppm=%s png=%s unique_sampled_colors=%u\n",
      argv[1], argv[2], unique);
   free(pix);
   return 0;
}
