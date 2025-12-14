#define _POSIX_C_SOURCE 200112L
#define _GNU_SOURCE  
#define CLAY_IMPLEMENTATION

#include "game.h"

int main(void) {
  // On Linux force X11 for more predictable behavior
  #ifdef __linux__
  setenv("SDL_VIDEODRIVER", "x11", 1);
  // Disable any desktop environment scaling
  setenv("GDK_SCALE", "1", 1);
  setenv("QT_AUTO_SCREEN_SCALE_FACTOR", "0", 1);
  #endif


  printf(".                                                      .\n");
  printf("        .n                   .                 .                  n.\n");
  printf("  .   .dP                  dP                   9b                 9b.    .\n");
  printf(" 4    qXb         .       dX                     Xb       .        dXp     t\n");
  printf("dX.    9Xb      .dXb    __                         __    dXb.     dXP     .Xb\n");
  printf("9XXb._       _.dXXXXb dXXXXbo.                 .odXXXXb dXXXXb._       _.dXXP\n");
  printf(" 9XXXXXXXXXXXXXXXXXXXVXXXXXXXXOo.           .oOXXXXXXXXVXXXXXXXXXXXXXXXXXXXP\n");
  printf("  `9XXXXXXXXXXXXXXXXXXXXX'~   ~`OOO8b   d8OOO'~   ~`XXXXXXXXXXXXXXXXXXXXXP'\n");
  printf("    `9XXXXXXXXXXXP' `9XX'   DIE    `98v8P'  HUMAN   `XXP' `9XXXXXXXXXXXP'\n");
  printf("        ~~~~~~~       9X.          .db|db.          .XP       ~~~~~~~\n");
  printf("                        )b.  .dbo.dP'`v'`9b.odb.  .dX(\n");
  printf("                      ,dXXXXXXXXXXXb     dXXXXXXXXXXXb.\n");
  printf("                     dXXXXXXXXXXXP'   .   `9XXXXXXXXXXXb\n");
  printf("                    dXXXXXXXXXXXXb   d|b   dXXXXXXXXXXXXb\n");
  printf("                    9XXb'   `XXXXXb.dX|Xb.dXXXXX'   `dXXP\n");
  printf("                     `'      9XXXXXX(   )XXXXXXP      `'\n");
  printf("                              XXXX X.`v'.X XXXX\n");
  printf("                              XP^X'`b   d'`X^XX\n");
  printf("                              X. 9  `   '  P )X\n");
  printf("                              `b  `       '  d'\n");
  printf("                               `             '\n");



  logger_initConsoleLogger(stderr);
  logger_setLevel(LogLevel_DEBUG);
  LOG_INFO("FIRE SKELETON INVADER");
  
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
