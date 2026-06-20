#include "load_i.h"
#include "logger.h"

#include <stdlib.h>
#include <string.h>

#define IMAGES_DIR "assets/images/"
#define MAX_IMAGES 64

// Collected during the directory scan: every *.png file name found.
typedef struct { char *files[MAX_IMAGES]; int count; } PngList;

static SDL_EnumerationResult collect_png(void *ud, const char *dir, const char *fname) {
  (void)dir;
  PngList *pl = ud;
  size_t n = SDL_strlen(fname);
  if (n > 4 && SDL_strcasecmp(fname + n - 4, ".png") == 0 && pl->count < MAX_IMAGES)
    pl->files[pl->count++] = SDL_strdup(fname);
  return SDL_ENUM_CONTINUE;
}

static int cmp_name(const void *a, const void *b) {
  return strcmp(*(char *const *)a, *(char *const *)b);
}

SDL_Texture **load_images(SDL_Renderer *renderer, char ***out_names, int *out_count) {
  // Scan assets/images/ for PNGs and load them all. Dropping a new foo.png into
  // that folder makes it usable from a script as "foo" -- no code change.
  PngList pl = { .count = 0 };
  SDL_EnumerateDirectory(IMAGES_DIR, collect_png, &pl);
  // Stable, alphabetical order so ids are deterministic across runs/platforms.
  qsort(pl.files, pl.count, sizeof(char *), cmp_name);

  SDL_Texture **textures = malloc((size_t)(pl.count > 0 ? pl.count : 1) * sizeof(SDL_Texture *));
  char        **names    = malloc((size_t)(pl.count > 0 ? pl.count : 1) * sizeof(char *));
  int loaded = 0;
  for (int i = 0; i < pl.count; i++) {
    char full[512];
    snprintf(full, sizeof full, "%s%s", IMAGES_DIR, pl.files[i]);
    SDL_Texture *tex = IMG_LoadTexture(renderer, full);
    if (tex) {
      size_t stem = SDL_strlen(pl.files[i]) - 4;   // drop ".png"
      char *name = malloc(stem + 1);
      memcpy(name, pl.files[i], stem);
      name[stem] = '\0';
      textures[loaded] = tex;
      names[loaded]    = name;
      LOG_DEBUG("Loaded image '%s' (id %d)", name, loaded);
      loaded++;
    } else {
      LOG_ERROR("Failed to load image %s", full);
    }
    SDL_free(pl.files[i]);
  }

  if (loaded == 0) LOG_ERROR("No images loaded from %s", IMAGES_DIR);
  *out_names = names;
  *out_count = loaded;
  return textures;
}

int image_id_by_name(struct Game *G, const char *name) {
  if (!G || !name) return -1;
  for (int i = 0; i < G->image_count; i++)
    if (strcmp(G->image_names[i], name) == 0) return i;
  return -1;
}
