#include "game.h"

TTF_Font **load_fonts(struct Game *G);

// Font file paths indexed by fontId; used by the renderer's per-size font cache.
const char **load_font_paths(void);
