#include <unifrog/log.h>

#include <string.h>
#include <strings.h>

static enum unifrog_log_level active_level = UNIFROG_LOG_TRACE;

static const char *const level_names[] = {
   "trace", "debug", "info", "warn", "error", "off",
};

const char *unifrog_log_level_name(enum unifrog_log_level level)
{
   return (unsigned)level < sizeof(level_names) / sizeof(level_names[0]) ?
      level_names[level] : "info";
}

enum unifrog_log_level unifrog_log_level_from_name(const char *name,
   enum unifrog_log_level fallback)
{
   if (!name || !name[0])
      return fallback;
   for (unsigned i = 0; i < sizeof(level_names) / sizeof(level_names[0]); i++) {
      if (strcasecmp(name, level_names[i]) == 0)
         return (enum unifrog_log_level)i;
   }
   return fallback;
}

void unifrog_log_set_level(enum unifrog_log_level level)
{
   active_level = (unsigned)level <= UNIFROG_LOG_OFF ? level :
      UNIFROG_LOG_TRACE;
}

enum unifrog_log_level unifrog_log_get_level(void)
{
   return active_level;
}

int unifrog_log_would_write(enum unifrog_log_level level)
{
   return active_level != UNIFROG_LOG_OFF && level >= active_level &&
      level < UNIFROG_LOG_OFF;
}
