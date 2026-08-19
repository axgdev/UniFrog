#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unifrog/frontend_controller.h>

#ifdef UNIFROG_HOST_XCB
#include <xcb/xcb.h>
#include <xcb/xproto.h>
#endif

#define W 320u
#define H 240u

struct rgb {
   uint8_t r;
   uint8_t g;
   uint8_t b;
};

static struct rgb pixels[W * H];

static const uint8_t font_alnum[36][5] = {
   {0x3e,0x51,0x49,0x45,0x3e}, {0x00,0x42,0x7f,0x40,0x00},
   {0x42,0x61,0x51,0x49,0x46}, {0x21,0x41,0x45,0x4b,0x31},
   {0x18,0x14,0x12,0x7f,0x10}, {0x27,0x45,0x45,0x45,0x39},
   {0x3c,0x4a,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03},
   {0x36,0x49,0x49,0x49,0x36}, {0x06,0x49,0x49,0x29,0x1e},
   {0x7e,0x11,0x11,0x11,0x7e}, {0x7f,0x49,0x49,0x36,0x00},
   {0x3e,0x41,0x41,0x41,0x22}, {0x7f,0x41,0x41,0x22,0x1c},
   {0x7f,0x49,0x49,0x49,0x41}, {0x7f,0x09,0x09,0x09,0x01},
   {0x3e,0x41,0x49,0x49,0x7a}, {0x7f,0x08,0x08,0x08,0x7f},
   {0x00,0x41,0x7f,0x41,0x00}, {0x20,0x40,0x41,0x3f,0x01},
   {0x7f,0x08,0x14,0x22,0x41}, {0x7f,0x40,0x40,0x40,0x40},
   {0x7f,0x02,0x0c,0x02,0x7f}, {0x7f,0x04,0x08,0x10,0x7f},
   {0x3e,0x41,0x41,0x41,0x3e}, {0x7f,0x09,0x09,0x09,0x06},
   {0x3e,0x41,0x51,0x21,0x5e}, {0x7f,0x09,0x19,0x29,0x46},
   {0x46,0x49,0x49,0x49,0x31}, {0x01,0x01,0x7f,0x01,0x01},
   {0x3f,0x40,0x40,0x40,0x3f}, {0x1f,0x20,0x40,0x20,0x1f},
   {0x3f,0x40,0x38,0x40,0x3f}, {0x63,0x14,0x08,0x14,0x63},
   {0x07,0x08,0x70,0x08,0x07}, {0x61,0x51,0x49,0x45,0x43},
};

static const uint8_t font_lower[26][5] = {
   {0x20,0x54,0x54,0x54,0x78}, {0x7f,0x48,0x44,0x44,0x38},
   {0x38,0x44,0x44,0x44,0x20}, {0x38,0x44,0x44,0x48,0x7f},
   {0x38,0x54,0x54,0x54,0x18}, {0x08,0x7e,0x09,0x01,0x02},
   {0x0c,0x52,0x52,0x52,0x3e}, {0x7f,0x08,0x04,0x04,0x78},
   {0x00,0x44,0x7d,0x40,0x00}, {0x20,0x40,0x44,0x3d,0x00},
   {0x7f,0x10,0x28,0x44,0x00}, {0x00,0x41,0x7f,0x40,0x00},
   {0x7c,0x04,0x18,0x04,0x78}, {0x7c,0x08,0x04,0x04,0x78},
   {0x38,0x44,0x44,0x44,0x38}, {0x7c,0x14,0x14,0x14,0x08},
   {0x08,0x14,0x14,0x18,0x7c}, {0x7c,0x08,0x04,0x04,0x08},
   {0x48,0x54,0x54,0x54,0x20}, {0x04,0x3f,0x44,0x40,0x20},
   {0x3c,0x40,0x40,0x20,0x7c}, {0x1c,0x20,0x40,0x20,0x1c},
   {0x3c,0x40,0x30,0x40,0x3c}, {0x44,0x28,0x10,0x28,0x44},
   {0x0c,0x50,0x50,0x50,0x3c}, {0x44,0x64,0x54,0x4c,0x44},
};

static const uint8_t *glyph_data(unsigned char c)
{
   static const uint8_t dash[5] = { 0x08,0x08,0x08,0x08,0x08 };
   static const uint8_t dot[5] = { 0x00,0x60,0x60,0x00,0x00 };
   static const uint8_t slash[5] = { 0x20,0x10,0x08,0x04,0x02 };
   static const uint8_t colon[5] = { 0x00,0x36,0x36,0x00,0x00 };
   static const uint8_t star[5] = { 0x14,0x08,0x3e,0x08,0x14 };
   static const uint8_t question[5] = { 0x02,0x01,0x51,0x09,0x06 };

   if (c >= '0' && c <= '9')
      return font_alnum[c - '0'];
   if (c >= 'A' && c <= 'Z')
      return font_alnum[10 + c - 'A'];
   if (c >= 'a' && c <= 'z')
      return font_lower[c - 'a'];
   if (c == '-')
      return dash;
   if (c == '.')
      return dot;
   if (c == '/')
      return slash;
   if (c == ':')
      return colon;
   if (c == '*')
      return star;
   return c == ' ' ? NULL : question;
}

static void pixel(int x, int y, struct rgb color)
{
   if (x >= 0 && y >= 0 && x < (int)W && y < (int)H)
      pixels[(unsigned)y * W + (unsigned)x] = color;
}

static void rect(int x, int y, int w, int h, struct rgb color)
{
   for (int yy = 0; yy < h; yy++)
      for (int xx = 0; xx < w; xx++)
         pixel(x + xx, y + yy, color);
}

static void glyph(int x, int y, unsigned char c, struct rgb color)
{
   const uint8_t *data = glyph_data(c);

   if (!data)
      return;
   for (int xx = 0; xx < 5; xx++) {
      for (int yy = 0; yy < 7; yy++) {
         if (data[xx] & (1u << yy))
            pixel(x + xx, y + yy, color);
      }
   }
}

static void text(int x, int y, const char *value, struct rgb color,
   unsigned max_chars)
{
   for (unsigned i = 0; value && value[i] && i < max_chars; i++)
      glyph(x + (int)i * 6, y, (unsigned char)value[i], color);
}

static void render(const struct unifrog_frontend_model *model)
{
   const struct rgb background = { 10, 12, 14 };
   const struct rgb panel = { 24, 29, 33 };
   const struct rgb focus = { 53, 92, 123 };
   const struct rgb text_color = { 232, 235, 226 };
   const struct rgb muted = { 116, 126, 128 };
   unsigned start = model->selected >= 8u ? model->selected - 7u : 0u;

   rect(0, 0, W, H, background);
   rect(0, 0, W, 28, panel);
   rect(0, H - 24, W, 24, panel);
   text(10, 10, model->title, text_color, 34);
   for (unsigned row = 0; row < 8u && start + row < model->count; row++) {
      const struct unifrog_frontend_model_item *item =
         &model->items[start + row];
      int y = 34 + (int)row * 22;

      rect(8, y, 304, 18, start + row == model->selected ? focus : panel);
      text(14, y + 5, item->label, text_color, 28);
      text(190, y + 5, item->detail, start + row == model->selected ?
         text_color : muted, 19);
   }
   text(10, 222, model->status[0] ? model->status :
      "arrows navigate  tab view  q quit", muted, 49);
}

static int write_ppm(const char *path)
{
   FILE *file = fopen(path, "wb");

   if (!file)
      return -1;
   fprintf(file, "P6\n%u %u\n255\n", W, H);
   if (fwrite(pixels, sizeof(pixels[0]), W * H, file) != W * H) {
      fclose(file);
      return -1;
   }
   return fclose(file);
}

static void default_settings(struct unifrog_frontend_model_settings *settings)
{
   memset(settings, 0, sizeof(*settings));
   settings->theme = "muos";
   settings->language = "english";
   settings->rom_root_label = "ROMs";
   settings->rom_root = "/ROMS";
   settings->active_storage_profile = "wide25";
   settings->configured_storage_profile = "wide25";
   settings->boot_storage_profile = "wide25";
   settings->clock_enabled = 1;
   settings->content_collect = 1;
   settings->content_history = 1;
   settings->menu_counter_folder = 1;
   settings->menu_counter_file = 1;
}

static int simulate_action(void *userdata,
   enum unifrog_frontend_action action, const char *payload)
{
   (void)userdata;
   printf("frontend model action=%s payload=%s\n",
      unifrog_frontend_action_id(action), payload ? payload : "");
   return 0;
}

static int headless(const char *directory)
{
   static const struct {
      enum unifrog_frontend_model_screen screen;
      const char *name;
      unsigned selected;
   } cases[] = {
      { UNIFROG_FRONTEND_MODEL_LAUNCH, "launcher.ppm", 0 },
      { UNIFROG_FRONTEND_MODEL_CONFIG, "config.ppm", 2 },
      { UNIFROG_FRONTEND_MODEL_STORAGE, "storage.ppm", 0 },
      { UNIFROG_FRONTEND_MODEL_STORAGE_MODE, "storage-mode.ppm", 20 },
   };
   struct unifrog_frontend_model_settings settings;
   struct unifrog_frontend_model model;
   char path[512];

   default_settings(&settings);
   for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
      unifrog_frontend_model_build(&model, cases[i].screen, &settings);
      model.selected = cases[i].selected < model.count ? cases[i].selected : 0;
      render(&model);
      snprintf(path, sizeof(path), "%s/%s", directory, cases[i].name);
      if (write_ppm(path) != 0) {
         fprintf(stderr, "write %s: %s\n", path, strerror(errno));
         return 1;
      }
   }
   printf("frontend model artifacts=%s cases=%u\n", directory,
      (unsigned)(sizeof(cases) / sizeof(cases[0])));
   return 0;
}

#ifdef UNIFROG_HOST_XCB
static void present(xcb_connection_t *connection, xcb_window_t window,
   xcb_gcontext_t gc)
{
   static uint32_t xrgb[W * H];

   for (unsigned i = 0; i < W * H; i++)
      xrgb[i] = ((uint32_t)pixels[i].r << 16) |
         ((uint32_t)pixels[i].g << 8) | pixels[i].b;
   xcb_put_image(connection, XCB_IMAGE_FORMAT_Z_PIXMAP, window, gc,
      W, H, 0, 0, 0, 24, sizeof(xrgb), (const uint8_t *)xrgb);
   xcb_flush(connection);
}

static int xcb_run(void)
{
   struct unifrog_frontend_model_settings settings;
   struct unifrog_frontend_controller controller;
   struct unifrog_frontend_controller_services services = {
      .action = simulate_action,
      .userdata = NULL,
   };
   xcb_connection_t *connection = xcb_connect(NULL, NULL);
   const xcb_setup_t *setup = xcb_get_setup(connection);
   xcb_screen_t *xcb_screen = xcb_setup_roots_iterator(setup).data;
   xcb_window_t window = xcb_generate_id(connection);
   xcb_gcontext_t gc = xcb_generate_id(connection);
   uint32_t mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
   uint32_t values[] = {
      xcb_screen->black_pixel,
      XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_KEY_PRESS |
         XCB_EVENT_MASK_STRUCTURE_NOTIFY,
   };
   int running = 1;

   if (xcb_connection_has_error(connection))
      return 1;
   xcb_create_window(connection, XCB_COPY_FROM_PARENT, window,
      xcb_screen->root, 0, 0, W, H, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
      xcb_screen->root_visual, mask, values);
   xcb_create_gc(connection, gc, window, 0, NULL);
   xcb_map_window(connection, window);
   default_settings(&settings);
   unifrog_frontend_controller_init(&controller, &settings, &services);
   render(&controller.model);
   present(connection, window, gc);
   while (running) {
      xcb_generic_event_t *event = xcb_wait_for_event(connection);
      uint8_t type;

      if (!event)
         break;
      type = event->response_type & ~0x80u;
      if (type == XCB_EXPOSE) {
         present(connection, window, gc);
      } else if (type == XCB_KEY_PRESS) {
         xcb_key_press_event_t *key = (xcb_key_press_event_t *)event;

         if (key->detail == 9u || key->detail == 24u) {
            running = 0;
         } else if (key->detail == 23u) {
            unifrog_frontend_controller_back(&controller);
         } else if (key->detail == 111u || key->detail == 113u) {
            unifrog_frontend_controller_move(&controller, -1);
         } else if (key->detail == 116u || key->detail == 114u) {
            unifrog_frontend_controller_move(&controller, 1);
         } else if (key->detail == 36u) {
            (void)unifrog_frontend_controller_activate(&controller);
         } else if (key->detail == 22u) {
            unifrog_frontend_controller_back(&controller);
         }
         render(&controller.model);
         present(connection, window, gc);
      } else if (type == XCB_DESTROY_NOTIFY) {
         running = 0;
      }
      free(event);
   }
   xcb_disconnect(connection);
   return 0;
}
#endif

int main(int argc, char **argv)
{
   if (argc == 3 && strcmp(argv[1], "--headless") == 0)
      return headless(argv[2]);
#ifdef UNIFROG_HOST_XCB
   if (argc == 2 && strcmp(argv[1], "--xcb") == 0)
      return xcb_run();
#endif
   fprintf(stderr, "usage: %s --headless DIR", argv[0]);
#ifdef UNIFROG_HOST_XCB
   fprintf(stderr, " | --xcb");
#endif
   fputc('\n', stderr);
   return 2;
}
