#include <unifrog/device.h>

#include <stdint.h>
#include <string.h>
#include <strings.h>

#include <fastboot/handoff.h>
#include <unifrog/boot_trace.h>
#include <unifrog/log.h>

#define LCD_ID_GB300 0x009306ul
#define LCD_ID_DY14 0x009307ul

extern unsigned long sf2000_lcd_panel_id(void) __attribute__((weak));

static enum unifrog_device_board board_override =
   UNIFROG_DEVICE_BOARD_AUTO;
static enum unifrog_device_board boot_board =
   UNIFROG_DEVICE_BOARD_AUTO;
static enum unifrog_device_board observed_board =
   UNIFROG_DEVICE_BOARD_AUTO;
static uint32_t boot_board_marks;
static uint32_t boot_board_scores;
static int boot_board_checked;

unsigned long unifrog_device_lcd_panel_id(void)
{
   return sf2000_lcd_panel_id ? sf2000_lcd_panel_id() : 0;
}

enum unifrog_device_panel unifrog_device_panel(void)
{
   unsigned long id = unifrog_device_lcd_panel_id();

   if (id == LCD_ID_GB300 || id == LCD_ID_DY14)
      return UNIFROG_DEVICE_PANEL_GB300;
   if (id)
      return UNIFROG_DEVICE_PANEL_SF2000;
   return UNIFROG_DEVICE_PANEL_UNKNOWN;
}

const char *unifrog_device_panel_name(enum unifrog_device_panel panel)
{
   switch (panel) {
   case UNIFROG_DEVICE_PANEL_GB300:
      return "gb300";
   case UNIFROG_DEVICE_PANEL_SF2000:
      return "sf2000";
   default:
      return "unknown";
   }
}

static enum unifrog_device_board panel_board_hint(void)
{
   return unifrog_device_panel() == UNIFROG_DEVICE_PANEL_GB300 ?
      UNIFROG_DEVICE_BOARD_GB300 : UNIFROG_DEVICE_BOARD_SF2000;
}

static enum unifrog_device_board board_from_fastboot(uint32_t board)
{
   switch (board) {
   case FASTBOOT_DEVICE_BOARD_SF2000:
      return UNIFROG_DEVICE_BOARD_SF2000;
   case FASTBOOT_DEVICE_BOARD_GB300:
      return UNIFROG_DEVICE_BOARD_GB300;
   default:
      return UNIFROG_DEVICE_BOARD_AUTO;
   }
}

const char *unifrog_device_board_name(enum unifrog_device_board board)
{
   switch (board) {
   case UNIFROG_DEVICE_BOARD_SF2000:
      return "sf2000";
   case UNIFROG_DEVICE_BOARD_GB300:
      return "gb300";
   default:
      return "auto";
   }
}

const char *unifrog_device_variant_name(void)
{
   enum unifrog_device_board board = unifrog_device_board();
   enum unifrog_device_panel panel = unifrog_device_panel();

   if (board == UNIFROG_DEVICE_BOARD_SF2000 &&
       panel == UNIFROG_DEVICE_PANEL_GB300)
      return "sf2000-gb300-screen";
   if (board == UNIFROG_DEVICE_BOARD_GB300 &&
       panel == UNIFROG_DEVICE_PANEL_SF2000)
      return "gb300-sf2000-screen";
   if (board == UNIFROG_DEVICE_BOARD_GB300)
      return "gb300";
   if (board == UNIFROG_DEVICE_BOARD_SF2000)
      return "sf2000";
   return "auto";
}

static void check_boot_board(void)
{
   uint32_t fastboot_board = FASTBOOT_DEVICE_BOARD_UNKNOWN;
   uint32_t marks = 0;
   uint32_t scores = 0;

   if (boot_board_checked)
      return;
   boot_board_checked = 1;

   if (!unifrog_boot_trace_fastboot_board(&fastboot_board, &marks, &scores))
      return;

   boot_board = board_from_fastboot(fastboot_board);
   boot_board_marks = marks;
   boot_board_scores = scores;
   if (boot_board == UNIFROG_DEVICE_BOARD_AUTO)
      return;

   unifrog_log("unifrog device boot_board=%s marks=0x%08lx score_sf=%lu score_gb=%lu override=%s panel=%s lcd=0x%06lx variant=%s\n",
      unifrog_device_board_name(boot_board),
      (unsigned long)boot_board_marks,
      (unsigned long)((boot_board_scores >> 16) & 0xffu),
      (unsigned long)(boot_board_scores & 0xffu),
      unifrog_device_board_name(board_override),
      unifrog_device_panel_name(unifrog_device_panel()),
      unifrog_device_lcd_panel_id(),
      unifrog_device_variant_name());
}

static enum unifrog_device_board parse_board_name(const char *name)
{
   if (!name || !name[0] || strcasecmp(name, "auto") == 0)
      return UNIFROG_DEVICE_BOARD_AUTO;
   if (strcasecmp(name, "sf2000") == 0 ||
       strcasecmp(name, "sf2000-gb300-screen") == 0 ||
       strcasecmp(name, "sf2000_gb300_screen") == 0)
      return UNIFROG_DEVICE_BOARD_SF2000;
   if (strcasecmp(name, "gb300") == 0 ||
       strcasecmp(name, "gb300-sf2000-screen") == 0 ||
       strcasecmp(name, "gb300_sf2000_screen") == 0)
      return UNIFROG_DEVICE_BOARD_GB300;
   return UNIFROG_DEVICE_BOARD_AUTO;
}

int unifrog_device_set_board_override(const char *name)
{
   enum unifrog_device_board board = parse_board_name(name);

   if (board == UNIFROG_DEVICE_BOARD_AUTO && name && name[0] &&
       strcasecmp(name, "auto") != 0) {
      unifrog_log("unifrog device board_override invalid=%s using=auto\n",
         name);
      return -1;
   }
   if (board_override != board) {
      board_override = board;
      unifrog_log("unifrog device board_override=%s panel=%s lcd=0x%06lx observed=%s\n",
         unifrog_device_board_name(board_override),
         unifrog_device_panel_name(unifrog_device_panel()),
         unifrog_device_lcd_panel_id(),
         unifrog_device_board_name(observed_board));
   }
   return 0;
}

const char *unifrog_device_board_override_name(void)
{
   return unifrog_device_board_name(board_override);
}

void unifrog_device_note_input_profile(int uses_gb300_stock_bits,
   const char *reason)
{
   enum unifrog_device_board board = uses_gb300_stock_bits ?
      UNIFROG_DEVICE_BOARD_GB300 : UNIFROG_DEVICE_BOARD_SF2000;

   if (observed_board == board)
      return;
   observed_board = board;
   unifrog_log("unifrog device observed_board=%s reason=%s override=%s panel=%s lcd=0x%06lx\n",
      unifrog_device_board_name(observed_board), reason ? reason : "",
      unifrog_device_board_name(board_override),
      unifrog_device_panel_name(unifrog_device_panel()),
      unifrog_device_lcd_panel_id());
}

enum unifrog_device_board unifrog_device_board(void)
{
   check_boot_board();
   if (board_override != UNIFROG_DEVICE_BOARD_AUTO)
      return board_override;
   if (observed_board != UNIFROG_DEVICE_BOARD_AUTO)
      return observed_board;
   if (boot_board != UNIFROG_DEVICE_BOARD_AUTO)
      return boot_board;
   return panel_board_hint();
}

int unifrog_device_uses_gb300_quirks(void)
{
   return unifrog_device_board() == UNIFROG_DEVICE_BOARD_GB300;
}
