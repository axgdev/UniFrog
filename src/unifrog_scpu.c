#include <unifrog/scpu.h>

#include <string.h>
#include <sys/unistd.h>

#include <kernel/io.h>

#define MSYSIO_BASE 0xb8800000u
#define HC1512_CHIP_ID 0x1512u

static uint32_t scpu_mhz_to_mctrl2(unsigned mhz)
{
   return ((((mhz * 10u) - 27u) / 27u) | 0x8000u);
}

static unsigned scpu_mctrl2_to_mhz(uint32_t mctrl)
{
   uint32_t n = mctrl & 0xffffu;

   if (n & 0x8000u)
      n &= 0x7fffu;

   return (unsigned)(((n * 27u) + 9u) / 10u);
}

static unsigned scpu_selector_to_mhz(unsigned selector)
{
   switch (selector) {
   case 0:
      return 594;
   case 1:
      return 396;
   case 2:
      return 297;
   case 3:
   case 4:
   case 5:
   case 6:
      return 198;
   default:
      return 0;
   }
}

static unsigned scpu_current_selector(void)
{
   return (REG32_READ(MSYSIO_BASE + 0x74) >> 8) & 0x7u;
}

static unsigned scpu_pll_enabled(void)
{
   return (REG32_READ(MSYSIO_BASE + 0x7c) >> 7) & 0x1u;
}

static void scpu_apply_hc1512(unsigned selector, unsigned pll_mhz)
{
   if (selector == 7) {
      REG32_SET_FIELD2(MSYSIO_BASE + 0x380, 16, 16,
         scpu_mhz_to_mctrl2(pll_mhz));
      usleep(1000);
      REG32_SET_FIELD2(MSYSIO_BASE + 0x74, 8, 3, 7);
      REG32_SET_FIELD2(MSYSIO_BASE + 0x7c, 7, 1, 1);
   } else {
      REG32_SET_FIELD2(MSYSIO_BASE + 0x7c, 7, 1, 0);
      REG32_SET_FIELD2(MSYSIO_BASE + 0x74, 8, 3, selector);
   }
   REG32_SET_FIELD2(MSYSIO_BASE + 0x74, 22, 1, 1);
   usleep(5000);
}

int unifrog_scpu_supported(void)
{
   return REG32_GET_FIELD2(MSYSIO_BASE + 0x0, 16, 16) == HC1512_CHIP_ID;
}

unsigned unifrog_scpu_current_mhz(void)
{
   unsigned selector;

   if (!unifrog_scpu_supported())
      return 0;

   selector = scpu_current_selector();
   if (selector == 7 && scpu_pll_enabled())
      return scpu_mctrl2_to_mhz(REG32_READ(MSYSIO_BASE + 0x380) >> 16);

   return scpu_selector_to_mhz(selector);
}

int unifrog_scpu_capture(struct unifrog_scpu_clock *clock)
{
   if (!clock)
      return -1;

   memset(clock, 0, sizeof(*clock));
   if (!unifrog_scpu_supported())
      return -1;

   clock->reg074 = REG32_READ(MSYSIO_BASE + 0x74);
   clock->reg07c = REG32_READ(MSYSIO_BASE + 0x7c);
   clock->reg380 = REG32_READ(MSYSIO_BASE + 0x380);
   clock->selector = scpu_current_selector();
   clock->pll_enabled = scpu_pll_enabled();
   clock->mhz = unifrog_scpu_current_mhz();
   clock->valid = 1;
   return 0;
}

int unifrog_scpu_apply_mhz(unsigned mhz)
{
   if (!unifrog_scpu_supported())
      return -1;

   switch (mhz) {
   case 198:
      scpu_apply_hc1512(3, mhz);
      return 0;
   case 297:
      scpu_apply_hc1512(2, mhz);
      return 0;
   case 396:
      scpu_apply_hc1512(1, mhz);
      return 0;
   case 594:
      scpu_apply_hc1512(0, mhz);
      return 0;
   case 702:
   case 756:
   case 810:
   case 864:
   case 918:
      scpu_apply_hc1512(7, mhz);
      return 0;
   case 808:
      scpu_apply_hc1512(7, 810);
      return 0;
   default:
      return -1;
   }
}

int unifrog_scpu_restore(const struct unifrog_scpu_clock *clock)
{
   if (!clock || !clock->valid || !unifrog_scpu_supported())
      return -1;

   REG32_WRITE(MSYSIO_BASE + 0x380, clock->reg380);
   REG32_WRITE(MSYSIO_BASE + 0x7c, clock->reg07c);
   REG32_WRITE(MSYSIO_BASE + 0x74, clock->reg074);
   usleep(5000);
   return 0;
}
