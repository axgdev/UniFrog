#include "test.h"

#include "frontend_internal.h"

#include <unifrog/config.h>

#include <stdarg.h>
#include <string.h>

int unifrog_log(const char *format, ...)
{
   (void)format;
   return 0;
}

int unifrog_config_read(const char *path, unifrog_config_entry_cb callback,
   void *userdata, unsigned *error_count)
{
   (void)path;
   (void)callback;
   (void)userdata;
   if (error_count)
      *error_count = 0;
   return -1;
}

int save_settings(struct frontend_state *fe)
{
   (void)fe;
   return 0;
}

char *frontend_trim_ascii(char *text)
{
   char *end;

   while (*text == ' ' || *text == '\t')
      text++;
   end = text + strlen(text);
   while (end > text && (end[-1] == ' ' || end[-1] == '\t'))
      *--end = '\0';
   return text;
}

static void set_association(struct frontend_state *fe, unsigned index,
   const char *extension, const char *handler)
{
   struct frontend_association *association = &fe->associations[index];

   unifrog_text_copy(association->extension,
      sizeof(association->extension), extension);
   unifrog_text_copy(association->handlers[0],
      sizeof(association->handlers[0]), handler);
   association->handler_count = 1;
   unifrog_text_copy(association->default_handler,
      sizeof(association->default_handler), handler);
   if (fe->association_count <= index)
      fe->association_count = index + 1u;
}

int main(void)
{
   static struct frontend_state fe;
   const struct frontend_association *association;

   memset(&fe, 0, sizeof(fe));
   set_association(&fe, 0, "png", "reader");
   set_association(&fe, 1, "p8.png", "fake08-prosty");

   association = frontend_association_for_path(&fe,
      "/ROMS/PICO/cart.p8.png");
   TEST_CHECK(association == &fe.associations[1]);
   association = frontend_association_for_path(&fe,
      "/ROMS/PICO/CART.P8.PNG");
   TEST_CHECK(association == &fe.associations[1]);
   association = frontend_association_for_path(&fe, "/Pictures/cart.png");
   TEST_CHECK(association == &fe.associations[0]);
   association = frontend_association_for_path(&fe, "/Pictures/cartp8.png");
   TEST_CHECK(association == &fe.associations[0]);
   TEST_CHECK(frontend_association_for_path(&fe, "/Pictures/png") == NULL);

   TEST_EQ_INT(0, frontend_association_set_default(&fe,
      "/ROMS/PICO/cart.p8.png", "other-core"));
   TEST_EQ_STR("other-core", fe.associations[1].default_handler);
   TEST_EQ_STR("other-core", fe.associations[1].handlers[0]);
   TEST_EQ_STR("reader", fe.associations[0].default_handler);

   return test_finish("file associations");
}
