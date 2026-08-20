#include "test.h"

#include <stdint.h>

#include <unifrog/media_policy.h>
#include <unifrog/media_content.h>
#include <unifrog/media.h>
#include <unifrog/reader.h>

static void test_lookup(void)
{
   struct unifrog_media_readahead_slot slots[] = {
      { 100, 0, 3 },
      { 50, 200, 1 },
      { 0, 0, 0 },
   };

   TEST_EQ_INT(0, unifrog_media_policy_find_slot(slots, 3, 0, 0));
   TEST_EQ_INT(0, unifrog_media_policy_find_slot(slots, 3, 99, 0));
   TEST_EQ_INT(-1, unifrog_media_policy_find_slot(slots, 3, 100, 0));
   TEST_EQ_INT(0, unifrog_media_policy_find_slot(slots, 3, 100, 1));
   TEST_EQ_INT(1, unifrog_media_policy_find_slot(slots, 3, 225, 0));
}

static void test_lru(void)
{
   struct unifrog_media_readahead_slot slots[] = {
      { 100, 0, 3 },
      { 50, 200, 1 },
      { 0, 0, 0 },
   };
   int evicted = -1;
   uint32_t clock;

   TEST_EQ_INT(2, unifrog_media_policy_choose_slot(slots, 3, &evicted));
   TEST_EQ_INT(0, evicted);
   slots[2].size = 25;
   slots[2].last_used = 2;
   TEST_EQ_INT(1, unifrog_media_policy_choose_slot(slots, 3, &evicted));
   TEST_EQ_INT(1, evicted);

   clock = unifrog_media_policy_touch(slots, 3, 3, 1);
   TEST_EQ_INT(4, clock);
   TEST_EQ_INT(4, slots[1].last_used);
   clock = unifrog_media_policy_touch(slots, 3, UINT32_MAX, 2);
   TEST_EQ_INT(1, clock);
   TEST_EQ_INT(0, slots[0].last_used);
   TEST_EQ_INT(1, slots[2].last_used);
}

static void test_content_types(void)
{
   TEST_EQ_INT(1, unifrog_media_path_is_supported("movie.MKV"));
   TEST_EQ_INT(1, unifrog_media_path_is_supported("cover.webp"));
   TEST_EQ_INT(1, unifrog_media_path_is_audio("track.OpUs"));
   TEST_EQ_INT(1, unifrog_media_path_is_audio("stream.adts"));
   TEST_EQ_INT(0, unifrog_media_path_is_audio("movie.rmvb"));
   TEST_EQ_INT(1, unifrog_media_path_is_supported("movie.rmvb"));
   TEST_EQ_INT(1, unifrog_media_path_is_image("photo.JPEG"));
   TEST_EQ_INT(1, unifrog_media_path_is_wav("tone.WAV"));
   TEST_EQ_INT(1, unifrog_media_path_is_aac("stream.adts"));
   TEST_EQ_INT(0, unifrog_media_path_is_supported("notes.txt"));
   TEST_EQ_INT(0, unifrog_media_path_is_audio(NULL));
   TEST_EQ_INT(1, unifrog_reader_path_is_image("scan.TIFF"));
   TEST_EQ_INT(1, unifrog_reader_path_is_text("notes.md"));
   TEST_EQ_INT(1, unifrog_reader_path_is_archive("book.EPUB"));
   TEST_EQ_INT(1, unifrog_reader_path_supported("page.xhtml"));
   TEST_EQ_INT(0, unifrog_reader_path_supported("movie.mkv"));
}

static void test_routes(void)
{
   enum unifrog_media_route route = UNIFROG_MEDIA_ROUTE_AUTO;

   TEST_EQ_INT(0, unifrog_media_route_parse("native", &route));
   TEST_EQ_INT(UNIFROG_MEDIA_ROUTE_NATIVE, route);
   TEST_EQ_STR("native", unifrog_media_route_name(route));
   TEST_EQ_INT(0, unifrog_media_route_parse("media", &route));
   TEST_EQ_INT(UNIFROG_MEDIA_ROUTE_AUTO, route);
   TEST_EQ_INT(0, unifrog_media_route_parse("hcplayer-muted", &route));
   TEST_EQ_INT(UNIFROG_MEDIA_ROUTE_HCPLAYER_MUTED, route);
   TEST_EQ_INT(-1, unifrog_media_route_parse("missing", &route));

   TEST_EQ_INT(1, unifrog_media_route_available("movie.mkv",
      UNIFROG_MEDIA_ROUTE_NATIVE, 0));
   TEST_EQ_INT(1, unifrog_media_route_available("movie.mkv",
      UNIFROG_MEDIA_ROUTE_FFMPEG, 0));
   TEST_EQ_INT(0, unifrog_media_route_available("cover.png",
      UNIFROG_MEDIA_ROUTE_NATIVE, 1));
   TEST_EQ_INT(1, unifrog_media_route_available("tone.wav",
      UNIFROG_MEDIA_ROUTE_WAV_AUDDEC, 0));
   TEST_EQ_INT(0, unifrog_media_route_available("track.mp3",
      UNIFROG_MEDIA_ROUTE_WAV_AUDDEC, 1));
   TEST_EQ_INT(0, unifrog_media_route_available("track.mp3",
      UNIFROG_MEDIA_ROUTE_HCPLAYER_MUTED, 1));
   TEST_EQ_INT(1, unifrog_media_route_available("movie.mp4",
      UNIFROG_MEDIA_ROUTE_HCPLAYER_MUTED, 1));
   TEST_EQ_INT(0, unifrog_media_route_available("movie.mp4",
      UNIFROG_MEDIA_ROUTE_HCPLAYER, 0));
   TEST_EQ_INT(1, unifrog_media_route_available("unusual.content",
      UNIFROG_MEDIA_ROUTE_AUTO, 0));
   TEST_EQ_INT(1, unifrog_media_route_available("unusual.content",
      UNIFROG_MEDIA_ROUTE_FFMPEG, 0));
   TEST_EQ_INT(0, unifrog_media_route_available("unusual.content",
      UNIFROG_MEDIA_ROUTE_WAV_AUDDEC, 1));
}

int main(void)
{
   test_lookup();
   test_lru();
   test_content_types();
   test_routes();
   return test_finish("media policy");
}
