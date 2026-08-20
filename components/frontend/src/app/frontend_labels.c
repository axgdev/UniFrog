#include "frontend_internal.h"

const char *frameskip_label(int frameskip)
{
   switch (frameskip) {
   case UNIFROG_LIBRETRO_FRAMESKIP_OFF:
      return "off";
   case UNIFROG_LIBRETRO_FRAMESKIP_AUTO:
      return "auto";
   case UNIFROG_LIBRETRO_FRAMESKIP_FIXED_1:
      return "fixed 1";
   case UNIFROG_LIBRETRO_FRAMESKIP_FIXED_2:
      return "fixed 2";
   default:
      return "unknown";
   }
}

const char *display_label(int display_mode)
{
   switch (display_mode) {
   case UNIFROG_LIBRETRO_DISPLAY_FIT:
      return "fit";
   case UNIFROG_LIBRETRO_DISPLAY_STRETCH:
      return "stretch";
   case UNIFROG_LIBRETRO_DISPLAY_ORIGINAL:
      return "original";
   default:
      return "unknown";
   }
}

const char *framebuffer_label(int format)
{
   return format == UNIFROG_LIBRETRO_FB_XRGB8888 ? "XRGB8888" : "RGB565";
}

const char *input_profile_label(int profile)
{
   switch (profile) {
   case UNIFROG_LIBRETRO_INPUT_DEFAULT:
      return "default";
   case UNIFROG_LIBRETRO_INPUT_RETROARCH:
      return "retroarch";
   case UNIFROG_LIBRETRO_INPUT_GENESIS:
      return "genesis";
   case UNIFROG_LIBRETRO_INPUT_SWAP_AB:
      return "swap A/B";
   case UNIFROG_LIBRETRO_INPUT_SWAP_XY:
      return "swap X/Y";
   default:
      return "unknown";
   }
}

unsigned clamp_state_slot(unsigned slot)
{
   return slot < 10u ? slot : 0u;
}

const char *state_slot_label(unsigned slot)
{
   static char label[16];

   snprintf(label, sizeof(label), "slot %u", clamp_state_slot(slot));
   return label;
}

const char *ge_clock_label(int ge_clock)
{
   switch (ge_clock) {
   case -1:
      return "auto";
   case 0:
      return "198 MHz";
   case 1:
      return "148 MHz";
   case 2:
      return "225 MHz";
   case 3:
      return "238 MHz";
   default:
      return "custom";
   }
}

const char *on_off_label(int value)
{
   return value ? "on" : "off";
}
