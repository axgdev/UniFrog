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
#define RAW_LOAD_LIMIT ((unsigned int)((uintptr_t)FASTBOOT_CHUNK_ADDR - \
	(uintptr_t)RAW_LOAD_ADDR))
#define BOOT_ASD_CFG_PATH "firmware/boot_asd.cfg"
#define DEFAULT_ASD_PREFIX "firmware/"
#define DEFAULT_ASD_PREFIX_LEN 9u
#define DEFAULT_ASD_NAME_MAX 64u
#define DEFAULT_ASD_CFG_READ_MAX 96u
#define FASTBOOT_PINMUXL_BASE 0xb88004a0u
#define FASTBOOT_GPIOLCTRL_BASE 0xb8800044u
#define FASTBOOT_GPIO_INPUT_ST_REG 0x0cu
#define FASTBOOT_GPIO_OUTPUT_VAL_REG 0x10u
#define FASTBOOT_GPIO_DIR_REG 0x14u
#define FASTBOOT_KEY_SF2000_PL1_PIN 23u
#define FASTBOOT_KEY_SF2000_CLK_PIN 24u
#define FASTBOOT_KEY_GB300_D1_PIN 25u
#define FASTBOOT_KEY_GB300_CLK_PIN 26u
#define FASTBOOT_KEY_GB300_D0_PIN 27u
#define FASTBOOT_KEY_SHIFTER_BITS 16u
#define FASTBOOT_BUTTON_SCAN_POLLS 3u
#define FASTBOOT_SF2000_KEY_MASK ((1u << 12u) - 1u)
#define FASTBOOT_GB300_KEY_MASK ((1u << FASTBOOT_KEY_SHIFTER_BITS) - 1u)
#define FASTBOOT_SF2000_B_KEY_MASK (1u << 5u)
#define FASTBOOT_GB300_B_KEY_MASK (1u << 6u)

#define FA_READ 0x01u

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

enum fastboot_diag_event {
	FASTBOOT_DIAG_STAGE1_START = 1,
	FASTBOOT_DIAG_MOUNT = 2,
	FASTBOOT_DIAG_HANDOFF_FOUND = 3,
	FASTBOOT_DIAG_HANDOFF_BOOT = 4,
	FASTBOOT_DIAG_HANDOFF_MISSING = 5,
	FASTBOOT_DIAG_RAW_OPEN = 10,
	FASTBOOT_DIAG_RAW_OPEN_FAILED = 11,
	FASTBOOT_DIAG_RAW_SKIP_FAILED = 12,
	FASTBOOT_DIAG_RAW_LOADED = 13,
	FASTBOOT_DIAG_RAW_LOAD_FAILED = 14,
	FASTBOOT_DIAG_RAW_TOO_LARGE = 15,
	FASTBOOT_DIAG_ASD_OPEN = 20,
	FASTBOOT_DIAG_ASD_OPEN_FAILED = 21,
	FASTBOOT_DIAG_ASD_SIZE_INVALID = 22,
	FASTBOOT_DIAG_ASD_SKIP_FAILED = 23,
	FASTBOOT_DIAG_ASD_LOAD_FAILED = 24,
	FASTBOOT_DIAG_ASD_LOADED = 25,
	FASTBOOT_DIAG_JUMP = 30,
	FASTBOOT_DIAG_FAIL = 99
};

static rom_printf_fn const rom_printf = (rom_printf_fn)0x8101aa50u;
static rom_f_mount_fn const rom_f_mount = (rom_f_mount_fn)0x8101f044u;
static rom_f_open_fn const rom_f_open = (rom_f_open_fn)0x8101f0d4u;
static rom_f_read_fn const rom_f_read = (rom_f_read_fn)0x8101f548u;
static rom_f_close_fn const rom_f_close = (rom_f_close_fn)0x8101fa70u;
static rom_cache_flush_fn const rom_cache_flush = (rom_cache_flush_fn)0x810032f4u;

static FATFS fatfs;
static FIL file;
static uint32_t fastboot_last_sf2000_keys;
static uint32_t fastboot_last_gb300_keys;

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

static void set_reg32_bits(uint32_t addr, uint32_t mask)
{
	*(volatile uint32_t *)(uintptr_t)addr = read_reg32(addr) | mask;
}

static void clear_reg32_bits(uint32_t addr, uint32_t mask)
{
	*(volatile uint32_t *)(uintptr_t)addr = read_reg32(addr) & ~mask;
}

static void fastboot_delay_ticks(unsigned int ticks)
{
	volatile unsigned int i;

	for (i = 0; i < ticks; i++)
		__asm__ volatile("nop");
}

static void fastboot_pinmux_l_gpio(unsigned int pin)
{
	if (pin < 32u)
		write_reg8(FASTBOOT_PINMUXL_BASE + pin, 0);
}

static void fastboot_gpio_l_output(unsigned int pin, int high)
{
	uint32_t bit = 1u << pin;

	fastboot_pinmux_l_gpio(pin);
	set_reg32_bits(FASTBOOT_GPIOLCTRL_BASE + FASTBOOT_GPIO_DIR_REG, bit);
	if (high)
		set_reg32_bits(FASTBOOT_GPIOLCTRL_BASE +
			FASTBOOT_GPIO_OUTPUT_VAL_REG, bit);
	else
		clear_reg32_bits(FASTBOOT_GPIOLCTRL_BASE +
			FASTBOOT_GPIO_OUTPUT_VAL_REG, bit);
}

static void fastboot_gpio_l_input(unsigned int pin)
{
	uint32_t bit = 1u << pin;

	fastboot_pinmux_l_gpio(pin);
	clear_reg32_bits(FASTBOOT_GPIOLCTRL_BASE + FASTBOOT_GPIO_DIR_REG, bit);
}

static int fastboot_gpio_l_get(unsigned int pin)
{
	return (read_reg32(FASTBOOT_GPIOLCTRL_BASE +
		FASTBOOT_GPIO_INPUT_ST_REG) >> pin) & 1u;
}

static uint32_t fastboot_scan_sf2000_keys(void)
{
	uint32_t raw = 0;
	unsigned int i;

	fastboot_gpio_l_output(FASTBOOT_KEY_SF2000_CLK_PIN, 1);
	fastboot_gpio_l_output(FASTBOOT_KEY_SF2000_PL1_PIN, 0);
	fastboot_delay_ticks(800);
	fastboot_gpio_l_input(FASTBOOT_KEY_SF2000_PL1_PIN);
	fastboot_delay_ticks(800);

	for (i = 0; i < 12u; i++) {
		if (!fastboot_gpio_l_get(FASTBOOT_KEY_SF2000_PL1_PIN))
			raw |= 1u << i;
		fastboot_gpio_l_output(FASTBOOT_KEY_SF2000_CLK_PIN, 0);
		fastboot_delay_ticks(500);
		fastboot_gpio_l_output(FASTBOOT_KEY_SF2000_CLK_PIN, 1);
		fastboot_delay_ticks(500);
	}
	return raw;
}

static uint32_t fastboot_scan_gb300_keys(void)
{
	uint32_t raw = 0;
	unsigned int i;

	fastboot_gpio_l_output(FASTBOOT_KEY_GB300_CLK_PIN, 1);
	fastboot_gpio_l_output(FASTBOOT_KEY_GB300_D0_PIN, 0);
	fastboot_gpio_l_output(FASTBOOT_KEY_GB300_D1_PIN, 0);
	fastboot_gpio_l_output(FASTBOOT_KEY_GB300_CLK_PIN, 0);
	fastboot_delay_ticks(800);
	fastboot_gpio_l_input(FASTBOOT_KEY_GB300_D0_PIN);
	fastboot_gpio_l_input(FASTBOOT_KEY_GB300_D1_PIN);
	fastboot_delay_ticks(800);

	for (i = 0; i < FASTBOOT_KEY_SHIFTER_BITS; i++) {
		if (!fastboot_gpio_l_get(FASTBOOT_KEY_GB300_D0_PIN) ||
				!fastboot_gpio_l_get(FASTBOOT_KEY_GB300_D1_PIN))
			raw |= 1u << i;
		fastboot_gpio_l_output(FASTBOOT_KEY_GB300_CLK_PIN, 0);
		fastboot_delay_ticks(500);
		fastboot_gpio_l_output(FASTBOOT_KEY_GB300_CLK_PIN, 1);
		fastboot_delay_ticks(500);
	}
	return raw;
}

static uint32_t fastboot_sanitize_sf2000_keys(uint32_t keys)
{
	keys &= FASTBOOT_SF2000_KEY_MASK;
	if (keys == FASTBOOT_SF2000_KEY_MASK)
		return 0;
	return keys & FASTBOOT_SF2000_B_KEY_MASK;
}

static uint32_t fastboot_sanitize_gb300_keys(uint32_t keys)
{
	keys &= FASTBOOT_GB300_KEY_MASK;
	if (keys == FASTBOOT_GB300_KEY_MASK)
		return 0;
	return keys & FASTBOOT_GB300_B_KEY_MASK;
}

static uint32_t fastboot_scan_any_key(void)
{
	uint32_t sf2000;
	uint32_t gb300;
	unsigned int i;

	sf2000 = FASTBOOT_SF2000_KEY_MASK;
	for (i = 0; i < FASTBOOT_BUTTON_SCAN_POLLS; i++)
		sf2000 &= fastboot_scan_sf2000_keys();
	sf2000 = fastboot_sanitize_sf2000_keys(sf2000);
	if (sf2000 != 0) {
		fastboot_last_sf2000_keys = sf2000;
		fastboot_last_gb300_keys = 0;
		return sf2000;
	}

	gb300 = FASTBOOT_GB300_KEY_MASK;
	for (i = 0; i < FASTBOOT_BUTTON_SCAN_POLLS; i++)
		gb300 &= fastboot_scan_gb300_keys();
	gb300 = fastboot_sanitize_gb300_keys(gb300);

	/*
	 * A saturated read means the scanned bus is not the active controller
	 * matrix, or is floating/stuck low this early in boot. On SF2000 hardware
	 * the GB300 pins commonly read as 0xffff before the OS takes ownership.
	 */
	fastboot_last_sf2000_keys = 0;
	fastboot_last_gb300_keys = gb300;
	return gb300;
}

static void fastboot_backlight_off(void)
{
	/* Latch high before GPIO mux: R05 is an active-low backlight gate. */
	set_reg32_bits(FASTBOOT_GPIOR_OUTPUT_REG, FASTBOOT_BACKLIGHT_R05_MASK);
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

static int read_stream_bounded(FIL *fp, uint8_t *load_addr,
		unsigned int max_size, unsigned int *loaded)
{
	uint8_t *dst = load_addr;
	UINT got = 0;
	unsigned int total = 0;
	int rc;

	for (;;) {
		unsigned int room = max_size - total;
		UINT want;

		if (room == 0)
			return -1;
		want = room > CHUNK_SIZE ? CHUNK_SIZE : room;
		rc = rom_f_read(fp, dst, want, &got);
		if (rc != 0)
			return -1;
		if (got == 0)
			break;
		dst += got;
		total += got;
	}

	rom_printf("fastboot: loaded %u bytes\n", total);
	rom_cache_flush(load_addr, total);
	if (loaded)
		*loaded = total;
	return total == 0 ? -1 : 0;
}

static int read_stream_exact(FIL *fp, uint8_t *load_addr, unsigned int size)
{
	uint8_t *dst = load_addr;
	unsigned int remaining = size;

	while (remaining != 0) {
		UINT want = remaining > CHUNK_SIZE ? CHUNK_SIZE : remaining;

		if (read_exact(fp, dst, want) != 0)
			return -1;
		dst += want;
		remaining -= want;
	}

	rom_printf("fastboot: loaded %u bytes\n", size);
	rom_cache_flush(load_addr, size);
	return size == 0 ? -1 : 0;
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
	write_diag(FASTBOOT_DIAG_ASD_OPEN, 0, path);
	rom_printf("fastboot: open %s staged\n", path);
	rc = rom_f_open(&file, path, FA_READ);
	if (rc != 0) {
		rom_printf("fastboot: open failed %d\n", rc);
		boot_trace_note(FASTBOOT_TRACE_STAGE1_ASD_DONE,
			trace_path_hash(path), (unsigned int)rc, 0);
			write_diag(FASTBOOT_DIAG_ASD_OPEN_FAILED, rc, path);
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
			write_diag(FASTBOOT_DIAG_ASD_SIZE_INVALID,
				(int)file.obj.objsize, path);
		return -1;
	}
	payload_size = (unsigned int)file.obj.objsize - ASD_SKIP;

	if (skip_bytes(&file, ASD_SKIP) != 0) {
		rom_printf("fastboot: skip failed\n");
		rom_f_close(&file);
		boot_trace_note(FASTBOOT_TRACE_STAGE1_ASD_DONE,
			trace_path_hash(path), 23, 0);
			write_diag(FASTBOOT_DIAG_ASD_SKIP_FAILED, -1, path);
		return -1;
	}

	rc = read_stream_exact(&file, ASD_STAGE_ADDR, payload_size);
	rom_f_close(&file);
	if (rc != 0) {
		boot_trace_note(FASTBOOT_TRACE_STAGE1_ASD_DONE,
			trace_path_hash(path), 24, (unsigned int)rc);
			write_diag(FASTBOOT_DIAG_ASD_LOAD_FAILED, rc, path);
		return -1;
	}

	copy_bytes(ASD_LOAD_ADDR, ASD_STAGE_ADDR, payload_size);
	rom_cache_flush(ASD_LOAD_ADDR, payload_size);
	rom_printf("fastboot: staged copy %u bytes\n", payload_size);
	boot_trace_note(FASTBOOT_TRACE_STAGE1_ASD_DONE,
		trace_path_hash(path), 0, payload_size);
	write_diag(FASTBOOT_DIAG_ASD_LOADED, (int)payload_size, path);
	return 0;
}

static int load_file(const char *path, unsigned int skip, uint8_t *load_addr)
{
	unsigned int max_size = RAW_LOAD_LIMIT;
	unsigned int loaded = 0;
	int rc;

	boot_trace_note(FASTBOOT_TRACE_STAGE1_LOAD_START,
		trace_path_hash(path), skip, (unsigned int)(uintptr_t)load_addr);
	write_diag(FASTBOOT_DIAG_RAW_OPEN, 0, path);
	rom_printf("fastboot: open %s\n", path);
	rc = rom_f_open(&file, path, FA_READ);
	if (rc != 0) {
		rom_printf("fastboot: open failed %d\n", rc);
		boot_trace_note(FASTBOOT_TRACE_STAGE1_LOAD_DONE,
			trace_path_hash(path), (unsigned int)rc, 0);
		write_diag(FASTBOOT_DIAG_RAW_OPEN_FAILED, rc, path);
		return -1;
	}

	if (skip != 0 && skip_bytes(&file, skip) != 0) {
		rom_printf("fastboot: skip failed\n");
		rom_f_close(&file);
		boot_trace_note(FASTBOOT_TRACE_STAGE1_LOAD_DONE,
			trace_path_hash(path), 12, 0);
		write_diag(FASTBOOT_DIAG_RAW_SKIP_FAILED, -1, path);
		return -1;
	}

	if (skip >= max_size) {
		rom_printf("fastboot: skip exceeds load limit\n");
		rom_f_close(&file);
		boot_trace_note(FASTBOOT_TRACE_STAGE1_LOAD_DONE,
			trace_path_hash(path), 15, skip);
		write_diag(FASTBOOT_DIAG_RAW_TOO_LARGE, (int)skip, path);
		return -1;
	}

	rc = read_stream_bounded(&file, load_addr, max_size - skip, &loaded);
	rom_f_close(&file);
	boot_trace_note(FASTBOOT_TRACE_STAGE1_LOAD_DONE,
		trace_path_hash(path), (unsigned int)rc, loaded);
	write_diag(rc == 0 ? FASTBOOT_DIAG_RAW_LOADED :
		FASTBOOT_DIAG_RAW_LOAD_FAILED, rc, path);
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

static int fastboot_ascii_is_space(char c)
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static int fastboot_ascii_ends_asd(const char *name)
{
	unsigned int len = 0;

	while (name[len] != '\0')
		len++;
	if (len < 5u)
		return 0;
	return (name[len - 4u] == '.') &&
		(name[len - 3u] == 'a' || name[len - 3u] == 'A') &&
		(name[len - 2u] == 's' || name[len - 2u] == 'S') &&
		(name[len - 1u] == 'd' || name[len - 1u] == 'D');
}

static int fastboot_valid_asd_name(const char *name)
{
	unsigned int i;

	if (!name || name[0] == '\0' || name[0] == '.')
		return 0;
	for (i = 0; name[i] != '\0'; i++) {
		char c = name[i];

		if (i >= DEFAULT_ASD_NAME_MAX)
			return 0;
		if (c == '/' || c == '\\' || c == ':' || fastboot_ascii_is_space(c))
			return 0;
	}
	return fastboot_ascii_ends_asd(name);
}

static int read_default_asd_path(char *path, unsigned int path_size)
{
	char cfg[DEFAULT_ASD_CFG_READ_MAX + 1u];
	char name[DEFAULT_ASD_NAME_MAX + 1u];
	UINT got = 0;
	unsigned int begin = 0;
	unsigned int end;
	unsigned int i;
	unsigned int name_len;
	int rc;

	if (!path || path_size <= DEFAULT_ASD_PREFIX_LEN + 1u)
		return -1;

	rc = rom_f_open(&file, BOOT_ASD_CFG_PATH, FA_READ);
	if (rc != 0) {
		boot_trace_note(FASTBOOT_TRACE_STAGE1_DEFAULT_RESULT,
			(unsigned int)-2, (unsigned int)rc, 0);
		return -1;
	}
	rc = rom_f_read(&file, cfg, DEFAULT_ASD_CFG_READ_MAX, &got);
	rom_f_close(&file);
	if (rc != 0 || got == 0) {
		boot_trace_note(FASTBOOT_TRACE_STAGE1_DEFAULT_RESULT,
			(unsigned int)-3, (unsigned int)rc, got);
		return -1;
	}

	cfg[got] = '\0';
	while (begin < got && fastboot_ascii_is_space(cfg[begin]))
		begin++;
	end = got;
	while (end > begin && fastboot_ascii_is_space(cfg[end - 1u]))
		end--;
	if (end <= begin || end - begin > DEFAULT_ASD_NAME_MAX) {
		boot_trace_note(FASTBOOT_TRACE_STAGE1_DEFAULT_RESULT,
			(unsigned int)-4, got, end - begin);
		return -1;
	}

	name_len = end - begin;
	for (i = 0; i < name_len; i++)
		name[i] = cfg[begin + i];
	name[name_len] = '\0';
	if (!fastboot_valid_asd_name(name)) {
		boot_trace_note(FASTBOOT_TRACE_STAGE1_DEFAULT_RESULT,
			(unsigned int)-5, trace_path_hash(name), name_len);
		return -1;
	}

	if (DEFAULT_ASD_PREFIX_LEN + name_len + 1u > path_size)
		return -1;
	for (i = 0; i < DEFAULT_ASD_PREFIX_LEN; i++)
		path[i] = DEFAULT_ASD_PREFIX[i];
	for (i = 0; i < name_len; i++)
		path[DEFAULT_ASD_PREFIX_LEN + i] = name[i];
	path[DEFAULT_ASD_PREFIX_LEN + name_len] = '\0';
	return 0;
}

static void jump_to_payload(const char *path)
{
	entry_fn entry = (entry_fn)ENTRY_ADDR;
	int backlight_state;

	fastboot_backlight_off();
	backlight_state = fastboot_backlight_state();
	boot_trace_note(FASTBOOT_TRACE_STAGE1_JUMP, trace_path_hash(path),
		(unsigned int)(uintptr_t)ENTRY_ADDR, (unsigned int)backlight_state);
	write_diag(FASTBOOT_DIAG_JUMP, backlight_state, path);
	rom_printf("fastboot: jump %p backlight_state=0x%03x\n",
		ENTRY_ADDR, backlight_state);
	entry();
}

void stage1_main(void)
{
	char handoff_path[FASTBOOT_HANDOFF_PATH_BYTES];
	char default_path[FASTBOOT_HANDOFF_PATH_BYTES];
	uint32_t boot_override_keys;
	int rc;

	clear_bss();
	disable_interrupts();
	boot_override_keys = 0;
	boot_trace_note(FASTBOOT_TRACE_STAGE1_START,
		(unsigned int)(uintptr_t)FASTBOOT_STAGE1_ADDR, 0, 0);
	fastboot_backlight_off();
	boot_trace_note(FASTBOOT_TRACE_STAGE1_BACKLIGHT_OFF,
		(unsigned int)fastboot_backlight_state(), 0, 0);
	write_diag(FASTBOOT_DIAG_STAGE1_START, 0, "");
	rom_printf("\nfastboot: stage1 @ 0x%08x\n", FASTBOOT_STAGE1_ADDR);
	fastboot_backlight_report("stage1_start");

	rc = rom_f_mount(&fatfs, "", 1);
	rom_printf("fastboot: mount %d\n", rc);
	boot_trace_note(FASTBOOT_TRACE_STAGE1_MOUNT_RESULT, (unsigned int)rc, 0, 0);
	write_diag(FASTBOOT_DIAG_MOUNT, rc, "");
	if (rc != 0)
		goto fail;

	if (read_handoff_path(handoff_path, sizeof(handoff_path)) == 0) {
		boot_trace_note(FASTBOOT_TRACE_STAGE1_HANDOFF_RESULT,
			0, trace_path_hash(handoff_path), 0);
		write_diag(FASTBOOT_DIAG_HANDOFF_FOUND, 0, handoff_path);
		rom_printf("fastboot: handoff %s\n", handoff_path);
		if (load_asd_staged(handoff_path) == 0) {
			write_diag(FASTBOOT_DIAG_HANDOFF_BOOT, 0, handoff_path);
			jump_to_payload(handoff_path);
		}
	} else {
		boot_trace_note(FASTBOOT_TRACE_STAGE1_HANDOFF_RESULT,
			(unsigned int)-1, 0, 0);
		write_diag(FASTBOOT_DIAG_HANDOFF_MISSING, -1, "");
	}

	boot_override_keys = fastboot_scan_any_key();
	if (boot_override_keys != 0) {
		boot_trace_note(FASTBOOT_TRACE_STAGE1_INPUT_OVERRIDE,
			boot_override_keys, fastboot_last_sf2000_keys,
			fastboot_last_gb300_keys);
		rom_printf("fastboot: input override keys=0x%08x sf2000=0x%08x gb300=0x%08x\n",
			(unsigned int)boot_override_keys,
			(unsigned int)fastboot_last_sf2000_keys,
			(unsigned int)fastboot_last_gb300_keys);
	}

	if (boot_override_keys == 0 &&
			read_default_asd_path(default_path, sizeof(default_path)) == 0) {
		boot_trace_note(FASTBOOT_TRACE_STAGE1_DEFAULT_RESULT,
			0, trace_path_hash(default_path), 0);
		rom_printf("fastboot: default %s\n", default_path);
		if (load_asd_staged(default_path) == 0)
			jump_to_payload(default_path);
		boot_trace_note(FASTBOOT_TRACE_STAGE1_DEFAULT_RESULT,
			(unsigned int)-1, trace_path_hash(default_path), 0);
	} else if (boot_override_keys == 0) {
		boot_trace_note(FASTBOOT_TRACE_STAGE1_DEFAULT_RESULT,
			(unsigned int)-1, 0, 0);
	}

	if (load_file("unifrog/firmware/unifrog.bin", 0, RAW_LOAD_ADDR) == 0)
		jump_to_payload("unifrog/firmware/unifrog.bin");

	if (load_asd_staged("stock.asd") == 0)
		jump_to_payload("stock.asd");
	if (load_asd_staged("bisrv_stock.asd") == 0)
		jump_to_payload("bisrv_stock.asd");
	if (load_asd_staged("stock/bisrv.asd") == 0)
		jump_to_payload("stock/bisrv.asd");

fail:
	boot_trace_note(FASTBOOT_TRACE_STAGE1_FAIL, 0, 0, 0);
	write_diag(FASTBOOT_DIAG_FAIL, -1, "");
	rom_printf("fastboot: no bootable payload\n");
	for (;;)
		;
}
