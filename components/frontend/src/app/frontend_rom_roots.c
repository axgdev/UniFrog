#include "frontend_internal.h"

static const char *const frontend_rom_root_presets[] = {
   FRONTEND_ROMS_ROOT,
   FRONTEND_ROOT "/games/ROMS",
   FRONTEND_ROOT,
};

static void frontend_rom_root_update_label(struct frontend_state *fe,
   const char *root)
{
   if (strcmp(root, FRONTEND_ROOT) == 0)
      unifrog_text_copy(fe->rom_root_label, sizeof(fe->rom_root_label), "SD");
   else if (strcmp(root, FRONTEND_ROOT "/games/ROMS") == 0)
      unifrog_text_copy(fe->rom_root_label, sizeof(fe->rom_root_label),
         "games/ROMS");
   else if (strcmp(root, FRONTEND_ROMS_ROOT) == 0)
      unifrog_text_copy(fe->rom_root_label, sizeof(fe->rom_root_label),
         "ROMs");
   else
      unifrog_text_copy(fe->rom_root_label, sizeof(fe->rom_root_label),
         frontend_basename(root));
}

unsigned frontend_rom_root_preset_count(void)
{
   return ARRAY_SIZE(frontend_rom_root_presets);
}

const char *frontend_rom_root_preset_at(unsigned index)
{
   if (index < ARRAY_SIZE(frontend_rom_root_presets))
      return frontend_rom_root_presets[index];
   return NULL;
}

/* Frontend ROM root state, labels, and membership helpers. */
const char *frontend_rom_root(const struct frontend_state *fe)
{
   return fe && fe->rom_root[0] ? fe->rom_root : FRONTEND_ROMS_ROOT;
}

const char *frontend_rom_root_label(const struct frontend_state *fe)
{
   return fe && fe->rom_root_label[0] ? fe->rom_root_label : "ROMs";
}

const char *frontend_rom_root_at(const struct frontend_state *fe,
   unsigned index)
{
   if (fe && index < fe->rom_root_count && fe->rom_roots[index][0])
      return fe->rom_roots[index];
   return index == 0 ? frontend_rom_root(fe) : NULL;
}

unsigned frontend_rom_root_count(const struct frontend_state *fe)
{
   if (fe && fe->rom_root_count)
      return fe->rom_root_count;
   return 1u;
}

int frontend_rom_root_index(const struct frontend_state *fe,
   const char *path)
{
   unsigned count = frontend_rom_root_count(fe);

   if (!path || !path[0])
      return -1;
   for (unsigned i = 0; i < count; i++) {
      const char *root = frontend_rom_root_at(fe, i);

      if (root && strcmp(root, path) == 0)
         return (int)i;
   }
   return -1;
}

void frontend_rom_root_sync_primary(struct frontend_state *fe)
{
   const char *root;

   if (!fe)
      return;
   if (!fe->rom_root_count && fe->rom_root[0])
      unifrog_text_copy(fe->rom_roots[fe->rom_root_count++],
         sizeof(fe->rom_roots[0]), fe->rom_root);
   if (!fe->rom_root_count)
      unifrog_text_copy(fe->rom_roots[fe->rom_root_count++],
         sizeof(fe->rom_roots[0]), FRONTEND_ROMS_ROOT);
   root = fe->rom_roots[0];
   unifrog_text_copy(fe->rom_root, sizeof(fe->rom_root), root);
   frontend_rom_root_update_label(fe, root);
}

int frontend_rom_root_set_primary(struct frontend_state *fe,
   const char *path)
{
   char normalized[FRONTEND_MAX_PATH];
   int index;

   if (!fe || frontend_normalize_path(normalized, sizeof(normalized),
       path) != 0)
      return -1;
   if (!fe->rom_root_count) {
      unifrog_text_copy(fe->rom_roots[0], sizeof(fe->rom_roots[0]),
         normalized);
      fe->rom_root_count = 1u;
      frontend_rom_root_sync_primary(fe);
      return 0;
   }
   index = frontend_rom_root_index(fe, normalized);
   if (index > 0) {
      for (unsigned i = (unsigned)index; i > 0; i--)
         unifrog_text_copy(fe->rom_roots[i], sizeof(fe->rom_roots[0]),
            fe->rom_roots[i - 1u]);
   } else if (index < 0) {
      unsigned first = fe->rom_root_count;

      if (first < FRONTEND_ROM_ROOT_MAX)
         fe->rom_root_count++;
      else
         first--;
      for (unsigned i = first; i > 0; i--)
         unifrog_text_copy(fe->rom_roots[i], sizeof(fe->rom_roots[0]),
            fe->rom_roots[i - 1u]);
   }
   unifrog_text_copy(fe->rom_roots[0], sizeof(fe->rom_roots[0]),
      normalized);
   frontend_rom_root_sync_primary(fe);
   return 0;
}

int frontend_rom_root_add(struct frontend_state *fe, const char *path)
{
   char normalized[FRONTEND_MAX_PATH];

   if (!fe || frontend_normalize_path(normalized, sizeof(normalized),
       path) != 0)
      return -1;
   if (fe->rom_root_count && frontend_rom_root_index(fe, normalized) >= 0)
      return 0;
   if (fe->rom_root_count >= FRONTEND_ROM_ROOT_MAX)
      return -1;
   unifrog_text_copy(fe->rom_roots[fe->rom_root_count++],
      sizeof(fe->rom_roots[0]), normalized);
   frontend_rom_root_sync_primary(fe);
   return 0;
}

void frontend_cycle_rom_root(struct frontend_state *fe, int dir)
{
   unsigned index = 0;

   if (!fe)
      return;
   for (unsigned i = 0; i < ARRAY_SIZE(frontend_rom_root_presets); i++) {
      if (strcmp(frontend_rom_root(fe), frontend_rom_root_presets[i]) == 0)
         index = i;
   }
   if (dir < 0)
      index = index == 0 ? ARRAY_SIZE(frontend_rom_root_presets) - 1u :
         index - 1u;
   else
      index = (index + 1u) % ARRAY_SIZE(frontend_rom_root_presets);
   (void)frontend_rom_root_set_primary(fe, frontend_rom_root_presets[index]);
}

int frontend_rom_root_remove(struct frontend_state *fe,
   const char *path)
{
   int index;

   if (!fe || fe->rom_root_count <= 1u)
      return -1;
   index = frontend_rom_root_index(fe, path);
   if (index < 0)
      return -1;
   for (unsigned i = (unsigned)index + 1u; i < fe->rom_root_count; i++)
      unifrog_text_copy(fe->rom_roots[i - 1u], sizeof(fe->rom_roots[0]),
         fe->rom_roots[i]);
   fe->rom_root_count--;
   fe->rom_roots[fe->rom_root_count][0] = '\0';
   frontend_rom_root_sync_primary(fe);
   return 0;
}

int frontend_path_is_rom_root(const struct frontend_state *fe,
   const char *path)
{
   return frontend_rom_root_index(fe, path) >= 0;
}

int frontend_path_is_inside_rom_root(const struct frontend_state *fe,
   const char *path)
{
   unsigned count = frontend_rom_root_count(fe);

   for (unsigned i = 0; i < count; i++) {
      const char *root = frontend_rom_root_at(fe, i);

      if (frontend_path_has_dir_prefix(path, root))
         return 1;
   }
   return 0;
}

const char *frontend_rom_title(const struct frontend_state *fe,
   const char *path)
{
   if (frontend_rom_root_index(fe, path) == 0)
      return frontend_rom_root_label(fe);
   if (frontend_path_is_rom_root(fe, path))
      return frontend_basename(path);
   return frontend_basename(path);
}
