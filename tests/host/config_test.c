#include "test.h"

#include <stdlib.h>
#include <unistd.h>

#include <unifrog/config.h>
#include <unifrog/frontend_config.h>

struct parsed_config {
   char section[8][64];
   char key[8][32];
   char value[8][96];
   unsigned count;
};

static int collect_entry(void *userdata, const char *section, const char *key,
   const char *value, unsigned line_number)
{
   struct parsed_config *parsed = userdata;
   unsigned index = parsed->count++;

   (void)line_number;
   if (index >= 8u)
      return 1;
   snprintf(parsed->section[index], sizeof(parsed->section[index]), "%s",
      section);
   snprintf(parsed->key[index], sizeof(parsed->key[index]), "%s", key);
   snprintf(parsed->value[index], sizeof(parsed->value[index]), "%s", value);
   return 0;
}

static void write_text(const char *path, const char *text)
{
   FILE *file = fopen(path, "wb");

   TEST_CHECK(file != NULL);
   if (!file)
      return;
   TEST_EQ_INT((long)strlen(text), (long)fwrite(text, 1, strlen(text), file));
   TEST_EQ_INT(0, fclose(file));
}

static void test_parser(const char *path)
{
   struct parsed_config parsed;
   unsigned errors = 0;

   memset(&parsed, 0, sizeof(parsed));
   write_text(path,
      "\xef\xbb\xbf # UTF-8 BOM and comment\n"
      " plain = value ; inline comment\n"
      "quoted = \"value # retained\" # comment\n"
      "[ core.gambatte ] ; section comment\n"
      "frameskip = 2\n"
      "apostrophe='Game Boy; Color'\n"
      "malformed line\n");
   TEST_EQ_INT(0, unifrog_config_read(path, collect_entry, &parsed, &errors));
   TEST_EQ_INT(4, parsed.count);
   TEST_EQ_INT(1, errors);
   TEST_EQ_STR("", parsed.section[0]);
   TEST_EQ_STR("plain", parsed.key[0]);
   TEST_EQ_STR("value", parsed.value[0]);
   TEST_EQ_STR("value # retained", parsed.value[1]);
   TEST_EQ_STR("core.gambatte", parsed.section[2]);
   TEST_EQ_STR("2", parsed.value[2]);
   TEST_EQ_STR("Game Boy; Color", parsed.value[3]);
}

static void test_scoped_precedence(const char *path)
{
   struct unifrog_frontend_config config;
   struct unifrog_libretro_run_options options;
   unsigned errors = 0;

   write_text(path,
      "[core.gambatte]\n"
      "frameskip=2\n"
      "audio=0\n"
      "rtc_offset_minutes=1440\n"
      "gain=99\n"
      "core=ignored\n"
      "[core.gearboy]\n"
      "frameskip=3\n"
      "cpu=702\n"
      "[core.invalid]\n"
      "cpu=800\n"
      "[rom./ROMS/Game Boy/Tetris.gb]\n"
      "core=gearboy\n"
      "audio=1\n"
      "rtc_offset_minutes=-2880\n"
      "[rom./ROMS/Game Boy/Tetris.gb]\n"
      "display=2\n");
   TEST_EQ_INT(0, unifrog_frontend_config_load(&config, path, &errors));
   TEST_EQ_INT(0, errors);
   TEST_EQ_INT(4, config.count);

   memset(&options, 0, sizeof(options));
   options.audio_enabled = 1;
   options.audio_gain = 2;
   options.frameskip = 1;
   options.scpu_mhz = 918;
   unifrog_frontend_config_apply(&config, "gambatte",
      "/ROMS/Game Boy/Tetris.gb", &options);
   TEST_EQ_STR("gearboy", options.core_id);
   TEST_EQ_INT(3, options.frameskip);
   TEST_EQ_INT(702, options.scpu_mhz);
   TEST_EQ_INT(1, options.audio_enabled);
   TEST_EQ_INT(2, options.audio_gain);
   TEST_EQ_INT(2, options.display_mode);
   TEST_EQ_INT(-2880, options.rtc_offset_minutes);

   memset(&options, 0, sizeof(options));
   options.audio_enabled = 1;
   options.frameskip = 1;
   unifrog_frontend_config_apply(&config, "gambatte",
      "/ROMS/Game Boy/Other.gb", &options);
   TEST_EQ_STR("gambatte", options.core_id);
   TEST_EQ_INT(2, options.frameskip);
   TEST_EQ_INT(0, options.audio_enabled);
   TEST_EQ_INT(1440, options.rtc_offset_minutes);

   memset(&options, 0, sizeof(options));
   options.scpu_mhz = 918;
   unifrog_frontend_config_apply(&config, "invalid", NULL, &options);
   TEST_EQ_INT(918, options.scpu_mhz);
}

static int write_replacement(FILE *file, void *userdata)
{
   return fprintf(file, "new_key=%s\n", (const char *)userdata) < 0 ? -1 : 0;
}

static void test_scoped_capacity(void)
{
   struct unifrog_frontend_config config;
   char section[32];

   unifrog_frontend_config_init(&config);
   for (unsigned i = 0; i < UNIFROG_FRONTEND_SCOPED_CONFIG_MAX + 1u; i++) {
      snprintf(section, sizeof(section), "core.core%u", i);
      TEST_EQ_INT(0, unifrog_frontend_config_parse_entry(&config, section,
         "audio", "1"));
   }
   TEST_EQ_INT(UNIFROG_FRONTEND_SCOPED_CONFIG_MAX, config.count);
   TEST_EQ_INT(1, config.overflowed);
}

static void test_section_replacement(const char *path)
{
   char buffer[512];
   FILE *file;
   size_t bytes;

   write_text(path,
      "# keep this comment\n"
      "global=value\n"
      "[replace.me]\n"
      "old_key=old\n"
      "[keep.me]\n"
      "keep_key=keep\n");
   TEST_EQ_INT(0, unifrog_config_replace_section(path, "replace.me",
      write_replacement, "new"));
   file = fopen(path, "rb");
   TEST_CHECK(file != NULL);
   if (!file)
      return;
   bytes = fread(buffer, 1, sizeof(buffer) - 1u, file);
   buffer[bytes] = '\0';
   fclose(file);
   TEST_CHECK(strstr(buffer, "# keep this comment") != NULL);
   TEST_CHECK(strstr(buffer, "global=value") != NULL);
   TEST_CHECK(strstr(buffer, "[keep.me]\nkeep_key=keep") != NULL);
   TEST_CHECK(strstr(buffer, "old_key=old") == NULL);
   TEST_CHECK(strstr(buffer, "[replace.me]\nnew_key=new") != NULL);

   TEST_EQ_INT(0, unifrog_config_remove_section(path, "replace.me"));
   file = fopen(path, "rb");
   TEST_CHECK(file != NULL);
   if (!file)
      return;
   bytes = fread(buffer, 1, sizeof(buffer) - 1u, file);
   buffer[bytes] = '\0';
   fclose(file);
   TEST_CHECK(strstr(buffer, "[replace.me]") == NULL);
   TEST_CHECK(strstr(buffer, "[keep.me]\nkeep_key=keep") != NULL);
}

int main(void)
{
   char path[96];

   snprintf(path, sizeof(path), "/tmp/unifrog-config-test-%ld.ini",
      (long)getpid());
   test_parser(path);
   test_scoped_precedence(path);
   test_scoped_capacity();
   test_section_replacement(path);
   unlink(path);
   return test_finish("unified configuration");
}
