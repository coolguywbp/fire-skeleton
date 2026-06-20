#include "load_i.h"
#include "logger.h"

SDL_Texture **load_images(SDL_Renderer *renderer) {
  char *imageDirPath = "assets/images/";

  int length = IMAGE_COUNT;

  // Index order IS the image id used by scripts (see image_id_from_name):
  //   0 skeleton/menu, 1 sheet/sprite, 2 invaders, 3 slots, 4 benchmark, 5 cube.
  // 2..5 are demo thumbnails for the demo-picker tiles.
  char *image_names[] = {
  "MenuFireSkeleton.png", "SpriteSheet1.png",
  "shot_invaders.png", "shot_slots.png", "shot_benchmark.png", "shot_cube.png"
  };
 
  SDL_Texture **textures = malloc(length * sizeof(SDL_Texture*));
  for (int i = 0; i < length; i++) {
    char full_path[256];
    snprintf(full_path, sizeof(full_path), "%s%s", imageDirPath, image_names[i]);
    SDL_Texture *image =
        IMG_LoadTexture(renderer, full_path);
    if (image) {
      // Do something with the loaded texture, e.g., store it in a list
      LOG_DEBUG("Loaded image: %s (%d/%d)", image_names[i], i+1, length);

      textures[i] = image;
      // Remember to free the texture later with
      // SDL_DestroyTexture(texture);
    } else {
      printf("Failed to load image %s\n", image_names[i]); //IMG_GetError());
    }
  }

  return textures;
}
