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

extern uint8_t __bss_start;
extern uint8_t __bss_end;

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

	write_diag(20, 0, path);
	rom_printf("fastboot: open %s staged\n", path);
	rc = rom_f_open(&file, path, FA_READ);
	if (rc != 0) {
		rom_printf("fastboot: open failed %d\n", rc);
		write_diag(21, rc, path);
		return -1;
	}

	if (file.obj.objsize <= ASD_SKIP ||
			file.obj.objsize - ASD_SKIP >
			(uintptr_t)FASTBOOT_CHUNK_ADDR - (uintptr_t)ASD_STAGE_ADDR) {
		rom_printf("fastboot: asd size invalid %u\n",
			(unsigned int)file.obj.objsize);
		rom_f_close(&file);
		write_diag(22, (int)file.obj.objsize, path);
		return -1;
	}
	payload_size = (unsigned int)file.obj.objsize - ASD_SKIP;

	if (skip_bytes(&file, ASD_SKIP) != 0) {
		rom_printf("fastboot: skip failed\n");
		rom_f_close(&file);
		write_diag(23, -1, path);
		return -1;
	}

	rc = load_stream(&file, ASD_STAGE_ADDR);
	rom_f_close(&file);
	if (rc != 0) {
		write_diag(24, rc, path);
		return -1;
	}

	copy_bytes(ASD_LOAD_ADDR, ASD_STAGE_ADDR, payload_size);
	rom_cache_flush(ASD_LOAD_ADDR, payload_size);
	rom_printf("fastboot: staged copy %u bytes\n", payload_size);
	write_diag(25, (int)payload_size, path);
	return 0;
}

static int load_file(const char *path, unsigned int skip, uint8_t *load_addr)
{
	int rc;

	write_diag(10, 0, path);
	rom_printf("fastboot: open %s\n", path);
	rc = rom_f_open(&file, path, FA_READ);
	if (rc != 0) {
		rom_printf("fastboot: open failed %d\n", rc);
		write_diag(11, rc, path);
		return -1;
	}

	if (skip != 0 && skip_bytes(&file, skip) != 0) {
		rom_printf("fastboot: skip failed\n");
		rom_f_close(&file);
		write_diag(12, -1, path);
		return -1;
	}

	rc = load_stream(&file, load_addr);
	rom_f_close(&file);
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

static void jump_to_payload(void)
{
	entry_fn entry = (entry_fn)ENTRY_ADDR;

	rom_printf("fastboot: jump %p\n", ENTRY_ADDR);
	entry();
}

void stage1_main(void)
{
	char handoff_path[FASTBOOT_HANDOFF_PATH_BYTES];
	int rc;

	clear_bss();
	disable_interrupts();
	write_diag(1, 0, "");
	rom_printf("\nfastboot: stage1 @ 0x%08x\n", FASTBOOT_STAGE1_ADDR);

	rc = rom_f_mount(&fatfs, "", 1);
	rom_printf("fastboot: mount %d\n", rc);
	write_diag(2, rc, "");
	if (rc != 0)
		goto fail;

	if (read_handoff_path(handoff_path, sizeof(handoff_path)) == 0) {
		write_diag(3, 0, handoff_path);
		rom_printf("fastboot: handoff %s\n", handoff_path);
		if (load_asd_staged(handoff_path) == 0) {
			write_diag(4, 0, handoff_path);
			jump_to_payload();
		}
	} else {
		write_diag(5, -1, "");
	}

	if (load_file("firmware/unifrog.bin", 0, RAW_LOAD_ADDR) == 0)
		jump_to_payload();
	if (load_file("unifrog.bin", 0, RAW_LOAD_ADDR) == 0)
		jump_to_payload();

	if (load_asd_staged("stock.asd") == 0)
		jump_to_payload();
	if (load_asd_staged("bisrv_stock.asd") == 0)
		jump_to_payload();
	if (load_asd_staged("stock/bisrv.asd") == 0)
		jump_to_payload();

fail:
	write_diag(99, -1, "");
	rom_printf("fastboot: no bootable payload\n");
	for (;;)
		;
}
