#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <unifrog/abi.h>
#include <unifrog/audio.h>

#define MEDIA_MODULE_LOG_BUFFER 512u

const struct unifrog_abi *unifrog_core_module_abi;

void *__real_memcpy(void *dest, const void *src, size_t len)
{
   return memcpy(dest, src, len);
}

void *__real_memmove(void *dest, const void *src, size_t len)
{
   return memmove(dest, src, len);
}

void *__real_memset(void *dest, int ch, size_t len)
{
   return memset(dest, ch, len);
}

static const struct unifrog_abi *module_abi(void)
{
   const struct unifrog_abi *abi = unifrog_core_module_abi;

   if (!abi || abi->magic != UNIFROG_ABI_MAGIC ||
       abi->size < sizeof(struct unifrog_abi))
      return NULL;
   return abi;
}

int unifrog_log(const char *fmt, ...)
{
   const struct unifrog_abi *abi = module_abi();
   char buffer[MEDIA_MODULE_LOG_BUFFER];
   va_list ap;
   int ret;

   if (!abi || !abi->vsnprintf || !abi->log_message)
      return -1;

   va_start(ap, fmt);
   ret = abi->vsnprintf(buffer, sizeof(buffer), fmt ? fmt : "", ap);
   va_end(ap);
   buffer[sizeof(buffer) - 1] = '\0';
   (void)abi->log_message(buffer);
   return ret;
}

int unifrog_log_flush(void)
{
   const struct unifrog_abi *abi = module_abi();

   return abi && abi->log_flush ? abi->log_flush() : -1;
}

size_t unifrog_log_auto_flush_bytes(void)
{
   const struct unifrog_abi *abi = module_abi();

   return abi && abi->log_auto_flush_bytes ?
      abi->log_auto_flush_bytes() : 0;
}

void unifrog_log_set_auto_flush_bytes(size_t bytes)
{
   const struct unifrog_abi *abi = module_abi();

   if (abi && abi->log_set_auto_flush_bytes)
      abi->log_set_auto_flush_bytes(bytes);
}

void unifrog_audio_set_system_output_enabled(int enabled)
{
   const struct unifrog_abi *abi = module_abi();

   if (abi && abi->audio_set_system_output_enabled)
      abi->audio_set_system_output_enabled(enabled);
}

void unifrog_audio_debug_dump(struct unifrog_audio *audio, const char *tag)
{
   const struct unifrog_abi *abi = module_abi();

   if (abi && abi->audio_debug_dump)
      abi->audio_debug_dump(audio, tag);
}

void unifrog_input_save_previous(void)
{
   const struct unifrog_abi *abi = module_abi();

   if (abi && abi->input_save_previous)
      abi->input_save_previous();
}

void unifrog_input_poll_with_wireless_divisor(unsigned wireless_divisor)
{
   const struct unifrog_abi *abi = module_abi();

   if (abi && abi->input_poll_with_wireless_divisor)
      abi->input_poll_with_wireless_divisor(wireless_divisor);
}

uint32_t unifrog_input_menu_buttons(void)
{
   const struct unifrog_abi *abi = module_abi();

   return abi && abi->input_menu_buttons ? abi->input_menu_buttons() : 0;
}

static int ascii_tolower(int ch)
{
   if (ch >= 'A' && ch <= 'Z')
      return ch + ('a' - 'A');
   return ch;
}

int unifrog_text_ends_with_ci(const char *text, const char *suffix)
{
   size_t text_len = 0;
   size_t suffix_len = 0;
   const char *tail;

   if (!text || !suffix)
      return 0;
   while (text[text_len])
      text_len++;
   while (suffix[suffix_len])
      suffix_len++;
   if (suffix_len > text_len)
      return 0;

   tail = text + text_len - suffix_len;
   for (size_t i = 0; i < suffix_len; i++) {
      if (ascii_tolower((unsigned char)tail[i]) !=
          ascii_tolower((unsigned char)suffix[i]))
         return 0;
   }
   return 1;
}

void unifrog_exception_panic(uint32_t cause, uint32_t epc, uint32_t badvaddr,
   uint32_t ra)
{
   const struct unifrog_abi *abi = module_abi();

   (void)cause;
   (void)epc;
   (void)badvaddr;
   (void)ra;
   if (abi && abi->log_message)
      (void)abi->log_message("unifrog media module exception\n");
   for (;;)
      ;
}
