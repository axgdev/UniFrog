#include <unifrog/frontend_app.h>
#include <unifrog/input.h>
#include <unifrog/linux_host.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t script_button(const char *name)
{
   if (strcmp(name, "up") == 0)
      return UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_UP);
   if (strcmp(name, "down") == 0)
      return UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_DOWN);
   if (strcmp(name, "left") == 0)
      return UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_LEFT);
   if (strcmp(name, "right") == 0)
      return UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_RIGHT);
   if (strcmp(name, "enter") == 0 || strcmp(name, "a") == 0)
      return UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_A);
   if (strcmp(name, "back") == 0 || strcmp(name, "b") == 0)
      return UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_B);
   if (strcmp(name, "x") == 0)
      return UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_X);
   if (strcmp(name, "y") == 0)
      return UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_Y);
   if (strcmp(name, "l") == 0)
      return UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_L);
   if (strcmp(name, "r") == 0)
      return UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_R);
   if (strcmp(name, "select") == 0)
      return UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_SELECT);
   if (strcmp(name, "start") == 0)
      return UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_START);
   return 0;
}

static char *trim_token(char *text)
{
   char *end;

   while (*text == ' ' || *text == '\t')
      text++;
   end = text + strlen(text);
   while (end > text && (end[-1] == ' ' || end[-1] == '\t'))
      *--end = '\0';
   return text;
}

int unifrog_linux_frontend_run_script(const char *script)
{
   char *copy;
   char *token;

   if (!script || !script[0])
      return 0;
   copy = malloc(strlen(script) + 1u);
   if (!copy)
      return -1;
   strcpy(copy, script);
   token = strtok(copy, ",");
   while (token) {
      uint32_t button = script_button(trim_token(token));

      if (!button) {
         free(copy);
         return -1;
      }
      unifrog_linux_input_set_buttons(button);
      (void)unifrog_frontend_app_step();
      unifrog_linux_input_set_buttons(0);
      (void)unifrog_frontend_app_step();
      token = strtok(NULL, ",");
   }
   free(copy);
   return 0;
}

int unifrog_linux_frontend_render_ppm(const char *path, const char *script)
{
   struct unifrog_surface surface;
   FILE *file;
   int ret;

   if (!path || !path[0])
      return -1;
   ret = unifrog_frontend_app_init();
   if (ret != 0)
      return ret;
   (void)unifrog_frontend_app_step();
   if (unifrog_linux_frontend_run_script(script) != 0) {
      unifrog_frontend_app_shutdown();
      return -1;
   }
   surface = unifrog_frontend_app_surface();
   if (!surface.pixels || !surface.width || !surface.height ||
       surface.stride < surface.width) {
      unifrog_frontend_app_shutdown();
      return -1;
   }
   file = fopen(path, "wb");
   if (!file) {
      unifrog_frontend_app_shutdown();
      return -1;
   }
   if (fprintf(file, "P6\n%u %u\n255\n", surface.width, surface.height) < 0) {
      fclose(file);
      unifrog_frontend_app_shutdown();
      return -1;
   }
   for (unsigned y = 0; y < surface.height; y++) {
      const uint16_t *row = surface.pixels + (size_t)y * surface.stride;

      for (unsigned x = 0; x < surface.width; x++) {
         uint16_t color = row[x];
         unsigned char rgb[3] = {
            (unsigned char)((((color >> 11) & 0x1fu) * 255u) / 31u),
            (unsigned char)((((color >> 5) & 0x3fu) * 255u) / 63u),
            (unsigned char)(((color & 0x1fu) * 255u) / 31u),
         };

         if (fwrite(rgb, sizeof(rgb), 1, file) != 1) {
            fclose(file);
            unifrog_frontend_app_shutdown();
            return -1;
         }
      }
   }
   ret = fclose(file);
   unifrog_frontend_app_shutdown();
   return ret == 0 ? 0 : -1;
}
