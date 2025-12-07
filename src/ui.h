#ifndef UI_H
#define UI_H

#include "game.h"

typedef struct {
  char* caption;
  char* id;
} MenuItem;

bool ui_create_layout(struct Game *G);
void ui_click_events(struct Game *G);

#endif // !MAIN_MENU_UI_H
