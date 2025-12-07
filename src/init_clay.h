#ifndef INIT_CLAY_H
#define INIT_CLAY_H

#include "game.h"

struct UI {
  Clay_RenderCommandArray renderCommands;
  Clay_Dimensions clayDimensions;
};

void HandleClayErrors(Clay_ErrorData errorData);
bool ui_init_clay(struct Game *G);
bool ui_update(struct Game *G);
bool ui_create_layout(struct Game *G);

#endif
