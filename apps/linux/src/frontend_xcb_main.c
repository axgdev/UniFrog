#include <unifrog/linux_host.h>

#include <stdio.h>
#include <string.h>

int unifrog_linux_frontend_xcb(void);

int main(int argc, char **argv)
{
   const char *render_path = NULL;
   const char *script = NULL;

   for (int i = 1; i < argc; i++) {
      if (strcmp(argv[i], "--xcb") == 0) {
         continue;
      } else if (strcmp(argv[i], "--render-ppm") == 0 && i + 1 < argc) {
         render_path = argv[++i];
      } else if (strcmp(argv[i], "--script") == 0 && i + 1 < argc) {
         script = argv[++i];
      } else {
         fprintf(stderr,
            "usage: %s [--xcb] [--render-ppm PATH] [--script CMDS]\n",
            argv[0]);
         return 2;
      }
   }
   if (render_path)
      return unifrog_linux_frontend_render_ppm(render_path, script) == 0 ?
         0 : 1;
   if (script) {
      fprintf(stderr, "--script requires --render-ppm\n");
      return 2;
   }
   return unifrog_linux_frontend_xcb();
}
