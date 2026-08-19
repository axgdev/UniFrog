/* Frontend-owned in-game presentation for an active libretro session. */
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <unifrog/gfx.h>
#include <unifrog/input.h>
#include <unifrog/libretro_session.h>
#include <unifrog/perf.h>

struct libretro_core_api;

static void quick_menu_draw_row(struct unifrog_surface *surface, int y,
   const char *label, const char *detail, int focused)
{
   uint16_t bg = focused ? UNIFROG_RGB565(45, 95, 110) :
      UNIFROG_RGB565(22, 29, 39);
   uint16_t fg = focused ? UNIFROG_RGB565(255, 255, 255) :
      UNIFROG_RGB565(230, 238, 240);
   uint16_t muted = focused ? UNIFROG_RGB565(205, 240, 235) :
      UNIFROG_RGB565(139, 154, 160);
   int w = (int)surface->width - 20;

   unifrog_gfx_fill_rect(surface, 10, y - 3, w, 22, bg);
   if (focused)
      unifrog_gfx_fill_rect(surface, 10, y - 3, 4, 22,
         UNIFROG_RGB565(120, 214, 189));
   unifrog_gfx_draw_text(surface, 14, y + 2, label ? label : "", fg, 1);
   if (detail && detail[0])
      unifrog_gfx_draw_text(surface, 216, y + 2, detail, muted, 1);
}

static void quick_menu_draw_tab(struct unifrog_surface *surface, int x, int y,
   int w, const char *label, int focused)
{
   uint16_t bg = focused ? UNIFROG_RGB565(52, 104, 132) :
      UNIFROG_RGB565(22, 29, 39);
   uint16_t fg = focused ? UNIFROG_RGB565(255, 255, 255) :
      UNIFROG_RGB565(205, 240, 235);

   unifrog_gfx_fill_rect(surface, x, y, w, 20, bg);
   if (focused)
      unifrog_gfx_fill_rect(surface, x, y, w, 2,
         UNIFROG_RGB565(120, 214, 189));
   unifrog_gfx_draw_text(surface, x + 8, y + 6, label ? label : "", fg, 1);
}

static void quick_menu_detail(char *dst, size_t size, unsigned value)
{
   if (!dst || size == 0)
      return;
   snprintf(dst, size, "< %u >", value);
}

static void quick_menu_fast_forward_detail(char *dst, size_t size)
{
   struct unifrog_libretro_session_snapshot snapshot;

   if (!dst || size == 0)
      return;
   unifrog_libretro_session_snapshot(&snapshot);
   if (snapshot.fast_forward_multiplier == 0)
      snprintf(dst, size, "< Off >");
   else
      snprintf(dst, size, "< %ux >", snapshot.fast_forward_multiplier);
}

static unsigned quick_menu_visible_core_option_count(void)
{
   return unifrog_libretro_session_option_count();
}

static void quick_menu_core_option_detail(char *dst, size_t size,
   const struct unifrog_libretro_session_option *option)
{
   if (!dst || size == 0)
      return;
   if (!option) {
      dst[0] = '\0';
      return;
   }
   snprintf(dst, size, "< %.18s >", option->value);
}

static void quick_menu_cycle_core_option(unsigned visible_index, int delta)
{
   (void)unifrog_libretro_session_option_adjust(visible_index, delta);
}

static void quick_menu_present(unsigned buffer)
{
   unifrog_libretro_session_present(buffer);
}

static void quick_menu_persist_status(const char *action, int ret)
{
   char status[UNIFROG_LIBRETRO_SESSION_STATUS_MAX];

   snprintf(status, sizeof(status), "%s%s", action,
      ret == 0 ? "" : " failed");
   unifrog_libretro_session_status(status);
}

int quick_menu_run(const struct libretro_core_api *core,
   const char *rom_path)
{
   enum quick_menu_page {
      QUICK_MENU_PAGE_MAIN = 0,
      QUICK_MENU_PAGE_CORE = 1,
      QUICK_MENU_PAGE_UNIFROG = 2,
      QUICK_MENU_PAGE_CONFIRM = 3,
   };
   enum quick_menu_main_row {
      QUICK_MENU_MAIN_RESUME,
      QUICK_MENU_MAIN_SAVE,
      QUICK_MENU_MAIN_LOAD,
      QUICK_MENU_MAIN_CORE,
      QUICK_MENU_MAIN_UNIFROG,
      QUICK_MENU_MAIN_RETURN,
      QUICK_MENU_MAIN_COUNT,
   };
   enum quick_menu_core_row {
      QUICK_MENU_CORE_BACK,
      QUICK_MENU_CORE_SAVE_CORE,
      QUICK_MENU_CORE_SAVE_GAME,
      QUICK_MENU_CORE_CLEAR_GAME,
      QUICK_MENU_CORE_CLEAR_CORE,
      QUICK_MENU_CORE_FIXED_COUNT,
   };
   enum quick_menu_unifrog_row {
      QUICK_MENU_UNIFROG_BACK,
      QUICK_MENU_UNIFROG_FAST_FORWARD,
      QUICK_MENU_UNIFROG_FRAMESKIP,
      QUICK_MENU_UNIFROG_AUDIO,
      QUICK_MENU_UNIFROG_DISPLAY,
      QUICK_MENU_UNIFROG_KEYMAP,
      QUICK_MENU_UNIFROG_CPU,
      QUICK_MENU_UNIFROG_GE,
      QUICK_MENU_UNIFROG_BACKLIGHT,
      QUICK_MENU_UNIFROG_RTC_OFFSET,
      QUICK_MENU_UNIFROG_SAVE_GAME,
      QUICK_MENU_UNIFROG_SAVE_CORE,
      QUICK_MENU_UNIFROG_CLEAR_GAME,
      QUICK_MENU_UNIFROG_CLEAR_CORE,
      QUICK_MENU_UNIFROG_COUNT,
   };
   enum quick_menu_confirm_row {
      QUICK_MENU_CONFIRM_YES,
      QUICK_MENU_CONFIRM_CANCEL,
      QUICK_MENU_CONFIRM_COUNT,
   };
   enum quick_menu_confirm_action {
      QUICK_MENU_CONFIRM_NONE,
      QUICK_MENU_CONFIRM_SAVE,
      QUICK_MENU_CONFIRM_LOAD,
   };
   static const char *main_labels[] = {
      "Resume", "Save state", "Load state", "Core Options",
      "UniFrog Settings", "Return to UniFrog",
   };
   static const char *core_labels[] = {
      "Back", "Save options for core", "Save options for game",
      "Clear saved game options", "Clear saved core options",
   };
   static const char *unifrog_labels[] = {
      "Back", "Fast forward", "Frameskip", "Audio", "Display", "Keymap",
      "CPU", "GE", "Backlight", "Game RTC offset", "Save settings for game",
      "Save settings for core", "Clear saved game settings",
      "Clear saved core settings",
   };
   static const char *confirm_labels[] = {
      "Confirm", "Cancel",
   };
   unsigned selected = 0;
   unsigned page = QUICK_MENU_PAGE_MAIN;
   unsigned previous_page = QUICK_MENU_PAGE_MAIN;
   unsigned previous_selected = QUICK_MENU_MAIN_SAVE;
   unsigned confirm_action = QUICK_MENU_CONFIRM_NONE;
   unsigned save_slot;
   unsigned load_slot;
   uint32_t previous = 0;
   uint32_t repeat_button = 0;
   uint32_t next_repeat_ms = 0;
   int input_ready = 0;
   int entry_combo_released = 0;
   const uint32_t entry_combo =
      UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_SELECT) |
      UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_START);
   const uint32_t action_buttons =
      entry_combo |
      UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_A) |
      UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_B);
   static const char *labels[QUICK_MENU_CORE_FIXED_COUNT +
      UNIFROG_LIBRETRO_SESSION_OPTION_MAX];
   static char label_storage[UNIFROG_LIBRETRO_SESSION_OPTION_MAX]
      [UNIFROG_LIBRETRO_SESSION_OPTION_LABEL_MAX];
   static char detail[QUICK_MENU_CORE_FIXED_COUNT +
      UNIFROG_LIBRETRO_SESSION_OPTION_MAX][28];
   struct unifrog_libretro_session_snapshot snapshot;
   int return_to_frontend = 0;

   if (unifrog_libretro_session_menu_begin(core, rom_path) != 0)
      return 0;
   unifrog_libretro_session_snapshot(&snapshot);
   save_slot = snapshot.state_slot;
   load_slot = snapshot.state_slot;

   printf("unifrog quick_menu start entry_buttons=0x%08lx\n",
      (unsigned long)unifrog_input_menu_buttons());

   for (;;) {
      uint32_t buttons;
      uint32_t pressed;
      uint32_t now_ms;
      unsigned draw_buffer;
      struct unifrog_surface surface;
      unsigned row_count;
      unsigned visible_rows;
      unsigned first_row;
      const char *footer;

      if (unifrog_libretro_session_surface(&surface, &draw_buffer) != 0)
         break;
      unifrog_libretro_session_snapshot(&snapshot);

      memset(detail, 0, sizeof(detail));
      if (page == QUICK_MENU_PAGE_MAIN) {
         for (unsigned i = 0; i < QUICK_MENU_MAIN_COUNT; i++)
            labels[i] = main_labels[i];
         row_count = QUICK_MENU_MAIN_COUNT;
         quick_menu_detail(detail[QUICK_MENU_MAIN_SAVE],
            sizeof(detail[QUICK_MENU_MAIN_SAVE]), save_slot + 1u);
         quick_menu_detail(detail[QUICK_MENU_MAIN_LOAD],
            sizeof(detail[QUICK_MENU_MAIN_LOAD]), load_slot + 1u);
         snprintf(detail[QUICK_MENU_MAIN_CORE],
            sizeof(detail[QUICK_MENU_MAIN_CORE]), "%u options",
            quick_menu_visible_core_option_count());
      } else {
         if (page == QUICK_MENU_PAGE_CORE) {
            unsigned visible_core_options = quick_menu_visible_core_option_count();

            for (unsigned i = 0; i < QUICK_MENU_CORE_FIXED_COUNT; i++)
               labels[i] = core_labels[i];
            row_count = QUICK_MENU_CORE_FIXED_COUNT + visible_core_options;
            for (unsigned i = 0; i < visible_core_options; i++) {
               struct unifrog_libretro_session_option option;
               unsigned row = QUICK_MENU_CORE_FIXED_COUNT + i;
               if (unifrog_libretro_session_option_get(i, &option) != 0)
                  continue;
               snprintf(label_storage[i], sizeof(label_storage[i]), "%.25s",
                  option.label);
               labels[row] = label_storage[i];
               quick_menu_core_option_detail(detail[row], sizeof(detail[row]),
                  &option);
            }
         } else if (page == QUICK_MENU_PAGE_UNIFROG) {
            for (unsigned i = 0; i < QUICK_MENU_UNIFROG_COUNT; i++)
               labels[i] = unifrog_labels[i];
            row_count = QUICK_MENU_UNIFROG_COUNT;
            quick_menu_fast_forward_detail(
               detail[QUICK_MENU_UNIFROG_FAST_FORWARD],
               sizeof(detail[QUICK_MENU_UNIFROG_FAST_FORWARD]));
            snprintf(detail[QUICK_MENU_UNIFROG_FRAMESKIP],
               sizeof(detail[QUICK_MENU_UNIFROG_FRAMESKIP]), "< %s >",
               snapshot.frameskip_label);
            snprintf(detail[QUICK_MENU_UNIFROG_AUDIO],
               sizeof(detail[QUICK_MENU_UNIFROG_AUDIO]), "< %s >",
               snapshot.audio_enabled ? "on" : "off");
            snprintf(detail[QUICK_MENU_UNIFROG_DISPLAY],
               sizeof(detail[QUICK_MENU_UNIFROG_DISPLAY]), "< %s >",
               snapshot.display_label);
            snprintf(detail[QUICK_MENU_UNIFROG_KEYMAP],
               sizeof(detail[QUICK_MENU_UNIFROG_KEYMAP]), "< %s >",
               snapshot.input_profile_label);
            snprintf(detail[QUICK_MENU_UNIFROG_CPU],
               sizeof(detail[QUICK_MENU_UNIFROG_CPU]), "< %u >",
               snapshot.scpu_mhz);
            snprintf(detail[QUICK_MENU_UNIFROG_GE],
               sizeof(detail[QUICK_MENU_UNIFROG_GE]), "< %s >",
               snapshot.ge_label);
            quick_menu_detail(detail[QUICK_MENU_UNIFROG_BACKLIGHT],
               sizeof(detail[QUICK_MENU_UNIFROG_BACKLIGHT]),
               snapshot.backlight);
            snprintf(detail[QUICK_MENU_UNIFROG_RTC_OFFSET],
               sizeof(detail[QUICK_MENU_UNIFROG_RTC_OFFSET]), "< %+d days >",
               snapshot.rtc_offset_minutes / (24 * 60));
         } else {
            for (unsigned i = 0; i < QUICK_MENU_CONFIRM_COUNT; i++)
               labels[i] = confirm_labels[i];
            row_count = QUICK_MENU_CONFIRM_COUNT;
            snprintf(detail[QUICK_MENU_CONFIRM_YES],
               sizeof(detail[QUICK_MENU_CONFIRM_YES]), "< slot %u >",
               confirm_action == QUICK_MENU_CONFIRM_LOAD ?
                  load_slot + 1u : save_slot + 1u);
         }
      }
      footer = page == QUICK_MENU_PAGE_CONFIRM ?
         "A confirm  B cancel" : "A choose  Left/Right adjust  B back";
      visible_rows = page == QUICK_MENU_PAGE_CONFIRM ? 2u : 8u;
      if (visible_rows > row_count)
         visible_rows = row_count;
      first_row = 0;
      if (selected >= visible_rows)
         first_row = selected - visible_rows + 1u;

      unifrog_gfx_fill_rect(&surface, 0, 0, surface.width, surface.height,
         UNIFROG_RGB565(8, 10, 14));
      unifrog_gfx_fill_rect(&surface, 0, 0, surface.width, 36,
         UNIFROG_RGB565(22, 29, 39));
      unifrog_gfx_fill_rect(&surface, 0, 36, surface.width, 1,
         UNIFROG_RGB565(120, 214, 189));
      unifrog_gfx_draw_text(&surface, 12, 10, "Pause Menu",
         UNIFROG_RGB565(230, 238, 240), 1);
      unifrog_gfx_draw_text(&surface, (int)surface.width - 58, 10,
         page == QUICK_MENU_PAGE_MAIN ? "paused" :
            page == QUICK_MENU_PAGE_CORE ? "core" :
            page == QUICK_MENU_PAGE_UNIFROG ? "settings" : "sure?",
         UNIFROG_RGB565(139, 154, 160), 1);
      quick_menu_draw_tab(&surface, 10, 38, 70, "Pause",
         page == QUICK_MENU_PAGE_MAIN);
      quick_menu_draw_tab(&surface, 84, 38, 70, "Core",
         page == QUICK_MENU_PAGE_CORE);
      quick_menu_draw_tab(&surface, 158, 38, 78, "UniFrog",
         page == QUICK_MENU_PAGE_UNIFROG);
      quick_menu_draw_tab(&surface, 240, 38, 70, "State",
         page == QUICK_MENU_PAGE_CONFIRM);
      if (page == QUICK_MENU_PAGE_CONFIRM)
         unifrog_gfx_draw_text(&surface, 14, 64,
            confirm_action == QUICK_MENU_CONFIRM_LOAD ?
               "Load selected state?" : "Save selected state?",
            UNIFROG_RGB565(230, 238, 240), 1);
      for (unsigned i = 0; i < visible_rows; i++) {
         unsigned row = first_row + i;
         quick_menu_draw_row(&surface,
            (page == QUICK_MENU_PAGE_CONFIRM ? 90 : 66) + (int)i * 18,
            labels[row], detail[row], row == selected);
      }
      if (snapshot.status[0])
         unifrog_gfx_draw_text(&surface, 12, (int)surface.height - 34,
            snapshot.status, UNIFROG_RGB565(205, 240, 235), 1);
      unifrog_gfx_fill_rect(&surface, 0, (int)surface.height - 22,
         surface.width, 22, UNIFROG_RGB565(22, 29, 39));
      unifrog_gfx_fill_rect(&surface, 0, (int)surface.height - 23,
         surface.width, 1, UNIFROG_RGB565(120, 214, 189));
      unifrog_gfx_draw_text(&surface, 12, (int)surface.height - 15,
         footer, UNIFROG_RGB565(139, 154, 160), 1);
      quick_menu_present(draw_buffer);

      unifrog_input_save_previous();
      unifrog_input_poll_with_wireless_divisor(1);
      unifrog_libretro_session_note_buttons(unifrog_input_buttons());
      buttons = unifrog_input_menu_buttons();
      now_ms = unifrog_perf_time_ms();

      if (!input_ready) {
         previous = buttons;
         if ((buttons & action_buttons) == 0) {
            input_ready = 1;
            entry_combo_released = 1;
            repeat_button = 0;
            next_repeat_ms = 0;
            printf("unifrog quick_menu input_ready ms=%lu\n",
               (unsigned long)now_ms);
         }
         unifrog_perf_delay_us(16000u);
         continue;
      }

      pressed = buttons & ~previous;

      if ((buttons & entry_combo) != entry_combo)
         entry_combo_released = 1;

      if (!(buttons & (UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_UP) |
                       UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_DOWN)))) {
         repeat_button = 0;
         next_repeat_ms = 0;
      }
      if ((pressed & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_UP)) ||
          (repeat_button == UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_UP) &&
           now_ms >= next_repeat_ms)) {
         selected = selected == 0 ? row_count - 1u : selected - 1u;
         repeat_button = UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_UP);
         next_repeat_ms = now_ms + ((pressed & repeat_button) ? 320u : 90u);
      }
      if ((pressed & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_DOWN)) ||
          (repeat_button == UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_DOWN) &&
           now_ms >= next_repeat_ms)) {
         selected++;
         if (selected >= row_count)
            selected = 0;
         repeat_button = UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_DOWN);
         next_repeat_ms = now_ms + ((pressed & repeat_button) ? 320u : 90u);
      }
      if (pressed & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_LEFT)) {
         if (page == QUICK_MENU_PAGE_CONFIRM &&
             selected == QUICK_MENU_CONFIRM_YES) {
            if (confirm_action == QUICK_MENU_CONFIRM_LOAD)
               load_slot = load_slot == 0 ?
                  UNIFROG_LIBRETRO_SESSION_STATE_SLOTS - 1u :
                  load_slot - 1u;
            else
               save_slot = save_slot == 0 ?
                  UNIFROG_LIBRETRO_SESSION_STATE_SLOTS - 1u :
                  save_slot - 1u;
         } else if (page == QUICK_MENU_PAGE_MAIN) {
            if (selected == QUICK_MENU_MAIN_SAVE)
               save_slot = save_slot == 0 ?
                  UNIFROG_LIBRETRO_SESSION_STATE_SLOTS - 1u :
                  save_slot - 1u;
            else if (selected == QUICK_MENU_MAIN_LOAD)
               load_slot = load_slot == 0 ?
                  UNIFROG_LIBRETRO_SESSION_STATE_SLOTS - 1u :
                  load_slot - 1u;
         } else if (page == QUICK_MENU_PAGE_UNIFROG &&
               selected == QUICK_MENU_UNIFROG_FAST_FORWARD)
            (void)unifrog_libretro_session_adjust(
               UNIFROG_LIBRETRO_SESSION_FAST_FORWARD, -1);
         else if (page == QUICK_MENU_PAGE_UNIFROG &&
               selected == QUICK_MENU_UNIFROG_FRAMESKIP)
            (void)unifrog_libretro_session_adjust(
               UNIFROG_LIBRETRO_SESSION_FRAMESKIP, -1);
         else if (page == QUICK_MENU_PAGE_UNIFROG &&
               selected == QUICK_MENU_UNIFROG_AUDIO)
            (void)unifrog_libretro_session_adjust(
               UNIFROG_LIBRETRO_SESSION_AUDIO, -1);
         else if (page == QUICK_MENU_PAGE_UNIFROG &&
               selected == QUICK_MENU_UNIFROG_DISPLAY)
            (void)unifrog_libretro_session_adjust(
               UNIFROG_LIBRETRO_SESSION_DISPLAY, -1);
         else if (page == QUICK_MENU_PAGE_UNIFROG &&
               selected == QUICK_MENU_UNIFROG_KEYMAP)
            (void)unifrog_libretro_session_adjust(
               UNIFROG_LIBRETRO_SESSION_INPUT_PROFILE, -1);
         else if (page == QUICK_MENU_PAGE_UNIFROG &&
               selected == QUICK_MENU_UNIFROG_CPU)
            (void)unifrog_libretro_session_adjust(
               UNIFROG_LIBRETRO_SESSION_CPU, -1);
         else if (page == QUICK_MENU_PAGE_UNIFROG &&
               selected == QUICK_MENU_UNIFROG_GE)
            (void)unifrog_libretro_session_adjust(
               UNIFROG_LIBRETRO_SESSION_GE, -1);
         else if (page == QUICK_MENU_PAGE_UNIFROG &&
               selected == QUICK_MENU_UNIFROG_BACKLIGHT)
            (void)unifrog_libretro_session_adjust(
               UNIFROG_LIBRETRO_SESSION_BACKLIGHT, -1);
         else if (page == QUICK_MENU_PAGE_UNIFROG &&
               selected == QUICK_MENU_UNIFROG_RTC_OFFSET)
            (void)unifrog_libretro_session_adjust(
               UNIFROG_LIBRETRO_SESSION_RTC_OFFSET, -1);
         else if (page == QUICK_MENU_PAGE_CORE &&
               selected >= QUICK_MENU_CORE_FIXED_COUNT)
            quick_menu_cycle_core_option(
               selected - QUICK_MENU_CORE_FIXED_COUNT, -1);
      }
      if (pressed & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_RIGHT)) {
         if (page == QUICK_MENU_PAGE_CONFIRM &&
             selected == QUICK_MENU_CONFIRM_YES) {
            unsigned *slot = confirm_action == QUICK_MENU_CONFIRM_LOAD ?
               &load_slot : &save_slot;
            (*slot)++;
            if (*slot >= UNIFROG_LIBRETRO_SESSION_STATE_SLOTS)
               *slot = 0;
         } else if (page == QUICK_MENU_PAGE_MAIN) {
            if (selected == QUICK_MENU_MAIN_SAVE) {
               save_slot++;
               if (save_slot >= UNIFROG_LIBRETRO_SESSION_STATE_SLOTS)
                  save_slot = 0;
            } else if (selected == QUICK_MENU_MAIN_LOAD) {
               load_slot++;
               if (load_slot >= UNIFROG_LIBRETRO_SESSION_STATE_SLOTS)
                  load_slot = 0;
            }
         } else if (page == QUICK_MENU_PAGE_UNIFROG &&
               selected == QUICK_MENU_UNIFROG_FAST_FORWARD)
            (void)unifrog_libretro_session_adjust(
               UNIFROG_LIBRETRO_SESSION_FAST_FORWARD, 1);
         else if (page == QUICK_MENU_PAGE_UNIFROG &&
               selected == QUICK_MENU_UNIFROG_FRAMESKIP)
            (void)unifrog_libretro_session_adjust(
               UNIFROG_LIBRETRO_SESSION_FRAMESKIP, 1);
         else if (page == QUICK_MENU_PAGE_UNIFROG &&
               selected == QUICK_MENU_UNIFROG_AUDIO)
            (void)unifrog_libretro_session_adjust(
               UNIFROG_LIBRETRO_SESSION_AUDIO, 1);
         else if (page == QUICK_MENU_PAGE_UNIFROG &&
               selected == QUICK_MENU_UNIFROG_DISPLAY)
            (void)unifrog_libretro_session_adjust(
               UNIFROG_LIBRETRO_SESSION_DISPLAY, 1);
         else if (page == QUICK_MENU_PAGE_UNIFROG &&
               selected == QUICK_MENU_UNIFROG_KEYMAP)
            (void)unifrog_libretro_session_adjust(
               UNIFROG_LIBRETRO_SESSION_INPUT_PROFILE, 1);
         else if (page == QUICK_MENU_PAGE_UNIFROG &&
               selected == QUICK_MENU_UNIFROG_CPU)
            (void)unifrog_libretro_session_adjust(
               UNIFROG_LIBRETRO_SESSION_CPU, 1);
         else if (page == QUICK_MENU_PAGE_UNIFROG &&
               selected == QUICK_MENU_UNIFROG_GE)
            (void)unifrog_libretro_session_adjust(
               UNIFROG_LIBRETRO_SESSION_GE, 1);
         else if (page == QUICK_MENU_PAGE_UNIFROG &&
               selected == QUICK_MENU_UNIFROG_BACKLIGHT)
            (void)unifrog_libretro_session_adjust(
               UNIFROG_LIBRETRO_SESSION_BACKLIGHT, 1);
         else if (page == QUICK_MENU_PAGE_UNIFROG &&
               selected == QUICK_MENU_UNIFROG_RTC_OFFSET)
            (void)unifrog_libretro_session_adjust(
               UNIFROG_LIBRETRO_SESSION_RTC_OFFSET, 1);
         else if (page == QUICK_MENU_PAGE_CORE &&
               selected >= QUICK_MENU_CORE_FIXED_COUNT)
            quick_menu_cycle_core_option(
               selected - QUICK_MENU_CORE_FIXED_COUNT, 1);
      }
      if (entry_combo_released &&
          (pressed & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_SELECT)) &&
          (buttons & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_START)))
         break;
      if (pressed & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_B)) {
         if (page != QUICK_MENU_PAGE_MAIN) {
            page = previous_page;
            selected = previous_selected;
            confirm_action = QUICK_MENU_CONFIRM_NONE;
            previous = buttons;
            unifrog_perf_delay_us(16000u);
            continue;
         }
         break;
      }
      if (pressed & UNIFROG_BUTTON_MASK(UNIFROG_BUTTON_A)) {
         if (page == QUICK_MENU_PAGE_MAIN) {
            if (selected == QUICK_MENU_MAIN_RESUME) {
               break;
            } else if (selected == QUICK_MENU_MAIN_SAVE) {
               previous_page = page;
               previous_selected = selected;
               confirm_action = QUICK_MENU_CONFIRM_SAVE;
               page = QUICK_MENU_PAGE_CONFIRM;
               selected = QUICK_MENU_CONFIRM_YES;
               {
                  char status[UNIFROG_LIBRETRO_SESSION_STATUS_MAX];
                  snprintf(status, sizeof(status), "Save to slot %u",
                     save_slot + 1u);
                  unifrog_libretro_session_status(status);
               }
            } else if (selected == QUICK_MENU_MAIN_LOAD) {
               previous_page = page;
               previous_selected = selected;
               confirm_action = QUICK_MENU_CONFIRM_LOAD;
               page = QUICK_MENU_PAGE_CONFIRM;
               selected = QUICK_MENU_CONFIRM_YES;
               {
                  char status[UNIFROG_LIBRETRO_SESSION_STATUS_MAX];
                  snprintf(status, sizeof(status), "Load from slot %u",
                     load_slot + 1u);
                  unifrog_libretro_session_status(status);
               }
            } else if (selected == QUICK_MENU_MAIN_CORE) {
               previous_page = page;
               previous_selected = selected;
               page = QUICK_MENU_PAGE_CORE;
               selected = QUICK_MENU_CORE_SAVE_CORE;
            } else if (selected == QUICK_MENU_MAIN_UNIFROG) {
               previous_page = page;
               previous_selected = selected;
               page = QUICK_MENU_PAGE_UNIFROG;
               selected = QUICK_MENU_UNIFROG_FAST_FORWARD;
            } else if (selected == QUICK_MENU_MAIN_RETURN) {
               return_to_frontend = 1;
               break;
            }
         } else if (page == QUICK_MENU_PAGE_CORE) {
            if (selected == QUICK_MENU_CORE_BACK) {
               page = QUICK_MENU_PAGE_MAIN;
               selected = QUICK_MENU_MAIN_CORE;
            } else if (selected == QUICK_MENU_CORE_SAVE_CORE) {
               int ret = unifrog_libretro_session_save_core_options(
                  UNIFROG_LIBRETRO_SESSION_SCOPE_CORE);

               quick_menu_persist_status("Core options saved", ret);
            } else if (selected == QUICK_MENU_CORE_SAVE_GAME) {
               int ret = unifrog_libretro_session_save_core_options(
                  UNIFROG_LIBRETRO_SESSION_SCOPE_CONTENT);

               quick_menu_persist_status("Game options saved", ret);
            } else if (selected == QUICK_MENU_CORE_CLEAR_GAME) {
               int ret = unifrog_libretro_session_clear_core_options(
                  UNIFROG_LIBRETRO_SESSION_SCOPE_CONTENT);

               quick_menu_persist_status("Saved game options cleared", ret);
            } else if (selected == QUICK_MENU_CORE_CLEAR_CORE) {
               int ret = unifrog_libretro_session_clear_core_options(
                  UNIFROG_LIBRETRO_SESSION_SCOPE_CORE);

               quick_menu_persist_status("Saved core options cleared", ret);
            } else if (selected >= QUICK_MENU_CORE_FIXED_COUNT) {
               quick_menu_cycle_core_option(
                  selected - QUICK_MENU_CORE_FIXED_COUNT, 1);
            }
         } else if (page == QUICK_MENU_PAGE_UNIFROG) {
            if (selected == QUICK_MENU_UNIFROG_BACK) {
               page = QUICK_MENU_PAGE_MAIN;
               selected = QUICK_MENU_MAIN_UNIFROG;
            } else if (selected == QUICK_MENU_UNIFROG_FAST_FORWARD) {
               (void)unifrog_libretro_session_adjust(
                  UNIFROG_LIBRETRO_SESSION_FAST_FORWARD, 1);
            } else if (selected == QUICK_MENU_UNIFROG_FRAMESKIP) {
               (void)unifrog_libretro_session_adjust(
                  UNIFROG_LIBRETRO_SESSION_FRAMESKIP, 1);
            } else if (selected == QUICK_MENU_UNIFROG_AUDIO) {
               (void)unifrog_libretro_session_adjust(
                  UNIFROG_LIBRETRO_SESSION_AUDIO, 1);
            } else if (selected == QUICK_MENU_UNIFROG_DISPLAY) {
               (void)unifrog_libretro_session_adjust(
                  UNIFROG_LIBRETRO_SESSION_DISPLAY, 1);
            } else if (selected == QUICK_MENU_UNIFROG_KEYMAP) {
               (void)unifrog_libretro_session_adjust(
                  UNIFROG_LIBRETRO_SESSION_INPUT_PROFILE, 1);
            } else if (selected == QUICK_MENU_UNIFROG_CPU) {
               (void)unifrog_libretro_session_adjust(
                  UNIFROG_LIBRETRO_SESSION_CPU, 1);
            } else if (selected == QUICK_MENU_UNIFROG_GE) {
               (void)unifrog_libretro_session_adjust(
                  UNIFROG_LIBRETRO_SESSION_GE, 1);
            } else if (selected == QUICK_MENU_UNIFROG_BACKLIGHT) {
               (void)unifrog_libretro_session_adjust(
                  UNIFROG_LIBRETRO_SESSION_BACKLIGHT, 1);
            } else if (selected == QUICK_MENU_UNIFROG_RTC_OFFSET) {
               (void)unifrog_libretro_session_adjust(
                  UNIFROG_LIBRETRO_SESSION_RTC_OFFSET, 1);
            } else if (selected == QUICK_MENU_UNIFROG_SAVE_GAME) {
               int ret = unifrog_libretro_session_save_settings(
                  UNIFROG_LIBRETRO_SESSION_SCOPE_CONTENT);

               quick_menu_persist_status("Game settings saved", ret);
            } else if (selected == QUICK_MENU_UNIFROG_SAVE_CORE) {
               int ret = unifrog_libretro_session_save_settings(
                  UNIFROG_LIBRETRO_SESSION_SCOPE_CORE);

               quick_menu_persist_status("Core settings saved", ret);
            } else if (selected == QUICK_MENU_UNIFROG_CLEAR_GAME) {
               int ret = unifrog_libretro_session_clear_settings(
                  UNIFROG_LIBRETRO_SESSION_SCOPE_CONTENT);

               quick_menu_persist_status("Saved game settings cleared", ret);
            } else if (selected == QUICK_MENU_UNIFROG_CLEAR_CORE) {
               int ret = unifrog_libretro_session_clear_settings(
                  UNIFROG_LIBRETRO_SESSION_SCOPE_CORE);

               quick_menu_persist_status("Saved core settings cleared", ret);
            }
         } else {
            if (selected == QUICK_MENU_CONFIRM_CANCEL) {
               page = previous_page;
               selected = previous_selected;
               confirm_action = QUICK_MENU_CONFIRM_NONE;
            } else if (confirm_action == QUICK_MENU_CONFIRM_SAVE) {
               int ret;

               printf("unifrog quick_menu confirm save slot=%u\n",
                  save_slot);
               ret = unifrog_libretro_session_state(
                  UNIFROG_LIBRETRO_SESSION_STATE_SAVE, save_slot);
               if (ret >= 0)
                  break;
               page = previous_page;
               selected = previous_selected;
            } else if (confirm_action == QUICK_MENU_CONFIRM_LOAD) {
               int ret;

               printf("unifrog quick_menu confirm load slot=%u\n",
                  load_slot);
               ret = unifrog_libretro_session_state(
                  UNIFROG_LIBRETRO_SESSION_STATE_LOAD, load_slot);
               if (ret >= 0)
                  break;
               page = previous_page;
               selected = previous_selected;
            }
         }
      }
      previous = buttons;
      unifrog_perf_delay_us(16000u);
   }

   printf("unifrog quick_menu done action=%d\n", return_to_frontend);
   return unifrog_libretro_session_menu_finish(return_to_frontend);
}
