#include <stdint.h>
#include <stddef.h>

#include "handoff.h"

#define ENTRY_ADDR ((uint8_t *)0x80001000u)
#define RAW_LOAD_ADDR ENTRY_ADDR
#define ASD_LOAD_ADDR ((uint8_t *)0x80000200u)
#define ASD_STAGE_ADDR ((uint8_t *)0x87000000u)
#define CHUNK_ADDR ((uint8_t *)FASTBOOT_CHUNK_ADDR)
#define CHUNK_SIZE (64u * 1024u)
#define ASD_SKIP 0x200u

#define FA_READ 0x01u

#ifndef FASTBOOT_BOOT_LOGO
#define FASTBOOT_BOOT_LOGO 1
#endif

#define FASTBOOT_DELAY_LOOPS_PER_MS 60000u
#define FASTBOOT_LCD_WIDTH 320u
#define FASTBOOT_LCD_HEIGHT 240u
#define FASTBOOT_LOGO_WIDTH 256u
#define FASTBOOT_LOGO_HEIGHT 100u
#define FASTBOOT_LOGO_X ((FASTBOOT_LCD_WIDTH - FASTBOOT_LOGO_WIDTH) / 2u)
#define FASTBOOT_LOGO_Y ((FASTBOOT_LCD_HEIGHT - FASTBOOT_LOGO_HEIGHT) / 2u)
#define FASTBOOT_LOGO_BG 0x0861u

#define PINMUXL_BASE 0xb88004a0u
#define PINMUXT_BASE 0xb8800500u
#define GPIOL_OUTPUT_REG 0xb8800054u
#define GPIOL_DIR_REG 0xb8800058u
#define GPIOT_OUTPUT_REG 0xb8800354u
#define GPIOT_DIR_REG 0xb8800358u

#define LCD_L_RESET (1u << 1)
#define LCD_L_D0_D4 (0x1fu << 2)
#define LCD_L_WR (1u << 7)
#define LCD_L_CS (1u << 10)
#define LCD_T_RD (1u << 0)
#define LCD_T_RS (1u << 1)
#define LCD_T_D11_D15 (0x1fu << 2)
#define LCD_T_D5_D10 (0x3fu << 9)
#define LCD_L_OUTPUTS (LCD_L_RESET | LCD_L_D0_D4 | LCD_L_WR | LCD_L_CS)
#define LCD_T_OUTPUTS (LCD_T_RD | LCD_T_RS | LCD_T_D11_D15 | LCD_T_D5_D10)

#define ST7789_SLPOUT 0x11u
#define ST7789_NORON 0x13u
#define ST7789_INVON 0x21u
#define ST7789_DISPON 0x29u
#define ST7789_CASET 0x2au
#define ST7789_RASET 0x2bu
#define ST7789_RAMWR 0x2cu
#define ST7789_TEON 0x35u
#define ST7789_MADCTL 0x36u
#define ST7789_COLMOD 0x3au

#if FASTBOOT_BOOT_LOGO
#include "../../assets/boot/unifrog-logo-rgb565.inc"
#endif

typedef unsigned int UINT;
typedef unsigned char BYTE;
typedef unsigned short WORD;
typedef unsigned int DWORD;
typedef unsigned long long QWORD;

typedef struct {
	BYTE fs_type;
	BYTE pdrv;
	BYTE n_fats;
	BYTE wflag;
	BYTE fsi_flag;
	BYTE reserved0;
	WORD id;
	WORD n_rootdir;
	WORD csize;
	void *lfnbuf;
	BYTE *dirbuf;
	DWORD last_clst;
	DWORD free_clst;
	DWORD cdir;
	DWORD cdc_scl;
	DWORD cdc_size;
	DWORD cdc_ofs;
	DWORD n_fatent;
	DWORD fsize;
	DWORD volbase;
	DWORD fatbase;
	DWORD dirbase;
	DWORD database;
	DWORD bitbase;
	DWORD winsect;
	BYTE win[512];
} FATFS;

typedef struct {
	FATFS *fs;
	WORD id;
	BYTE attr;
	BYTE stat;
	DWORD sclust;
	BYTE reserved0[4];
	QWORD objsize;
	DWORD n_cont;
	DWORD n_frag;
	DWORD c_scl;
	DWORD c_size;
	DWORD c_ofs;
	BYTE reserved1[4];
} FFOBJID;

typedef struct {
	FFOBJID obj;
	BYTE flag;
	BYTE err;
	BYTE reserved0[6];
	QWORD fptr;
	DWORD clust;
	DWORD sect;
	DWORD dir_sect;
	BYTE *dir_ptr;
	BYTE buf[512];
} FIL;

typedef int (*rom_printf_fn)(const char *fmt, ...);
typedef int (*rom_f_mount_fn)(FATFS *fs, const char *path, BYTE opt);
typedef int (*rom_f_open_fn)(FIL *fp, const char *path, BYTE mode);
typedef int (*rom_f_read_fn)(FIL *fp, void *buff, UINT btr, UINT *br);
typedef int (*rom_f_close_fn)(FIL *fp);
typedef void (*rom_cache_flush_fn)(void *addr, unsigned long len);
typedef void (*entry_fn)(void);

static rom_printf_fn const rom_printf = (rom_printf_fn)0x8101aa50u;
static rom_f_mount_fn const rom_f_mount = (rom_f_mount_fn)0x8101f044u;
static rom_f_open_fn const rom_f_open = (rom_f_open_fn)0x8101f0d4u;
static rom_f_read_fn const rom_f_read = (rom_f_read_fn)0x8101f548u;
static rom_f_close_fn const rom_f_close = (rom_f_close_fn)0x8101fa70u;
static rom_cache_flush_fn const rom_cache_flush = (rom_cache_flush_fn)0x810032f4u;

static FATFS fatfs;
static FIL file;
static int fastboot_logo_visible;

extern uint8_t __bss_start;
extern uint8_t __bss_end;

static void write_reg8(uint32_t addr, uint8_t value)
{
	*(volatile uint8_t *)(uintptr_t)addr = value;
}

static uint8_t read_reg8(uint32_t addr)
{
	return *(volatile uint8_t *)(uintptr_t)addr;
}

static uint32_t read_reg32(uint32_t addr)
{
	return *(volatile uint32_t *)(uintptr_t)addr;
}

static void write_reg32(uint32_t addr, uint32_t value)
{
	*(volatile uint32_t *)(uintptr_t)addr = value;
}

static void set_reg32_bits(uint32_t addr, uint32_t mask)
{
	*(volatile uint32_t *)(uintptr_t)addr = read_reg32(addr) | mask;
}

static void clear_reg32_bits(uint32_t addr, uint32_t mask)
{
	*(volatile uint32_t *)(uintptr_t)addr = read_reg32(addr) & ~mask;
}

static uint32_t boot_ticks(void)
{
	uint32_t count;

	__asm__ volatile("mfc0 %0, $9" : "=r"(count));
	return count;
}

static void delay_ms(unsigned int ms)
{
	while (ms-- != 0) {
		volatile unsigned int i;

		for (i = 0; i < FASTBOOT_DELAY_LOOPS_PER_MS; i++)
			__asm__ volatile("nop");
	}
}

static void fastboot_backlight_off(void)
{
	/* Latch high before GPIO mux: R05 is an active-low backlight gate. */
	set_reg32_bits(FASTBOOT_GPIOR_OUTPUT_REG, FASTBOOT_BACKLIGHT_R05_MASK);
	set_reg32_bits(FASTBOOT_GPIOR_DIR_REG, FASTBOOT_BACKLIGHT_R05_MASK);
	write_reg8(FASTBOOT_PINMUX_R05_ADDR, 0);
}

static void fastboot_backlight_on_gpio(void)
{
	/* Latch low before GPIO mux: R05 is an active-low backlight gate. */
	clear_reg32_bits(FASTBOOT_GPIOR_OUTPUT_REG, FASTBOOT_BACKLIGHT_R05_MASK);
	set_reg32_bits(FASTBOOT_GPIOR_DIR_REG, FASTBOOT_BACKLIGHT_R05_MASK);
	write_reg8(FASTBOOT_PINMUX_R05_ADDR, 0);
}

static int fastboot_backlight_state(void)
{
	uint32_t state = read_reg8(FASTBOOT_PINMUX_R05_ADDR);

	if (read_reg32(FASTBOOT_GPIOR_DIR_REG) & FASTBOOT_BACKLIGHT_R05_MASK)
		state |= 0x100u;
	if (read_reg32(FASTBOOT_GPIOR_OUTPUT_REG) & FASTBOOT_BACKLIGHT_R05_MASK)
		state |= 0x200u;
	return (int)state;
}

static void fastboot_backlight_report(const char *tag)
{
	uint32_t state = (uint32_t)fastboot_backlight_state();

	rom_printf("fastboot: backlight %s state=0x%03x mux=%u dir=%u out=%u\n",
		tag ? tag : "",
		(unsigned int)state,
		(unsigned int)(state & 0xffu),
		(unsigned int)((state >> 8) & 1u),
		(unsigned int)((state >> 9) & 1u));
}

static unsigned int trace_path_hash(const char *path)
{
	unsigned int hash = 2166136261u;

	if (!path)
		return 0;
	while (*path) {
		hash ^= (unsigned char)*path++;
		hash *= 16777619u;
	}
	return hash;
}

static void boot_trace_reset(volatile struct fastboot_trace *trace)
{
	unsigned int i;

	trace->magic = FASTBOOT_TRACE_MAGIC;
	trace->version = FASTBOOT_TRACE_VERSION;
	trace->count = 0;
	trace->dropped = 0;
	for (i = 0; i < FASTBOOT_TRACE_ENTRIES; i++) {
		trace->entries[i].seq = 0;
		trace->entries[i].event = 0;
		trace->entries[i].arg0 = 0;
		trace->entries[i].arg1 = 0;
		trace->entries[i].arg2 = 0;
		trace->entries[i].r05_state = 0;
	}
}

static void boot_trace_note(unsigned int event, unsigned int arg0,
		unsigned int arg1, unsigned int arg2)
{
	volatile struct fastboot_trace *trace = FASTBOOT_TRACE_ADDR;
	unsigned int index;

	if (trace->magic != FASTBOOT_TRACE_MAGIC ||
			trace->version != FASTBOOT_TRACE_VERSION)
		boot_trace_reset(trace);

	if (trace->count >= FASTBOOT_TRACE_ENTRIES) {
		trace->dropped++;
		rom_cache_flush((void *)trace, sizeof(*trace));
		return;
	}

	index = trace->count++;
	trace->entries[index].seq = index;
	trace->entries[index].event = event;
	trace->entries[index].arg0 = arg0;
	trace->entries[index].arg1 = arg1;
	trace->entries[index].arg2 = arg2;
	trace->entries[index].r05_state = (uint32_t)fastboot_backlight_state();
	rom_cache_flush((void *)trace, sizeof(*trace));
}

#if FASTBOOT_BOOT_LOGO
static const uint8_t fastboot_st7789_init_sf2000[] = {
	1, ST7789_SLPOUT, 99,
	2, ST7789_MADCTL, 0x70, 0,
	2, ST7789_TEON, 0x00, 0,
	2, ST7789_COLMOD, 0x55, 0,
	4, 0xb1, 0x40, 0x04, 0x14, 0,
	6, 0xb2, 0x0c, 0x0c, 0x00, 0x33, 0x33, 0,
	2, 0xb7, 0x71, 0,
	2, 0xbb, 0x3b, 0,
	2, 0xc0, 0x2c, 0,
	2, 0xc2, 0x01, 0,
	2, 0xc3, 0x13, 0,
	2, 0xc4, 0x20, 0,
	2, 0xc6, 0x0f, 0,
	3, 0xd0, 0xa4, 0xa1, 0,
	2, 0xd6, 0xa1, 0,
	15, 0xe0, 0xd0, 0x06, 0x06, 0x0e, 0x0d, 0x06, 0x2f,
		0x3a, 0x47, 0x08, 0x15, 0x14, 0x2c, 0x33, 0,
	15, 0xe1, 0xd0, 0x06, 0x06, 0x0e, 0x0d, 0x06, 0x2f,
		0x3b, 0x47, 0x08, 0x15, 0x14, 0x2c, 0x33, 0,
	1, ST7789_INVON, 0,
	0
};

static void fastboot_lcd_set_l(uint32_t mask, int high)
{
	if (high)
		set_reg32_bits(GPIOL_OUTPUT_REG, mask);
	else
		clear_reg32_bits(GPIOL_OUTPUT_REG, mask);
}

static void fastboot_lcd_set_t(uint32_t mask, int high)
{
	if (high)
		set_reg32_bits(GPIOT_OUTPUT_REG, mask);
	else
		clear_reg32_bits(GPIOT_OUTPUT_REG, mask);
}

static void fastboot_lcd_pinmux_gpio(void)
{
	static const uint8_t lpins[] = { 1, 2, 3, 4, 5, 6, 7, 10 };
	static const uint8_t tpins[] = {
		0, 1, 2, 3, 4, 5, 6, 9, 10, 11, 12, 13, 14
	};
	unsigned int i;

	for (i = 0; i < sizeof(lpins); i++)
		write_reg8(PINMUXL_BASE + lpins[i], 0);
	for (i = 0; i < sizeof(tpins); i++)
		write_reg8(PINMUXT_BASE + tpins[i], 0);
}

static void fastboot_lcd_gpio_ready(void)
{
	fastboot_lcd_pinmux_gpio();
	set_reg32_bits(GPIOL_DIR_REG, LCD_L_OUTPUTS);
	set_reg32_bits(GPIOT_DIR_REG, LCD_T_OUTPUTS);
	set_reg32_bits(GPIOL_OUTPUT_REG, LCD_L_CS | LCD_L_WR | LCD_L_RESET);
	set_reg32_bits(GPIOT_OUTPUT_REG, LCD_T_RS | LCD_T_RD);
}

static void fastboot_lcd_write_bus(uint16_t value)
{
	uint32_t lout = read_reg32(GPIOL_OUTPUT_REG);
	uint32_t tout = read_reg32(GPIOT_OUTPUT_REG);

	lout = (lout & ~LCD_L_D0_D4) | ((uint32_t)(value & 0x001fu) << 2);
	tout = (tout & ~(LCD_T_D11_D15 | LCD_T_D5_D10)) |
		((uint32_t)(value & 0x07e0u) << 4) |
		((uint32_t)(value >> 9) & LCD_T_D11_D15);
	write_reg32(GPIOL_OUTPUT_REG, lout);
	write_reg32(GPIOT_OUTPUT_REG, tout);
}

static void fastboot_lcd_write16(uint16_t value, int is_data)
{
	fastboot_lcd_set_t(LCD_T_RS, is_data);
	fastboot_lcd_set_l(LCD_L_CS, 0);
	fastboot_lcd_write_bus(value);
	fastboot_lcd_set_l(LCD_L_WR, 0);
	fastboot_lcd_set_l(LCD_L_WR, 1);
	fastboot_lcd_set_l(LCD_L_CS, 1);
	fastboot_lcd_set_t(LCD_T_RS, 1);
}

static void fastboot_lcd_command(uint8_t command)
{
	fastboot_lcd_write16(command, 0);
}

static void fastboot_lcd_data(uint16_t data)
{
	fastboot_lcd_write16(data, 1);
}

static void fastboot_lcd_reset(void)
{
	fastboot_lcd_set_t(LCD_T_RS, 1);
	fastboot_lcd_set_l(LCD_L_WR, 1);
	fastboot_lcd_set_l(LCD_L_RESET, 1);
	delay_ms(10);
	fastboot_lcd_set_l(LCD_L_RESET, 0);
	delay_ms(20);
	fastboot_lcd_set_l(LCD_L_RESET, 1);
	delay_ms(120);
}

static void fastboot_lcd_apply_init(void)
{
	const uint8_t *p = fastboot_st7789_init_sf2000;

	clear_reg32_bits(0xb8800078u, 1u << 15);
	for (;;) {
		unsigned int count = *p++;
		unsigned int i;
		unsigned int ms;

		if (!count)
			break;
		fastboot_lcd_command(*p++);
		for (i = 1; i < count; i++)
			fastboot_lcd_data(*p++);
		ms = *p++;
		if (ms)
			delay_ms(ms);
	}
}

static void fastboot_lcd_window(unsigned int x0, unsigned int y0,
		unsigned int x1, unsigned int y1)
{
	fastboot_lcd_command(ST7789_CASET);
	fastboot_lcd_data((uint16_t)(x0 >> 8));
	fastboot_lcd_data((uint16_t)(x0 & 0xffu));
	fastboot_lcd_data((uint16_t)(x1 >> 8));
	fastboot_lcd_data((uint16_t)(x1 & 0xffu));
	fastboot_lcd_command(ST7789_RASET);
	fastboot_lcd_data((uint16_t)(y0 >> 8));
	fastboot_lcd_data((uint16_t)(y0 & 0xffu));
	fastboot_lcd_data((uint16_t)(y1 >> 8));
	fastboot_lcd_data((uint16_t)(y1 & 0xffu));
	fastboot_lcd_command(ST7789_RAMWR);
}

static void fastboot_lcd_fill(uint16_t color, unsigned int pixels)
{
	while (pixels-- != 0)
		fastboot_lcd_data(color);
}

static void fastboot_lcd_draw_logo_rle(void)
{
	unsigned int logo_pixels = FASTBOOT_LOGO_WIDTH * FASTBOOT_LOGO_HEIGHT;
	unsigned int pos = 0;
	unsigned int i;

	for (i = 0; i + 1 < UNIFROG_BOOT_LOGO_RLE_WORDS && pos < logo_pixels;
			i += 2u) {
		uint16_t color = unifrog_boot_logo_rle[i];
		unsigned int count = unifrog_boot_logo_rle[i + 1u];

		while (count-- != 0 && pos++ < logo_pixels)
			fastboot_lcd_data(color);
	}
}

static void fastboot_lcd_present_logo(void)
{
	uint32_t start_ticks = boot_ticks();

	boot_trace_note(FASTBOOT_TRACE_STAGE1_LOGO_BEGIN,
		FASTBOOT_BOOT_LOGO, start_ticks, 0);
	fastboot_lcd_gpio_ready();
	boot_trace_note(FASTBOOT_TRACE_STAGE1_LOGO_GPIO_READY,
		(uint32_t)read_reg32(GPIOL_DIR_REG),
		(uint32_t)read_reg32(GPIOT_DIR_REG),
		boot_ticks() - start_ticks);
	fastboot_lcd_reset();
	fastboot_lcd_apply_init();
	boot_trace_note(FASTBOOT_TRACE_STAGE1_LOGO_PANEL_READY,
		FASTBOOT_LCD_WIDTH, FASTBOOT_LCD_HEIGHT,
		boot_ticks() - start_ticks);
	fastboot_lcd_window(0, 0, FASTBOOT_LCD_WIDTH - 1u,
		FASTBOOT_LCD_HEIGHT - 1u);
	fastboot_lcd_fill(FASTBOOT_LOGO_BG,
		FASTBOOT_LCD_WIDTH * FASTBOOT_LCD_HEIGHT);
	fastboot_lcd_window(FASTBOOT_LOGO_X, FASTBOOT_LOGO_Y,
		FASTBOOT_LOGO_X + FASTBOOT_LOGO_WIDTH - 1u,
		FASTBOOT_LOGO_Y + FASTBOOT_LOGO_HEIGHT - 1u);
	fastboot_lcd_draw_logo_rle();
	boot_trace_note(FASTBOOT_TRACE_STAGE1_LOGO_DRAW_DONE,
		FASTBOOT_LOGO_WIDTH, FASTBOOT_LOGO_HEIGHT,
		boot_ticks() - start_ticks);
	fastboot_lcd_command(ST7789_DISPON);
	delay_ms(20);
	fastboot_backlight_on_gpio();
	fastboot_logo_visible = 1;
	boot_trace_note(FASTBOOT_TRACE_STAGE1_LOGO_BACKLIGHT_ON,
		(uint32_t)fastboot_backlight_state(), 0,
		boot_ticks() - start_ticks);
	rom_printf("fastboot: boot logo drawn ticks=%u\n",
		(unsigned int)(boot_ticks() - start_ticks));
}
#else
static void fastboot_lcd_present_logo(void)
{
	boot_trace_note(FASTBOOT_TRACE_STAGE1_LOGO_SKIPPED,
		FASTBOOT_BOOT_LOGO, 0, boot_ticks());
}
#endif

static void disable_interrupts(void)
{
	unsigned int status;

	__asm__ volatile(
		"mfc0 %0, $12\n\t"
		"li $8, -2\n\t"
		"and %0, %0, $8\n\t"
		"mtc0 %0, $12\n\t"
		"nop\n\t"
		"nop\n\t"
		"nop"
		: "=&r"(status)
		:
		: "$8", "memory");
}

static void clear_bss(void)
{
	uint8_t *p;

	for (p = &__bss_start; p < &__bss_end; p++)
		*p = 0;
}

static void copy_diag_path(volatile struct fastboot_diag *diag,
		const char *path)
{
	unsigned int i;

	if (!path)
		path = "";
	for (i = 0; i + 1 < FASTBOOT_HANDOFF_PATH_BYTES && path[i] != '\0'; i++)
		diag->path[i] = path[i];
	diag->path[i] = '\0';
}

static void write_diag(unsigned int event, int result, const char *path)
{
	volatile struct fastboot_diag *diag = FASTBOOT_DIAG_ADDR;

	diag->magic = FASTBOOT_DIAG_MAGIC;
	diag->stage_addr = FASTBOOT_STAGE1_ADDR;
	diag->event = event;
	diag->result = result;
	copy_diag_path(diag, path);
	rom_cache_flush((void *)diag, sizeof(*diag));
}

static int read_exact(FIL *fp, void *dst, UINT len)
{
	UINT got = 0;
	int rc;

	rc = rom_f_read(fp, dst, len, &got);
	if (rc != 0 || got != len)
		return -1;
	return 0;
}

static int skip_bytes(FIL *fp, unsigned int bytes)
{
	UINT want;

	while (bytes != 0) {
		want = bytes > CHUNK_SIZE ? CHUNK_SIZE : bytes;
		if (read_exact(fp, CHUNK_ADDR, want) != 0)
			return -1;
		bytes -= want;
	}
	return 0;
}

static int load_stream(FIL *fp, uint8_t *load_addr)
{
	uint8_t *dst = load_addr;
	UINT got = 0;
	unsigned int total = 0;
	int rc;

	for (;;) {
		rc = rom_f_read(fp, dst, CHUNK_SIZE, &got);
		if (rc != 0)
			return -1;
		if (got == 0)
			break;
		dst += got;
		total += got;
	}

	rom_printf("fastboot: loaded %u bytes\n", total);
	rom_cache_flush(load_addr, total);
	return total == 0 ? -1 : 0;
}

static void copy_bytes(uint8_t *dst, const uint8_t *src, unsigned int len)
{
	while (len-- != 0)
		*dst++ = *src++;
}

static int load_asd_staged(const char *path)
{
	unsigned int payload_size;
	int rc;

	boot_trace_note(FASTBOOT_TRACE_STAGE1_ASD_START,
		trace_path_hash(path), (unsigned int)(uintptr_t)ASD_LOAD_ADDR, 0);
	write_diag(20, 0, path);
	rom_printf("fastboot: open %s staged\n", path);
	rc = rom_f_open(&file, path, FA_READ);
	if (rc != 0) {
		rom_printf("fastboot: open failed %d\n", rc);
		boot_trace_note(FASTBOOT_TRACE_STAGE1_ASD_DONE,
			trace_path_hash(path), (unsigned int)rc, 0);
		write_diag(21, rc, path);
		return -1;
	}

	if (file.obj.objsize <= ASD_SKIP ||
			file.obj.objsize - ASD_SKIP >
			(uintptr_t)FASTBOOT_CHUNK_ADDR - (uintptr_t)ASD_STAGE_ADDR) {
		rom_printf("fastboot: asd size invalid %u\n",
			(unsigned int)file.obj.objsize);
		rom_f_close(&file);
		boot_trace_note(FASTBOOT_TRACE_STAGE1_ASD_DONE,
			trace_path_hash(path), 22, (unsigned int)file.obj.objsize);
		write_diag(22, (int)file.obj.objsize, path);
		return -1;
	}
	payload_size = (unsigned int)file.obj.objsize - ASD_SKIP;

	if (skip_bytes(&file, ASD_SKIP) != 0) {
		rom_printf("fastboot: skip failed\n");
		rom_f_close(&file);
		boot_trace_note(FASTBOOT_TRACE_STAGE1_ASD_DONE,
			trace_path_hash(path), 23, 0);
		write_diag(23, -1, path);
		return -1;
	}

	rc = load_stream(&file, ASD_STAGE_ADDR);
	rom_f_close(&file);
	if (rc != 0) {
		boot_trace_note(FASTBOOT_TRACE_STAGE1_ASD_DONE,
			trace_path_hash(path), 24, (unsigned int)rc);
		write_diag(24, rc, path);
		return -1;
	}

	copy_bytes(ASD_LOAD_ADDR, ASD_STAGE_ADDR, payload_size);
	rom_cache_flush(ASD_LOAD_ADDR, payload_size);
	rom_printf("fastboot: staged copy %u bytes\n", payload_size);
	boot_trace_note(FASTBOOT_TRACE_STAGE1_ASD_DONE,
		trace_path_hash(path), 0, payload_size);
	write_diag(25, (int)payload_size, path);
	return 0;
}

static int load_file(const char *path, unsigned int skip, uint8_t *load_addr)
{
	int rc;

	boot_trace_note(FASTBOOT_TRACE_STAGE1_LOAD_START,
		trace_path_hash(path), skip, (unsigned int)(uintptr_t)load_addr);
	write_diag(10, 0, path);
	rom_printf("fastboot: open %s\n", path);
	rc = rom_f_open(&file, path, FA_READ);
	if (rc != 0) {
		rom_printf("fastboot: open failed %d\n", rc);
		boot_trace_note(FASTBOOT_TRACE_STAGE1_LOAD_DONE,
			trace_path_hash(path), (unsigned int)rc, 0);
		write_diag(11, rc, path);
		return -1;
	}

	if (skip != 0 && skip_bytes(&file, skip) != 0) {
		rom_printf("fastboot: skip failed\n");
		rom_f_close(&file);
		boot_trace_note(FASTBOOT_TRACE_STAGE1_LOAD_DONE,
			trace_path_hash(path), 12, 0);
		write_diag(12, -1, path);
		return -1;
	}

	rc = load_stream(&file, load_addr);
	rom_f_close(&file);
	boot_trace_note(FASTBOOT_TRACE_STAGE1_LOAD_DONE,
		trace_path_hash(path), (unsigned int)rc, 0);
	write_diag(rc == 0 ? 13 : 14, rc, path);
	return rc;
}

static int read_handoff_path(char *path, unsigned int path_size)
{
	volatile struct fastboot_handoff *handoff = FASTBOOT_HANDOFF_ADDR;
	unsigned int i;

	if (path_size == 0 || handoff->magic != FASTBOOT_HANDOFF_MAGIC)
		return -1;

	for (i = 0; i < FASTBOOT_HANDOFF_PATH_BYTES && i + 1 < path_size; i++) {
		path[i] = handoff->path[i];
		if (path[i] == '\0')
			break;
	}
	path[path_size - 1] = '\0';

	handoff->magic = 0;
	for (i = 0; i < FASTBOOT_HANDOFF_PATH_BYTES; i++)
		handoff->path[i] = 0;
	rom_cache_flush((void *)handoff, sizeof(*handoff));

	for (i = 0; i < path_size && path[i] != '\0'; i++) {
		if (path[i] == '\r' || path[i] == '\n' || path[i] == '\t' ||
				path[i] == ' ') {
			path[i] = '\0';
			break;
		}
		if (path[i] == '\\' || path[i] == ':')
			return -1;
	}

	if (path[0] == '\0' || path[0] == '/' ||
			path[0] == '.' || path[0] == '\\')
		return -1;

	return 0;
}

static int path_has_unifrog_payload(const char *path)
{
	static const char needle[] = "unifrog.bin";
	unsigned int i;

	if (!path)
		return 0;
	for (i = 0; path[i] != '\0'; i++) {
		unsigned int j = 0;

		while (needle[j] != '\0' && path[i + j] == needle[j])
			j++;
		if (needle[j] == '\0')
			return 1;
	}
	return 0;
}

static void jump_to_payload(const char *path)
{
	entry_fn entry = (entry_fn)ENTRY_ADDR;
	int backlight_state;
	int keep_logo;

	keep_logo = fastboot_logo_visible && path_has_unifrog_payload(path);
	if (!keep_logo)
		fastboot_backlight_off();
	backlight_state = fastboot_backlight_state();
	boot_trace_note(FASTBOOT_TRACE_STAGE1_JUMP, trace_path_hash(path),
		(unsigned int)(uintptr_t)ENTRY_ADDR,
		(unsigned int)backlight_state | (keep_logo ? 0x10000u : 0u));
	write_diag(30, backlight_state | (keep_logo ? 0x10000 : 0), path);
	rom_printf("fastboot: jump %p backlight_state=0x%03x keep_logo=%d\n",
		ENTRY_ADDR, backlight_state, keep_logo);
	entry();
}

void stage1_main(void)
{
	char handoff_path[FASTBOOT_HANDOFF_PATH_BYTES];
	int rc;

	clear_bss();
	disable_interrupts();
	boot_trace_note(FASTBOOT_TRACE_STAGE1_START,
		(unsigned int)(uintptr_t)FASTBOOT_STAGE1_ADDR, 0, 0);
	fastboot_backlight_off();
	boot_trace_note(FASTBOOT_TRACE_STAGE1_BACKLIGHT_OFF,
		(unsigned int)fastboot_backlight_state(), 0, 0);
	fastboot_lcd_present_logo();
	write_diag(1, 0, "");
	rom_printf("\nfastboot: stage1 @ 0x%08x\n", FASTBOOT_STAGE1_ADDR);
	fastboot_backlight_report("stage1_start");

	rc = rom_f_mount(&fatfs, "", 1);
	rom_printf("fastboot: mount %d\n", rc);
	boot_trace_note(FASTBOOT_TRACE_STAGE1_MOUNT_RESULT, (unsigned int)rc, 0, 0);
	write_diag(2, rc, "");
	if (rc != 0)
		goto fail;

	if (read_handoff_path(handoff_path, sizeof(handoff_path)) == 0) {
		boot_trace_note(FASTBOOT_TRACE_STAGE1_HANDOFF_RESULT,
			0, trace_path_hash(handoff_path), 0);
		write_diag(3, 0, handoff_path);
		rom_printf("fastboot: handoff %s\n", handoff_path);
		if (load_asd_staged(handoff_path) == 0) {
			write_diag(4, 0, handoff_path);
			jump_to_payload(handoff_path);
		}
	} else {
		boot_trace_note(FASTBOOT_TRACE_STAGE1_HANDOFF_RESULT,
			(unsigned int)-1, 0, 0);
		write_diag(5, -1, "");
	}

	if (load_file("firmware/unifrog.bin", 0, RAW_LOAD_ADDR) == 0)
		jump_to_payload("firmware/unifrog.bin");
	if (load_file("unifrog.bin", 0, RAW_LOAD_ADDR) == 0)
		jump_to_payload("unifrog.bin");

	if (load_asd_staged("stock.asd") == 0)
		jump_to_payload("stock.asd");
	if (load_asd_staged("bisrv_stock.asd") == 0)
		jump_to_payload("bisrv_stock.asd");
	if (load_asd_staged("stock/bisrv.asd") == 0)
		jump_to_payload("stock/bisrv.asd");

fail:
	boot_trace_note(FASTBOOT_TRACE_STAGE1_FAIL, 0, 0, 0);
	write_diag(99, -1, "");
	rom_printf("fastboot: no bootable payload\n");
	for (;;)
		;
}
