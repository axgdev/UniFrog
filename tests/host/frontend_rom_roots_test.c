#include "test.h"

#include "frontend_internal.h"

#include <string.h>

int unifrog_config_commit(const char *temporary, const char *path)
{
   (void)temporary;
   (void)path;
   return 0;
}

static void reset_frontend(struct frontend_state *fe)
{
   memset(fe, 0, sizeof(*fe));
   unifrog_text_copy(fe->rom_root, sizeof(fe->rom_root),
      FRONTEND_ROMS_ROOT);
   unifrog_text_copy(fe->rom_root_label, sizeof(fe->rom_root_label),
      "ROMs");
}

int main(void)
{
   static struct frontend_state fe;
   const char *games_root = FRONTEND_ROOT "/games/ROMS";

   reset_frontend(&fe);
   TEST_EQ_INT(0, frontend_rom_root_add(&fe, FRONTEND_ROMS_ROOT));
   TEST_EQ_INT(1, frontend_rom_root_count(&fe));
   TEST_EQ_STR(FRONTEND_ROMS_ROOT, frontend_rom_root(&fe));
   TEST_EQ_STR(FRONTEND_ROMS_ROOT, frontend_rom_root_at(&fe, 0));
   TEST_EQ_STR("ROMs", frontend_rom_root_label(&fe));

   TEST_EQ_INT(0, frontend_rom_root_add(&fe, games_root));
   TEST_EQ_INT(2, frontend_rom_root_count(&fe));
   TEST_EQ_STR(FRONTEND_ROMS_ROOT, frontend_rom_root_at(&fe, 0));
   TEST_EQ_STR(games_root, frontend_rom_root_at(&fe, 1));

   TEST_EQ_INT(0, frontend_rom_root_set_primary(&fe, games_root));
   TEST_EQ_INT(2, frontend_rom_root_count(&fe));
   TEST_EQ_STR(games_root, frontend_rom_root(&fe));
   TEST_EQ_STR(games_root, frontend_rom_root_at(&fe, 0));
   TEST_EQ_STR(FRONTEND_ROMS_ROOT, frontend_rom_root_at(&fe, 1));
   TEST_EQ_STR("games/ROMS", frontend_rom_root_label(&fe));

   TEST_EQ_INT(0, frontend_rom_root_set_primary(&fe, FRONTEND_ROOT));
   TEST_EQ_INT(3, frontend_rom_root_count(&fe));
   TEST_EQ_STR(FRONTEND_ROOT, frontend_rom_root(&fe));
   TEST_EQ_STR(FRONTEND_ROOT, frontend_rom_root_at(&fe, 0));
   TEST_EQ_STR(games_root, frontend_rom_root_at(&fe, 1));
   TEST_EQ_STR(FRONTEND_ROMS_ROOT, frontend_rom_root_at(&fe, 2));
   TEST_EQ_STR("SD", frontend_rom_root_label(&fe));

   reset_frontend(&fe);
   TEST_EQ_INT(0, frontend_rom_root_add(&fe, FRONTEND_ROMS_ROOT));
   TEST_EQ_INT(0, frontend_rom_root_add(&fe, games_root));
   frontend_cycle_rom_root(&fe, 1);
   TEST_EQ_STR(games_root, frontend_rom_root(&fe));
   TEST_EQ_STR(games_root, frontend_rom_root_at(&fe, 0));
   TEST_EQ_STR(FRONTEND_ROMS_ROOT, frontend_rom_root_at(&fe, 1));
   TEST_EQ_STR("games/ROMS", frontend_rom_root_label(&fe));

   frontend_cycle_rom_root(&fe, 1);
   TEST_EQ_STR(FRONTEND_ROOT, frontend_rom_root(&fe));
   TEST_EQ_INT(3, frontend_rom_root_count(&fe));
   TEST_EQ_STR(FRONTEND_ROOT, frontend_rom_root_at(&fe, 0));
   TEST_EQ_STR(games_root, frontend_rom_root_at(&fe, 1));
   TEST_EQ_STR(FRONTEND_ROMS_ROOT, frontend_rom_root_at(&fe, 2));
   TEST_EQ_STR("SD", frontend_rom_root_label(&fe));

   return test_finish("frontend ROM roots");
}
