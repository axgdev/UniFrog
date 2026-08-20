#define _GNU_SOURCE

#include <unifrog/core_registry.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int write_core(const char *root, const char *file_name,
   const char *core_id, const char *extensions)
{
   struct unifrog_core_module_header header;
   char path[256];
   FILE *file;

   memset(&header, 0, sizeof(header));
   header.magic = UNIFROG_CORE_MODULE_MAGIC;
   header.header_size = sizeof(header);
   header.format_version = UNIFROG_CORE_MODULE_FORMAT_VERSION;
   header.endian_mark = UNIFROG_CORE_MODULE_ENDIAN_MARK;
   header.flags = UNIFROG_CORE_MODULE_FLAG_FIXED_ADDRESS;
   header.load_addr = 0x82000000u;
   header.file_end_addr = 0x82001000u;
   header.memory_end_addr = 0x82002000u;
   header.bss_addr = 0x82001000u;
   header.bss_end_addr = 0x82002000u;
   header.entry_addr = 0x82000100u;
   header.gp_addr = 0x82001800u;
   snprintf(header.core_id, sizeof(header.core_id), "%s", core_id);
   snprintf(header.extensions, sizeof(header.extensions), "%s", extensions);
   snprintf(path, sizeof(path), "%s/%s", root, file_name);
   file = fopen(path, "wb");
   if (!file)
      return -1;
   if (fwrite(&header, 1, sizeof(header), file) != sizeof(header) ||
       fseek(file, (long)(header.file_end_addr - header.load_addr) - 1,
          SEEK_SET) != 0 ||
       fputc(0, file) == EOF) {
      fclose(file);
      return -1;
   }
   return fclose(file);
}

static int has_id(char ids[][UNIFROG_CORE_MODULE_ID_MAX], unsigned count,
   const char *id)
{
   for (unsigned i = 0; i < count; i++) {
      if (strcmp(ids[i], id) == 0)
         return 1;
   }
   return 0;
}

int main(void)
{
   struct unifrog_core_module_header header;
   struct unifrog_core_registry registry;
   char ids[8][UNIFROG_CORE_MODULE_ID_MAX];
   char root[] = "/tmp/unifrog-core-registry-XXXXXX";
   unsigned count;
   int ret = 1;

   if (!mkdtemp(root))
      return 1;
   if (unifrog_core_registry_read_header(NULL, NULL) == 0)
      goto out;
   if (write_core(root, "gpsp.bin", "gpsp", "gba") != 0 ||
       write_core(root, "gpsp-gbac-prosty.bin", "gpsp-gbac-prosty", "gba") != 0 ||
       write_core(root, "snes9x2002.bin", "snes9x2002", "sfc|smc") != 0 ||
       write_core(root, "truncated.bin", "truncated", "bad") != 0)
      goto out;
   {
      char path[256];

      snprintf(path, sizeof(path), "%s/truncated.bin", root);
      if (truncate(path, (off_t)sizeof(header)) != 0)
         goto out;
      snprintf(path, sizeof(path), "%s/gpsp.bin", root);
      if (unifrog_core_registry_read_header(path, &header) != 0)
         goto out;
   }
   header.bss_addr = header.load_addr;
   if (unifrog_core_registry_header_valid(&header))
      goto out;
   header.bss_addr = header.file_end_addr;
   header.entry_addr++;
   if (unifrog_core_registry_header_valid(&header))
      goto out;
   header.entry_addr--;
   memset(header.core_id, 'x', sizeof(header.core_id));
   if (unifrog_core_registry_header_valid(&header))
      goto out;
   snprintf(header.core_id, sizeof(header.core_id), "%s", "gpsp");
   header.gp_addr = header.memory_end_addr + 0x100u;
   if (!unifrog_core_registry_header_valid(&header))
      goto out;
   header.gp_addr = 0x82001800u;
   if (unifrog_core_registry_scan(&registry, root) != 0 ||
       registry.count != 3u)
      goto out;
   memset(ids, 0, sizeof(ids));
   count = unifrog_core_registry_collect_path(&registry, "/ROMS/GBA/test.gba",
      ids, 0, 8);
   if (count != 2u || !has_id(ids, count, "gpsp") ||
      !has_id(ids, count, "gpsp-gbac-prosty"))
      goto out;
   memset(ids, 0, sizeof(ids));
   count = unifrog_core_registry_collect_family(&registry, "gpsp", ids, 0, 8);
   if (count != 2u || !has_id(ids, count, "gpsp") ||
       !has_id(ids, count, "gpsp-gbac-prosty"))
      goto out;
   if (!unifrog_core_registry_find(&registry, "snes9x2002") ||
       unifrog_core_registry_find(&registry, "missing"))
      goto out;
   ret = 0;
out:
   {
      static const char *const files[] = {
         "gpsp.bin", "gpsp-gbac-prosty.bin", "snes9x2002.bin",
         "truncated.bin",
      };
      char path[256];

      for (unsigned i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
         snprintf(path, sizeof(path), "%s/%s", root, files[i]);
         (void)unlink(path);
      }
      (void)rmdir(root);
   }
   if (ret == 0)
      puts("OK core registry");
   return ret;
}
