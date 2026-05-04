#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/unistd.h>

#include <cpu_func.h>

#include <unifrog/battery.h>
#include <unifrog/boot.h>
#include <unifrog/fb.h>
#include <unifrog/ge.h>
#include <unifrog/gfx.h>
#include <unifrog/input.h>
#include <unifrog/log.h>
#include <unifrog/platform.h>
#include <unifrog/runtime.h>
#include <unifrog/text.h>

#define RGB565(r, g, b) UNIFROG_RGB565((r), (g), (b))

#ifndef UNIFROG_GIT_COMMIT
#define UNIFROG_GIT_COMMIT "unknown"
#endif

#ifndef UNIFROG_GIT_DIRTY
#define UNIFROG_GIT_DIRTY 1
#endif

#define printf unifrog_log
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#define MAX_FIRMWARE_ITEMS 12
#define MAX_FIRMWARE_NAME 64
#define MAX_FIRMWARE_PATH 96

struct shell_item {
	const char *name;
	const char *detail;
};

static const struct shell_item shell_items[] = {
	{"FIRMWARE", "boot an ASD from /firmware"},
	{"INPUT", "local and wireless controllers"},
	{"BATTERY", "raw battery status"},
	{"FLUSH LOG", "write buffered diagnostics"},
	{"REBOOT", "restart device"},
};

struct firmware_item {
	char name[MAX_FIRMWARE_NAME];
	char path[MAX_FIRMWARE_PATH];
	int boot_supported;
};

static struct unifrog_fb fb;
static struct unifrog_ge ge;
static struct unifrog_surface surface;
static uint16_t *render_pixels;
static struct unifrog_battery_status battery;
static struct firmware_item firmware_items[MAX_FIRMWARE_ITEMS];
static unsigned firmware_count;
static unsigned firmware_selected;
static char firmware_status[96];
static const char *firmware_scan_dirs[] = {
	"/media/mmcblk0/firmware",
	"/media/mmcblk0/FIRMWARE",
	"/media/mmcblk0p1/firmware",
	"/media/mmcblk0p1/FIRMWARE",
	"/media/mmcblk0p2/firmware",
	"/media/mmcblk0p2/FIRMWARE",
	"/firmware",
	"/FIRMWARE",
	"firmware",
	"FIRMWARE",
};
static unsigned selected;
static unsigned frame;
static int dirty = 1;
static int first_frame_shown;
static int wireless_started;

static void draw_message(const char *title, const char *message);
static void draw_firmware(void);

static int pressed(enum unifrog_button button)
{
	return unifrog_input_pressed(button);
}

static void set_backlight(unsigned value)
{
	unsigned char level = value > 100 ? 100 : (unsigned char)value;
	int fd = open("/dev/backlight", O_RDWR);

	if (fd < 0)
		return;
	(void)write(fd, &level, sizeof(level));
	close(fd);
}

static void present_surface(void)
{
	struct unifrog_ge_surface src;
	struct unifrog_ge_surface dst;
	struct unifrog_ge_rect rect;

	src.pixels = render_pixels;
	src.width = surface.width;
	src.height = surface.height;
	src.pitch_bytes = surface.stride * sizeof(uint16_t);
	src.format = UNIFROG_GE_FORMAT_RGB565;
	dst = unifrog_fb_ge_surface(&fb);
	rect.x = 0;
	rect.y = 0;
	rect.w = surface.width;
	rect.h = surface.height;

	if (unifrog_ge_stretch(&ge, &dst, &rect, &src, &rect,
			UNIFROG_GE_FLUSH_SOURCE) == 0)
		unifrog_ge_sync(&ge);
	else
		unifrog_fb_flush(&fb);
}

static void present_black_frames(unsigned count)
{
	for (unsigned i = 0; i < count; i++) {
		unifrog_gfx_fill_rect(&surface, 0, 0, surface.width, surface.height,
			RGB565(0, 0, 0));
		present_surface();
		if (!first_frame_shown) {
			set_backlight(100);
			first_frame_shown = 1;
		}
		usleep(16000);
	}
}

static void draw_header(void)
{
	char buf[96];

	unifrog_gfx_fill_rect(&surface, 0, 0, surface.width, surface.height,
		RGB565(9, 12, 16));
	unifrog_gfx_fill_rect(&surface, 0, 0, surface.width, 34,
		RGB565(24, 38, 52));
	unifrog_gfx_draw_text(&surface, 10, 10, "UNIFROG", RGB565(232, 245, 238), 2);

	if (battery.available) {
		snprintf(buf, sizeof(buf), "%u%% %umV",
			battery.bars * 25, battery.millivolts);
	} else {
		snprintf(buf, sizeof(buf), "BAT --");
	}
	unifrog_gfx_draw_text(&surface, 232, 12, buf, RGB565(166, 220, 184), 1);
}

static void draw_menu(void)
{
	char buf[96];
	unsigned y = 52;

	draw_header();
	unifrog_gfx_draw_text(&surface, 10, 40, "BOOT SHELL",
		RGB565(120, 154, 176), 1);

	for (unsigned i = 0; i < ARRAY_SIZE(shell_items); i++) {
		uint16_t fg = RGB565(196, 210, 218);
		uint16_t bg = RGB565(9, 12, 16);

		if (i == selected) {
			bg = RGB565(34, 82, 82);
			fg = RGB565(255, 255, 255);
		}

		unifrog_gfx_fill_rect(&surface, 8, y - 4, surface.width - 16, 21, bg);
		snprintf(buf, sizeof(buf), "%s", shell_items[i].name);
		unifrog_gfx_draw_text(&surface, 14, y, buf, fg, 1);
		unifrog_gfx_draw_text(&surface, 108, y, shell_items[i].detail,
			RGB565(126, 144, 154), 1);
		y += 24;
	}

	snprintf(buf, sizeof(buf), "RF:%s CH:%u LOG:%u",
		unifrog_input_wireless_bus_ok() ? "OK" : "NO",
		unifrog_input_wireless_channel_index(),
		(unsigned)unifrog_log_pending());
	unifrog_gfx_draw_text(&surface, 10, surface.height - 18, buf,
		RGB565(120, 138, 150), 1);

	present_surface();
	if (!first_frame_shown) {
		set_backlight(100);
		first_frame_shown = 1;
	}
}

static int has_asd_suffix(const char *name)
{
	size_t len;

	if (!name)
		return 0;
	len = strlen(name);
	if (len < 5)
		return 0;
	return strcmp(name + len - 4, ".asd") == 0 ||
		strcmp(name + len - 4, ".ASD") == 0;
}

static int firmware_item_exists(const char *name)
{
	for (unsigned i = 0; i < firmware_count; i++) {
		if (strcmp(firmware_items[i].name, name) == 0)
			return 1;
	}

	return 0;
}

static void refresh_firmware_list(void)
{
	DIR *dir;
	struct dirent *ent;
	struct stat st;
	unsigned dirs_opened = 0;
	unsigned entries_seen = 0;
	int mount_ret;
	int storage_ret;

	firmware_count = 0;
	firmware_selected = 0;
	snprintf(firmware_status, sizeof(firmware_status), "WAIT STORAGE");
	draw_firmware();
	mount_ret = unifrog_platform_mount_storage();
	printf("unifrog firmware storage mount ret=%d\n", mount_ret);
	storage_ret = unifrog_platform_wait_for_storage();
	printf("unifrog firmware storage wait ret=%d\n", storage_ret);

	for (unsigned d = 0; d < ARRAY_SIZE(firmware_scan_dirs) &&
			firmware_count < MAX_FIRMWARE_ITEMS; d++) {
		const char *dir_path = firmware_scan_dirs[d];

		printf("unifrog firmware stat path=%s ret=%d\n",
			dir_path, stat(dir_path, &st));
		dir = opendir(dir_path);
		if (!dir) {
			printf("unifrog firmware open failed path=%s\n", dir_path);
			continue;
		}

		dirs_opened++;
		printf("unifrog firmware scan path=%s\n", dir_path);
		while ((ent = readdir(dir)) != NULL && firmware_count < MAX_FIRMWARE_ITEMS) {
			size_t name_len;

			entries_seen++;
			printf("unifrog firmware entry path=%s name=%s\n",
				dir_path, ent->d_name);
			if (!has_asd_suffix(ent->d_name))
				continue;
			name_len = strlen(ent->d_name);
			if (name_len >= MAX_FIRMWARE_NAME ||
				strlen(dir_path) + 1 + name_len >= MAX_FIRMWARE_PATH)
				continue;
			if (firmware_item_exists(ent->d_name)) {
				printf("unifrog firmware duplicate name=%s path=%s\n",
					ent->d_name, dir_path);
				continue;
			}
			strcpy(firmware_items[firmware_count].name, ent->d_name);
			strcpy(firmware_items[firmware_count].path, dir_path);
			strcat(firmware_items[firmware_count].path, "/");
			strcat(firmware_items[firmware_count].path, ent->d_name);
			firmware_items[firmware_count].boot_supported =
				unifrog_boot_firmware_name_supported(ent->d_name);
			firmware_count++;
		}

		closedir(dir);
	}
	snprintf(firmware_status, sizeof(firmware_status),
		"M%d S%d D%u E%u ASD%u",
		mount_ret, storage_ret, dirs_opened, entries_seen, firmware_count);
	printf("unifrog firmware list count=%u\n", firmware_count);
	unifrog_log_flush();
}

static void draw_firmware(void)
{
	unsigned y = 58;
	char buf[96];

	draw_header();
	unifrog_gfx_draw_text(&surface, 10, 40, "FIRMWARE",
		RGB565(120, 154, 176), 1);

	if (firmware_count == 0) {
		unifrog_gfx_draw_text(&surface, 10, 84, "NO .ASD IN /FIRMWARE",
			RGB565(190, 205, 214), 1);
		unifrog_gfx_draw_text(&surface, 10, 110, firmware_status,
			RGB565(166, 220, 184), 1);
		unifrog_gfx_draw_text(&surface, 10, 210, "A REFRESH   B BACK",
			RGB565(120, 138, 150), 1);
		present_surface();
		return;
	}

	for (unsigned i = 0; i < firmware_count; i++) {
		uint16_t fg = RGB565(196, 210, 218);
		uint16_t bg = RGB565(9, 12, 16);

		if (i == firmware_selected) {
			bg = RGB565(34, 82, 82);
			fg = RGB565(255, 255, 255);
		}

		unifrog_gfx_fill_rect(&surface, 8, y - 4, surface.width - 16, 21, bg);
		unifrog_gfx_draw_text(&surface, 14, y, firmware_items[i].name, fg, 1);
		if (!firmware_items[i].boot_supported)
			unifrog_gfx_draw_text(&surface, surface.width - 62, y,
				"8.3", RGB565(250, 190, 70), 1);
		y += 24;
	}

	if (!firmware_items[firmware_selected].boot_supported)
		unifrog_gfx_draw_text(&surface, 10, 188, "RENAME: 8 CHARS + .ASD",
			RGB565(250, 190, 70), 1);
	unifrog_gfx_draw_text(&surface, 10, 210, "A BOOT   B BACK",
		RGB565(120, 138, 150), 1);
	present_surface();
}

static void boot_firmware(const struct firmware_item *item)
{
	if (!item->boot_supported) {
		draw_message("FIRMWARE", "Rename to 8.3 .ASD");
		dirty = 0;
		return;
	}

	draw_message("FIRMWARE", "Rebooting");
	unifrog_log_flush();

	printf("unifrog firmware handoff path=%s name=%s\n",
		item->path, item->name);
	unifrog_log_flush();

	if (unifrog_boot_firmware_asd(item->name) != 0) {
		draw_message("FIRMWARE", "Handoff failed");
		dirty = 0;
	}
}

static void draw_input(void)
{
	struct unifrog_input_snapshot snap;
	char buf[96];
	char pressed[96];
	size_t used = 0;

	unifrog_input_snapshot(&snap);
	draw_header();
	unifrog_gfx_draw_text(&surface, 10, 46, "INPUT", RGB565(232, 245, 238), 2);

	snprintf(buf, sizeof(buf), "LOCAL 0x%08lx", (unsigned long)snap.local_raw);
	unifrog_gfx_draw_text(&surface, 10, 78, buf, RGB565(190, 205, 214), 1);
	snprintf(buf, sizeof(buf), "P1 0x%08lx T%u",
		(unsigned long)snap.wireless_raw[0], snap.wireless_timeout[0]);
	unifrog_gfx_draw_text(&surface, 10, 96, buf, RGB565(190, 205, 214), 1);
	snprintf(buf, sizeof(buf), "P2 0x%08lx T%u",
		(unsigned long)snap.wireless_raw[1], snap.wireless_timeout[1]);
	unifrog_gfx_draw_text(&surface, 10, 114, buf, RGB565(190, 205, 214), 1);
	snprintf(buf, sizeof(buf), "BUTTONS 0x%08lx", (unsigned long)snap.buttons);
	unifrog_gfx_draw_text(&surface, 10, 132, buf, RGB565(166, 220, 184), 1);
	for (int i = 0; i < UNIFROG_BUTTON_COUNT; i++) {
		if (snap.buttons & UNIFROG_BUTTON_MASK(i)) {
			int written = snprintf(pressed + used, sizeof(pressed) - used,
				"%s%s", used ? " " : "",
				unifrog_input_button_name((enum unifrog_button)i));
			if (written > 0) {
				used += (size_t)written;
				if (used >= sizeof(pressed))
					used = sizeof(pressed) - 1;
			}
		}
	}
	if (used == 0)
		snprintf(pressed, sizeof(pressed), "NONE");
	unifrog_gfx_draw_text(&surface, 10, 150, pressed, RGB565(250, 190, 70), 1);
	unifrog_gfx_draw_text(&surface, 10, 210, "B BACK",
		RGB565(120, 138, 150), 1);

	present_surface();
}

static void draw_battery(void)
{
	char buf[96];

	draw_header();
	unifrog_gfx_draw_text(&surface, 10, 46, "BATTERY",
		RGB565(232, 245, 238), 2);
	snprintf(buf, sizeof(buf), "AVAILABLE %d", battery.available);
	unifrog_gfx_draw_text(&surface, 10, 82, buf, RGB565(190, 205, 214), 1);
	snprintf(buf, sizeof(buf), "RAW %u", battery.raw);
	unifrog_gfx_draw_text(&surface, 10, 100, buf, RGB565(190, 205, 214), 1);
	snprintf(buf, sizeof(buf), "MILLIVOLTS %u", battery.millivolts);
	unifrog_gfx_draw_text(&surface, 10, 118, buf, RGB565(190, 205, 214), 1);
	snprintf(buf, sizeof(buf), "BARS %u LOW %d SRC %s",
		battery.bars, battery.low, battery.source);
	unifrog_gfx_draw_text(&surface, 10, 136, buf, RGB565(166, 220, 184), 1);
	unifrog_gfx_draw_text(&surface, 10, 210, "A REFRESH   B BACK",
		RGB565(120, 138, 150), 1);

	present_surface();
}

static void draw_message(const char *title, const char *message)
{
	draw_header();
	unifrog_gfx_draw_text(&surface, 10, 58, title, RGB565(232, 245, 238), 2);
	unifrog_gfx_draw_text(&surface, 10, 96, message, RGB565(190, 205, 214), 1);
	unifrog_gfx_draw_text(&surface, 10, 210, "B BACK", RGB565(120, 138, 150), 1);
	present_surface();
}

void unifrog_test_frontend_main(void)
{
	enum {
		SHELL_MENU,
		SHELL_FIRMWARE,
		SHELL_INPUT,
		SHELL_BATTERY,
		SHELL_MESSAGE,
	} view = SHELL_MENU;
	const char *message_title = "UNIFROG";
	const char *message_body = "READY";

	printf("unifrog shell boot api=%u commit=%s dirty=%d\n",
		unifrog_runtime_api_version(), UNIFROG_GIT_COMMIT, UNIFROG_GIT_DIRTY);
	unifrog_battery_status_init(&battery);
	unifrog_input_clear();

	if (unifrog_fb_open(&fb, UNIFROG_FB_OPEN_DEFAULT) != 0) {
		printf("unifrog shell fb open failed\n");
		for (;;)
			usleep(10000);
	}
	if (unifrog_ge_open(&ge) != 0) {
		printf("unifrog shell ge open failed\n");
		for (;;)
			usleep(10000);
	}
	unifrog_ge_set_fast_clock(&ge);
	render_pixels = malloc((size_t)fb.width * fb.height * sizeof(*render_pixels));
	if (!render_pixels) {
		printf("unifrog shell render alloc failed\n");
		for (;;)
			usleep(10000);
	}
	surface.pixels = render_pixels;
	surface.width = fb.width;
	surface.height = fb.height;
	surface.stride = fb.width;
	present_black_frames(3);
	draw_menu();
	dirty = 0;

	for (;;) {
		unifrog_input_save_previous();
		unifrog_input_poll();
		if (frame == 1) {
			unifrog_battery_update(&battery, 1);
			dirty = 1;
		}
		if (!wireless_started && frame == 2) {
			unifrog_input_wireless_init();
			wireless_started = 1;
			dirty = 1;
		}
		if (wireless_started && (frame % 60) == 0)
			unifrog_battery_update(&battery, 0);

		if (view == SHELL_MENU) {
			if (pressed(UNIFROG_BUTTON_UP)) {
				selected = selected == 0 ?
					(unsigned)ARRAY_SIZE(shell_items) - 1 :
					selected - 1;
				dirty = 1;
			}
			if (pressed(UNIFROG_BUTTON_DOWN)) {
				selected++;
				if (selected >= ARRAY_SIZE(shell_items))
					selected = 0;
				dirty = 1;
			}
			if (pressed(UNIFROG_BUTTON_A) || pressed(UNIFROG_BUTTON_START)) {
				if (selected == 0) {
					refresh_firmware_list();
					view = SHELL_FIRMWARE;
				} else if (selected == 1) {
					view = SHELL_INPUT;
				} else if (selected == 2) {
					unifrog_battery_update(&battery, 1);
					view = SHELL_BATTERY;
				} else if (selected == 3) {
					unifrog_log_flush();
					message_title = "LOG";
					message_body = "Flushed";
					view = SHELL_MESSAGE;
				} else if (selected == 4) {
					draw_message("REBOOT", "Restarting");
					unifrog_log_flush();
					reset();
				}
				dirty = 1;
			}
		} else if (view == SHELL_FIRMWARE) {
			if (pressed(UNIFROG_BUTTON_B)) {
				view = SHELL_MENU;
				dirty = 1;
			}
			if (pressed(UNIFROG_BUTTON_UP) && firmware_count != 0) {
				firmware_selected = firmware_selected == 0 ?
					firmware_count - 1 : firmware_selected - 1;
				dirty = 1;
			}
			if (pressed(UNIFROG_BUTTON_DOWN) && firmware_count != 0) {
				firmware_selected++;
				if (firmware_selected >= firmware_count)
					firmware_selected = 0;
				dirty = 1;
			}
			if (pressed(UNIFROG_BUTTON_A)) {
				if (firmware_count == 0)
					refresh_firmware_list();
				else
					boot_firmware(&firmware_items[firmware_selected]);
				dirty = 1;
			}
		} else {
			if (pressed(UNIFROG_BUTTON_B)) {
				view = SHELL_MENU;
				dirty = 1;
			}
			if (view == SHELL_BATTERY && pressed(UNIFROG_BUTTON_A)) {
				unifrog_battery_update(&battery, 1);
				dirty = 1;
			}
		}

		if (dirty || view == SHELL_INPUT || (frame % 30) == 0) {
			if (view == SHELL_MENU)
				draw_menu();
			else if (view == SHELL_FIRMWARE)
				draw_firmware();
			else if (view == SHELL_INPUT)
				draw_input();
			else if (view == SHELL_BATTERY)
				draw_battery();
			else
				draw_message(message_title, message_body);
			dirty = 0;
		}

		frame++;
		usleep(16000);
	}
}
