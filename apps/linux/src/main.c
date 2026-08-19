#include <unifrog/libretro_host.h>
#include <unifrog/linux_host.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

static int parse_unsigned(const char *text, unsigned *value)
{
   char *end;
   unsigned long parsed;

   if (!text || !text[0] || !value)
      return -1;
   errno = 0;
   parsed = strtoul(text, &end, 0);
   if (errno || *end || parsed > UINT_MAX)
      return -1;
   *value = (unsigned)parsed;
   return 0;
}

static int run_libretro_rom(const char *rom_path, const char *core_id,
   const char *core_path, unsigned max_frames)
{
   struct unifrog_libretro_run_options options;

   if (!rom_path || !rom_path[0])
      return 2;
   unifrog_libretro_run_options_init(&options);
   options.audio_enabled = 1;
   options.max_frames = max_frames ? max_frames : 3u;
   if (core_id && core_id[0])
      snprintf(options.core_id, sizeof(options.core_id), "%s", core_id);
   if (core_path && core_path[0])
      snprintf(options.core_path, sizeof(options.core_path), "%s", core_path);
   return unifrog_libretro_run_path_ex(rom_path, &options) == 0 ? 0 : 1;
}

int main(int argc, char **argv)
{
   const char *script = NULL;
   const char *render_path = NULL;
   const char *run_rom = NULL;
   const char *run_core = NULL;
   const char *run_core_path = NULL;
   unsigned run_max_frames = 3;

   for (int i = 1; i < argc; i++) {
      if (strcmp(argv[i], "--script") == 0 && i + 1 < argc) {
         script = argv[++i];
      } else if (strcmp(argv[i], "--render-ppm") == 0 && i + 1 < argc) {
         render_path = argv[++i];
      } else if (strcmp(argv[i], "--run-rom") == 0 && i + 1 < argc) {
         run_rom = argv[++i];
      } else if (strcmp(argv[i], "--core-id") == 0 && i + 1 < argc) {
         run_core = argv[++i];
      } else if (strcmp(argv[i], "--core-path") == 0 && i + 1 < argc) {
         run_core_path = argv[++i];
      } else if (strcmp(argv[i], "--max-frames") == 0 && i + 1 < argc) {
         if (parse_unsigned(argv[++i], &run_max_frames) != 0) {
            fprintf(stderr, "invalid --max-frames value: %s\n", argv[i]);
            return 2;
         }
      } else {
         fprintf(stderr,
            "usage: %s [--script BUTTONS --render-ppm PATH] "
            "[--run-rom ROM --core-id ID --core-path SO --max-frames N]\n",
            argv[0]);
         return 2;
      }
   }
   if (run_rom)
      return run_libretro_rom(run_rom, run_core, run_core_path,
         run_max_frames);
   if (!render_path) {
      fprintf(stderr, "--render-ppm is required for headless frontend use\n");
      return 2;
   }
   return unifrog_linux_frontend_render_ppm(render_path, script) == 0 ?
      0 : 1;
}
