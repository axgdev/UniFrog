#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t fnv1a64_update(uint64_t hash, const unsigned char *buf,
   size_t len)
{
   for (size_t i = 0; i < len; i++) {
      hash ^= (uint64_t)buf[i];
      hash *= 1099511628211ull;
   }
   return hash;
}

static int file_fingerprint(const char *path, unsigned long long *out_size,
   uint64_t *out_hash)
{
   unsigned char buf[8192];
   unsigned long long size = 0;
   uint64_t hash = 1469598103934665603ull;
   FILE *fp;

   if (!path || !out_size || !out_hash)
      return -1;

   fp = fopen(path, "rb");
   if (!fp) {
      fprintf(stderr, "bytecode_manifest: open failed: %s: %s\n",
         path, strerror(errno));
      return -1;
   }

   for (;;) {
      size_t got = fread(buf, 1, sizeof(buf), fp);

      if (got > 0) {
         hash = fnv1a64_update(hash, buf, got);
         size += (unsigned long long)got;
      }
      if (got < sizeof(buf)) {
         if (ferror(fp)) {
            fprintf(stderr, "bytecode_manifest: read failed: %s\n", path);
            fclose(fp);
            return -1;
         }
         break;
      }
   }

   if (fclose(fp) != 0) {
      fprintf(stderr, "bytecode_manifest: close failed: %s\n", path);
      return -1;
   }

   *out_size = size;
   *out_hash = hash;
   return 0;
}

static int make_bytecode_path(char *out, size_t out_size,
   const char *root, const char *source)
{
   int ret;

   if (!out || !out_size || !root || !source)
      return -1;

   ret = snprintf(out, out_size, "%s/%s.mqbc", root, source);
   if (ret < 0 || (size_t)ret >= out_size) {
      fprintf(stderr, "bytecode_manifest: path too long: %s/%s.mqbc\n",
         root, source);
      return -1;
   }
   return 0;
}

int main(int argc, char **argv)
{
   const char *bytecode_root;
   const char *out_path;
   FILE *out;

   if (argc < 4) {
      fprintf(stderr,
         "usage: bytecode_manifest <bytecode-root> <out> <source.js>...\n");
      return 2;
   }

   bytecode_root = argv[1];
   out_path = argv[2];
   out = fopen(out_path, "wb");
   if (!out) {
      fprintf(stderr, "bytecode_manifest: open output failed: %s: %s\n",
         out_path, strerror(errno));
      return 1;
   }

   fprintf(out, "format|js2300-bytecode-manifest|1\n");
   for (int i = 3; i < argc; i++) {
      char bytecode_path[1024];
      unsigned long long source_size = 0;
      unsigned long long bytecode_size = 0;
      uint64_t source_hash = 0;
      uint64_t bytecode_hash = 0;

      if (make_bytecode_path(bytecode_path, sizeof(bytecode_path),
          bytecode_root, argv[i]) != 0 ||
          file_fingerprint(argv[i], &source_size, &source_hash) != 0 ||
          file_fingerprint(bytecode_path, &bytecode_size, &bytecode_hash) != 0) {
         fclose(out);
         return 1;
      }

      fprintf(out, "file|%s|%llu|%016llx|%llu|%016llx\n",
         argv[i], source_size, (unsigned long long)source_hash,
         bytecode_size, (unsigned long long)bytecode_hash);
   }

   if (fclose(out) != 0) {
      fprintf(stderr, "bytecode_manifest: close output failed: %s\n", out_path);
      return 1;
   }
   return 0;
}
