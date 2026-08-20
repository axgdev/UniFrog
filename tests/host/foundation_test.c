#include "test.h"

#include <unifrog/battery.h>
#include <unifrog/artwork.h>
#include <unifrog/boot.h>
#include <unifrog/path.h>
#include <unifrog/log.h>
#include <unifrog/storage_profile.h>
#include <unifrog/text.h>
#include <unifrog/zip.h>

#include <unistd.h>
#include <sys/stat.h>

static void test_text(void)
{
   char text[16];
   static const char japanese[] = "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e";

   memset(text, 0x55, sizeof(text));
   unifrog_text_copy(text, sizeof(text), "hello");
   TEST_EQ_STR("hello", text);
   unifrog_text_copy(text, 5, "abcdef");
   TEST_EQ_STR("abcd", text);
   unifrog_text_copy(text, sizeof(text), NULL);
   TEST_EQ_STR("", text);

   TEST_CHECK(unifrog_text_ends_with_ci("Game.GBA", ".gba"));
   TEST_CHECK(!unifrog_text_ends_with_ci("Game.GB", ".gba"));
   TEST_CHECK(!unifrog_text_ends_with_ci(NULL, ".gba"));

   TEST_EQ_INT(0, unifrog_text_marquee(text, sizeof(text), "short", 8, 0));
   TEST_EQ_STR("short", text);
   TEST_EQ_INT(1,
      unifrog_text_marquee(text, sizeof(text), "abcdefghijkl", 8, 1080));
   TEST_EQ_STR("bcdefghi", text);

   TEST_EQ_INT(3, unifrog_text_utf8_length(japanese));
   TEST_EQ_INT(2,
      unifrog_text_utf8_copy_chars(text, sizeof(text), japanese, 2));
   TEST_EQ_STR("\xe6\x97\xa5\xe6\x9c\xac", text);
   TEST_EQ_INT(1,
      unifrog_text_marquee(text, sizeof(text), japanese, 2, 1080));
   TEST_EQ_STR("\xe6\x9c\xac\xe8\xaa\x9e", text);
}

static void test_paths(void)
{
   char path[32];

   unifrog_path_join(path, sizeof(path), "/ROMS", "GBA");
   TEST_EQ_STR("/ROMS/GBA", path);
   unifrog_path_join(path, sizeof(path), "/ROMS/", "GBA");
   TEST_EQ_STR("/ROMS/GBA", path);
   unifrog_path_join(path, sizeof(path), "", "GBA");
   TEST_EQ_STR("GBA", path);
   unifrog_path_join(path, 8, "/ROMS", "GBA");
   TEST_EQ_STR("/ROMS/G", path);
}

static void test_storage_profiles(void)
{
   const struct unifrog_storage_profile_info *profile;

   TEST_CHECK(unifrog_storage_profile_count() > 3u);
   TEST_EQ_STR("boot", unifrog_storage_profile_name(0));
   TEST_EQ_STR("auto", unifrog_storage_profile_name(1));
   TEST_CHECK(unifrog_storage_profile_name(
      unifrog_storage_profile_count()) == NULL);
   profile = unifrog_storage_profile_info("wide25");
   TEST_EQ_STR("wide25", profile->name);
   TEST_EQ_STR("4-bit", profile->bus_width);
   TEST_EQ_STR("3.3 V", profile->signal);

   profile = unifrog_storage_profile_info("uhs25");
   TEST_EQ_STR("UHS SDR25", profile->timing);
   TEST_EQ_STR("1.8 V", profile->signal);

   TEST_EQ_STR("boot", unifrog_storage_profile_info("missing")->name);
   TEST_EQ_STR("boot", unifrog_storage_profile_info(NULL)->name);
}

static void test_battery_calibration(void)
{
   struct unifrog_battery_calibration calibration;
   struct unifrog_battery_status status;

   unifrog_battery_calibration_defaults(&calibration);
   TEST_EQ_INT(1, unifrog_battery_calibration_valid(&calibration));
   unifrog_battery_set_calibration(&calibration);
   TEST_EQ_INT(0, unifrog_battery_percent_for_mv(3500));
   TEST_EQ_INT(25, unifrog_battery_percent_for_mv(3660));
   TEST_EQ_INT(50, unifrog_battery_percent_for_mv(3720));
   TEST_EQ_INT(75, unifrog_battery_percent_for_mv(3800));
   TEST_EQ_INT(100, unifrog_battery_percent_for_mv(4000));
   TEST_EQ_INT(4, unifrog_battery_bars_for_raw(200));

   calibration.millivolts[2] = calibration.millivolts[1];
   TEST_EQ_INT(0, unifrog_battery_calibration_valid(&calibration));
   unifrog_battery_calibration_defaults(&calibration);
   unifrog_battery_status_init(&status);
   TEST_EQ_INT(1, unifrog_battery_status_apply_sample(&status, 180, 3600,
      "test", 1));
   TEST_EQ_INT(0, status.low);
   (void)unifrog_battery_status_apply_sample(&status, 180, 3600,
      "test", 2);
   TEST_EQ_INT(0, status.low);
   (void)unifrog_battery_status_apply_sample(&status, 180, 3600,
      "test", 3);
   TEST_EQ_INT(1, status.low);
   (void)unifrog_battery_status_apply_sample(&status, 190, 3800,
      "test", 4);
   TEST_EQ_INT(1, status.low);
   (void)unifrog_battery_status_apply_sample(&status, 190, 3800,
      "test", 5);
   TEST_EQ_INT(0, status.low);
   TEST_EQ_INT(75, status.percent);
   TEST_EQ_INT(3, status.bars);
   TEST_EQ_INT(0, unifrog_battery_status_apply_sample(&status, 190, 3800,
      "test", 300001));
   TEST_CHECK(status.discharge_mv_per_hour > 0);
}

static void test_log_policy(void)
{
   TEST_EQ_STR("trace", unifrog_log_level_name(UNIFROG_LOG_TRACE));
   TEST_EQ_STR("error", unifrog_log_level_name(UNIFROG_LOG_ERROR));
   TEST_EQ_INT(UNIFROG_LOG_WARN,
      unifrog_log_level_from_name("WARN", UNIFROG_LOG_INFO));
   TEST_EQ_INT(UNIFROG_LOG_INFO,
      unifrog_log_level_from_name("invalid", UNIFROG_LOG_INFO));

   unifrog_log_set_level(UNIFROG_LOG_WARN);
   TEST_EQ_INT(0, unifrog_log_would_write(UNIFROG_LOG_INFO));
   TEST_EQ_INT(1, unifrog_log_would_write(UNIFROG_LOG_WARN));
   TEST_EQ_INT(1, unifrog_log_would_write(UNIFROG_LOG_ERROR));
   unifrog_log_set_level(UNIFROG_LOG_TRACE);
}

static void test_boot_path_policy(void)
{
   TEST_EQ_INT(1, unifrog_boot_asd_path_supported("firmware/stock.asd"));
   TEST_EQ_INT(1, unifrog_boot_asd_path_supported("unifrog_data/fw/alt.ASD"));
   TEST_EQ_INT(0, unifrog_boot_asd_path_supported("/firmware/stock.asd"));
   TEST_EQ_INT(0, unifrog_boot_asd_path_supported("firmware/../stock.asd"));
   TEST_EQ_INT(0, unifrog_boot_asd_path_supported("firmware//stock.asd"));
   TEST_EQ_INT(0, unifrog_boot_asd_path_supported("firmware/stock asd"));
   TEST_EQ_INT(0, unifrog_boot_asd_path_supported("firmware/stock.bin"));
}

static void test_zip_writer(void)
{
   static const char payload[] = "diagnostic-data\n";
   char path[96];
   char readback[sizeof(payload)];
   struct unifrog_zip_writer writer;
   struct unifrog_zip_archive archive;
   const struct unifrog_zip_entry *entry;
   FILE *out;

   snprintf(path, sizeof(path), "/tmp/unifrog-zip-test-%ld.zip", (long)getpid());
   unlink(path);
   TEST_EQ_INT(0, unifrog_zip_writer_open_path(path, &writer));
   TEST_EQ_INT(0, unifrog_zip_writer_add_data(&writer, "logs/report.txt",
      payload, sizeof(payload) - 1u));
   TEST_EQ_INT(-1, unifrog_zip_writer_add_data(&writer, "../unsafe.txt",
      payload, sizeof(payload) - 1u));
   TEST_EQ_INT(0, unifrog_zip_writer_close(&writer));
   TEST_EQ_INT(0, unifrog_zip_open_path(path, &archive));
   TEST_EQ_INT(1, unifrog_zip_entry_count(&archive));
   entry = unifrog_zip_find(&archive, "logs/report.txt");
   TEST_CHECK(entry != NULL);
   out = tmpfile();
   TEST_CHECK(out != NULL);
   if (entry && out) {
      TEST_EQ_INT(0, unifrog_zip_extract_entry_to_file(&archive, entry, out));
      rewind(out);
      memset(readback, 0, sizeof(readback));
      TEST_EQ_INT(sizeof(payload) - 1u,
         fread(readback, 1, sizeof(readback), out));
      TEST_EQ_STR(payload, readback);
      fclose(out);
   }
   unifrog_zip_close(&archive);
   unlink(path);
}

static void test_artwork_templates(void)
{
   struct unifrog_artwork_paths paths;
   char root[96];
   char system[128];
   char media[160];
   char image[192];
   char rom[160];
   FILE *file;

   snprintf(root, sizeof(root), "/tmp/unifrog-art-%ld", (long)getpid());
   snprintf(system, sizeof(system), "%s/GBA", root);
   snprintf(media, sizeof(media), "%s/media", system);
   snprintf(image, sizeof(image), "%s/images", media);
   snprintf(rom, sizeof(rom), "%s/Advance Wars.gba", system);
   (void)mkdir(root, 0777);
   (void)mkdir(system, 0777);
   (void)mkdir(media, 0777);
   (void)mkdir(image, 0777);
   snprintf(image, sizeof(image), "%s/media/images/Advance Wars.png", system);
   file = fopen(image, "wb");
   TEST_CHECK(file != NULL);
   if (file) {
      fputs("png", file);
      fclose(file);
   }
   TEST_EQ_INT(0, unifrog_artwork_resolve(rom, NULL,
      "{rom_dir}/missing/{filename}|{rom_dir}/media/images/{name}.png",
      "", "", &paths));
   TEST_EQ_STR(image, paths.box);
   TEST_EQ_STR("", paths.preview);
   unlink(image);
   rmdir(media); /* Expected to fail while images still exists. */
   snprintf(image, sizeof(image), "%s/media/images", system);
   rmdir(image);
   rmdir(media);
   rmdir(system);
   rmdir(root);
}

int main(void)
{
   test_text();
   test_paths();
   test_storage_profiles();
   test_battery_calibration();
   test_log_policy();
   test_boot_path_policy();
   test_zip_writer();
   test_artwork_templates();
   return test_finish("foundation");
}
