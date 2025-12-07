#define CLAY_IMPLEMENTATION
#include "game.h"

int main(void) {
  bool exit_status = EXIT_FAILURE;

  struct Game *G = NULL;

  if (game_new(&G)) {
    if (game_run(G)) {
      exit_status = EXIT_SUCCESS;
    }
  }

  game_free(&G);
  return exit_status;
}
