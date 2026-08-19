#include <unifrog/frontend_app.h>
#include <unifrog/input.h>
#include <unifrog/linux_host.h>
#include <unifrog/perf.h>

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <xcb/xcb.h>

#define XCB_UI_SCALE 2u
#define XCB_UI_WIDTH 320u
#define XCB_UI_HEIGHT 240u

struct xcb_host {
   xcb_connection_t *connection;
   xcb_window_t window;
   xcb_gcontext_t gc;
   xcb_atom_t wm_delete_window;
   uint16_t *frame;
   uint32_t *pixels;
   pthread_mutex_t state_lock;
   int running;
   int stop_requested;
};

static uint32_t key_button(uint8_t detail)
{
   switch (detail) {
   case 111:
      return UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_UP);
   case 116:
      return UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_DOWN);
   case 113:
      return UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_LEFT);
   case 114:
      return UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_RIGHT);
   case 36:
   case 26:
      return UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_A);
   case 22:
   case 56:
      return UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_B);
   case 53:
      return UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_X);
   case 29:
      return UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_Y);
   case 46:
      return UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_L);
   case 27:
      return UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_R);
   case 65:
      return UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_SELECT);
   case 23:
      return UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_START);
   case 24:
      return UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_SELECT) |
         UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_START);
   default:
      return 0;
   }
}

static xcb_atom_t atom(xcb_connection_t *connection, const char *name)
{
   xcb_intern_atom_reply_t *reply;
   xcb_atom_t value = XCB_ATOM_NONE;

   reply = xcb_intern_atom_reply(connection,
      xcb_intern_atom(connection, 0, (uint16_t)strlen(name), name), NULL);
   if (reply) {
      value = reply->atom;
      free(reply);
   }
   return value;
}

static int present(struct xcb_host *host)
{
   const unsigned rows_per_request = 16u;
   unsigned width = XCB_UI_WIDTH * XCB_UI_SCALE;
   unsigned height = XCB_UI_HEIGHT * XCB_UI_SCALE;

   if (unifrog_linux_display_copy_rgb565(host->frame, XCB_UI_WIDTH,
       XCB_UI_HEIGHT) != 0)
      return -1;
   for (unsigned y = 0; y < XCB_UI_HEIGHT; y++) {
      const uint16_t *src = host->frame + (size_t)y * XCB_UI_WIDTH;

      for (unsigned x = 0; x < XCB_UI_WIDTH; x++) {
         uint16_t c = src[x];
         uint32_t r = ((c >> 11) & 0x1fu) * 255u / 31u;
         uint32_t g = ((c >> 5) & 0x3fu) * 255u / 63u;
         uint32_t b = (c & 0x1fu) * 255u / 31u;
         uint32_t value = (r << 16) | (g << 8) | b;

         for (unsigned sy = 0; sy < XCB_UI_SCALE; sy++) {
            uint32_t *dst = host->pixels +
               (size_t)(y * XCB_UI_SCALE + sy) * width +
               x * XCB_UI_SCALE;

            for (unsigned sx = 0; sx < XCB_UI_SCALE; sx++)
               dst[sx] = value;
         }
      }
   }
   for (unsigned y = 0; y < height; y += rows_per_request) {
      unsigned rows = height - y;

      if (rows > rows_per_request)
         rows = rows_per_request;
      xcb_put_image(host->connection, XCB_IMAGE_FORMAT_Z_PIXMAP,
         host->window, host->gc,
         (uint16_t)width, (uint16_t)rows, 0, (int16_t)y, 0, 24,
         width * rows * sizeof(*host->pixels),
         (const uint8_t *)(host->pixels + (size_t)y * width));
   }
   return xcb_flush(host->connection) > 0 ? 0 : -1;
}

static int host_running(struct xcb_host *host)
{
   int running;

   pthread_mutex_lock(&host->state_lock);
   running = host->running;
   pthread_mutex_unlock(&host->state_lock);
   return running;
}

static int host_stop_requested(struct xcb_host *host)
{
   int requested;

   pthread_mutex_lock(&host->state_lock);
   requested = host->stop_requested;
   pthread_mutex_unlock(&host->state_lock);
   return requested;
}

static void host_request_stop(struct xcb_host *host)
{
   pthread_mutex_lock(&host->state_lock);
   host->stop_requested = 1;
   pthread_mutex_unlock(&host->state_lock);
   unifrog_linux_set_stop_requested(1);
}

static void *xcb_host_thread(void *userdata)
{
   struct xcb_host *host = userdata;
   uint32_t buttons = 0;

   while (host_running(host)) {
      xcb_generic_event_t *event;

      while ((event = xcb_poll_for_event(host->connection)) != NULL) {
         uint8_t type = event->response_type & 0x7fu;

         if (type == XCB_KEY_PRESS || type == XCB_KEY_RELEASE) {
            const xcb_key_press_event_t *key =
               (const xcb_key_press_event_t *)event;
            uint32_t button = key_button(key->detail);

            if (key->detail == 9u) {
               host_request_stop(host);
            } else if (type == XCB_KEY_PRESS) {
               buttons |= button;
            } else {
               buttons &= ~button;
            }
         } else if (type == XCB_CLIENT_MESSAGE) {
            const xcb_client_message_event_t *client =
               (const xcb_client_message_event_t *)event;

            if (client->data.data32[0] == host->wm_delete_window)
               host_request_stop(host);
         } else if (type == XCB_DESTROY_NOTIFY) {
            host_request_stop(host);
         } else if (type == XCB_FOCUS_OUT) {
            buttons = 0;
         }
         free(event);
      }
      unifrog_linux_input_set_buttons(buttons);
      if (present(host) != 0)
         host_request_stop(host);
      unifrog_perf_delay_us(16000u);
   }
   return NULL;
}

int unifrog_linux_frontend_xcb(void)
{
   struct xcb_host host;
   xcb_connection_t *connection;
   const xcb_setup_t *setup;
   xcb_screen_t *screen;
   xcb_window_t window;
   xcb_gcontext_t gc;
   xcb_atom_t wm_protocols;
   pthread_t thread;
   uint32_t values[2];
   unsigned width = XCB_UI_WIDTH * XCB_UI_SCALE;
   unsigned height = XCB_UI_HEIGHT * XCB_UI_SCALE;
   int thread_started = 0;
   int ret;

   memset(&host, 0, sizeof(host));
   unifrog_linux_set_stop_requested(0);
   pthread_mutex_init(&host.state_lock, NULL);
   connection = xcb_connect(NULL, NULL);
   if (!connection || xcb_connection_has_error(connection)) {
      if (connection)
         xcb_disconnect(connection);
      pthread_mutex_destroy(&host.state_lock);
      fprintf(stderr, "linux frontend: cannot connect to X server\n");
      return 1;
   }
   setup = xcb_get_setup(connection);
   screen = xcb_setup_roots_iterator(setup).data;
   host.frame = calloc((size_t)XCB_UI_WIDTH * XCB_UI_HEIGHT,
      sizeof(*host.frame));
   host.pixels = calloc((size_t)width * height, sizeof(*host.pixels));
   if (!screen || !host.frame || !host.pixels) {
      free(host.frame);
      free(host.pixels);
      xcb_disconnect(connection);
      pthread_mutex_destroy(&host.state_lock);
      return 1;
   }

   window = xcb_generate_id(connection);
   gc = xcb_generate_id(connection);
   values[0] = screen->black_pixel;
   values[1] = XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_KEY_PRESS |
      XCB_EVENT_MASK_KEY_RELEASE | XCB_EVENT_MASK_STRUCTURE_NOTIFY |
      XCB_EVENT_MASK_FOCUS_CHANGE;
   xcb_create_window(connection, XCB_COPY_FROM_PARENT, window, screen->root,
      0, 0, (uint16_t)width, (uint16_t)height, 0,
      XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual,
      XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK, values);
   xcb_create_gc(connection, gc, window, 0, NULL);
   xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window,
      XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8, 13, "UniFrog Linux");
   wm_protocols = atom(connection, "WM_PROTOCOLS");
   host.wm_delete_window = atom(connection, "WM_DELETE_WINDOW");
   if (wm_protocols != XCB_ATOM_NONE &&
       host.wm_delete_window != XCB_ATOM_NONE) {
      xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window,
         wm_protocols, XCB_ATOM_ATOM, 32, 1, &host.wm_delete_window);
   }
   xcb_map_window(connection, window);
   xcb_flush(connection);

   ret = unifrog_frontend_app_init();
   if (ret != 0) {
      fprintf(stderr, "linux frontend: init failed: %d\n", ret);
   } else {
      host.connection = connection;
      host.window = window;
      host.gc = gc;
      host.running = 1;
      if (pthread_create(&thread, NULL, xcb_host_thread, &host) == 0)
         thread_started = 1;
      else
         ret = -1;
   }
   while (ret == 0 && !host_stop_requested(&host) &&
          unifrog_frontend_app_running())
      (void)unifrog_frontend_app_step();
   if (thread_started) {
      pthread_mutex_lock(&host.state_lock);
      host.running = 0;
      pthread_mutex_unlock(&host.state_lock);
      pthread_join(thread, NULL);
   }

   unifrog_linux_input_set_buttons(0);
   unifrog_linux_set_stop_requested(0);
   unifrog_frontend_app_request_stop();
   unifrog_frontend_app_shutdown();
   free(host.frame);
   free(host.pixels);
   xcb_free_gc(connection, gc);
   xcb_destroy_window(connection, window);
   xcb_disconnect(connection);
   pthread_mutex_destroy(&host.state_lock);
   return ret == 0 ? 0 : 1;
}
