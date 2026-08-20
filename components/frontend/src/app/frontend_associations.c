#include "frontend_internal.h"

#include <unifrog/config.h>

static const char *path_extension_name(const char *path)
{
   const char *dot = path ? strrchr(path, '.') : NULL;
   const char *slash = path ? strrchr(path, '/') : NULL;

   if (!dot || (slash && dot < slash) || !dot[1])
      return "";
   return dot + 1;
}

static struct frontend_association *association_find_mutable(
   struct frontend_state *fe, const char *extension)
{
   if (!fe || !extension || !extension[0])
      return NULL;
   for (unsigned i = 0; i < fe->association_count; i++) {
      if (strcasecmp(fe->associations[i].extension, extension) == 0)
         return &fe->associations[i];
   }
   return NULL;
}

const struct frontend_association *frontend_association_for_path(
   const struct frontend_state *fe, const char *path)
{
   const struct frontend_association *best = NULL;
   const char *slash;
   const char *name;
   size_t name_len;
   size_t best_len = 0;

   if (!fe || !path)
      return NULL;
   slash = strrchr(path, '/');
   name = slash ? slash + 1 : path;
   name_len = strlen(name);
   for (unsigned i = 0; i < fe->association_count; i++) {
      const struct frontend_association *candidate = &fe->associations[i];
      size_t length = strlen(candidate->extension);

      if (length > best_len && name_len > length &&
          name[name_len - length - 1u] == '.' &&
          strcasecmp(name + name_len - length, candidate->extension) == 0) {
         best = candidate;
         best_len = length;
      }
   }
   return best;
}

static struct frontend_association *association_add(struct frontend_state *fe,
   const char *extension)
{
   struct frontend_association *association;

   if (!fe || !extension || strlen(extension) >=
       sizeof(fe->associations[0].extension))
      return NULL;
   association = association_find_mutable(fe, extension);
   if (association)
      return association;
   if (!fe || fe->association_count >= FRONTEND_ASSOCIATION_MAX)
      return NULL;
   association = &fe->associations[fe->association_count++];
   memset(association, 0, sizeof(*association));
   unifrog_text_copy(association->extension, sizeof(association->extension),
      extension);
   return association;
}

static void association_parse_handlers(struct frontend_association *association,
   const char *text, unsigned line_number)
{
   char copy[FRONTEND_MAX_LINE];
   char *part;

   if (!association)
      return;
   association->handler_count = 0;
   unifrog_text_copy(copy, sizeof(copy), text ? text : "");
   part = copy;
   while (part && part[0] &&
          association->handler_count < FRONTEND_ASSOCIATION_HANDLER_MAX) {
      char *next = strchr(part, ',');
      char *handler = frontend_trim_ascii(part);

      if (next)
         *next++ = '\0';
      if (handler[0]) {
         int duplicate = 0;

         if (strlen(handler) >= sizeof(association->handlers[0])) {
            unifrog_log("frontend associations invalid_handler line=%u\n",
               line_number);
            part = next;
            continue;
         }
         for (unsigned i = 0; i < association->handler_count; i++) {
            if (strcmp(association->handlers[i], handler) == 0) {
               duplicate = 1;
               break;
            }
         }
         if (!duplicate)
            unifrog_text_copy(
               association->handlers[association->handler_count++],
               sizeof(association->handlers[0]), handler);
      }
      part = next;
   }
   if (part && part[0])
      unifrog_log("frontend associations too_many_handlers line=%u max=%u\n",
         line_number, FRONTEND_ASSOCIATION_HANDLER_MAX);
}

static int association_config_entry(void *userdata, const char *section,
   const char *key, const char *value, unsigned line_number)
{
   struct frontend_state *fe = userdata;
   const char *name;
   const char *field;
   size_t name_length;
   char extension[sizeof(fe->associations[0].extension)];
   struct frontend_association *association;

   if (section[0] || strncmp(key, "extension.", 10) != 0)
      return 0;
   name = key + 10;
   field = strrchr(name, '.');
   if (!field)
      return 0;
   name_length = (size_t)(field - name);
   field++;
   if (strcmp(field, "handlers") != 0 && strcmp(field, "default") != 0)
      return 0;
   if (!name_length || name_length >= sizeof(extension)) {
      unifrog_log("frontend associations invalid_extension line=%u\n",
         line_number);
      return 0;
   }
   memcpy(extension, name, name_length);
   extension[name_length] = '\0';
   association = association_add(fe, extension);
   if (!association) {
      unifrog_log("frontend associations capacity=%u line=%u\n",
         FRONTEND_ASSOCIATION_MAX, line_number);
      return 0;
   }
   if (strcmp(field, "handlers") == 0)
      association_parse_handlers(association, value, line_number);
   else if (strlen(value) < sizeof(association->default_handler))
      unifrog_text_copy(association->default_handler,
         sizeof(association->default_handler), value);
   else
      unifrog_log("frontend associations invalid_default line=%u\n",
         line_number);
   return 0;
}

static int associations_load_file(struct frontend_state *fe,
   const char *path)
{
   unsigned errors = 0;
   int ret;

   ret = unifrog_config_read(path, association_config_entry, fe, &errors);
   if (ret != 0) {
      unifrog_log("frontend associations open_failed path=%s ret=%d errno=%d\n",
         path ? path : "", ret, errno);
      return ret;
   }
   if (errors)
      unifrog_log("frontend associations parse_errors=%u path=%s\n", errors,
         path);
   return 0;
}

void frontend_associations_load(struct frontend_state *fe)
{
   if (!fe)
      return;
   memset(fe->associations, 0, sizeof(fe->associations));
   fe->association_count = 0;
   (void)associations_load_file(fe, UNIFROG_DEFAULT_CONFIG_PATH);
   (void)associations_load_file(fe, UNIFROG_CONFIG_PATH);
   unifrog_log("frontend associations count=%u\n", fe->association_count);
}

int frontend_association_set_default(struct frontend_state *fe,
   const char *path, const char *handler)
{
   struct frontend_association *association;
   const struct frontend_association *matched;
   const char *extension = path_extension_name(path);

   if (!extension[0] || !handler || !handler[0])
      return -1;
   matched = frontend_association_for_path(fe, path);
   association = association_add(fe,
      matched ? matched->extension : extension);
   if (!association)
      return -1;
   unifrog_text_copy(association->default_handler,
      sizeof(association->default_handler), handler);
   for (unsigned i = 0; i < association->handler_count; i++) {
      if (strcmp(association->handlers[i], handler) == 0)
         return frontend_associations_save(fe);
   }
   {
      unsigned last = association->handler_count <
         FRONTEND_ASSOCIATION_HANDLER_MAX ? association->handler_count :
         FRONTEND_ASSOCIATION_HANDLER_MAX - 1u;

      for (unsigned i = last; i > 0; i--)
         unifrog_text_copy(association->handlers[i],
            sizeof(association->handlers[i]), association->handlers[i - 1u]);
      unifrog_text_copy(association->handlers[0],
         sizeof(association->handlers[0]), handler);
      if (association->handler_count < FRONTEND_ASSOCIATION_HANDLER_MAX)
         association->handler_count++;
   }
   return frontend_associations_save(fe);
}

int frontend_associations_save(const struct frontend_state *fe)
{
   if (!fe)
      return -1;
   return save_settings((struct frontend_state *)fe);
}
