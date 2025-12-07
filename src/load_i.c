#include "load_i.h"

SDL_Texture **load_images(SDL_Renderer *renderer) {
  char *imageDirPath = "assets/images/";

  int length = 2;

  char *image_names[] = {
  "MenuFireSkeleton.png", "SpriteSheet1.png"
  };
 
  SDL_Texture **textures = malloc(length * sizeof(SDL_Texture*));
  for (int i = 0; i < length; i++) {
    char full_path[256];
    snprintf(full_path, sizeof(full_path), "%s%s", imageDirPath, image_names[i]);
    SDL_Texture *image =
        IMG_LoadTexture(renderer, full_path);
    if (image) {
      // Do something with the loaded texture, e.g., store it in a list
      printf("Loaded image: %s (%d)\n", image_names[i], i+1);

      textures[i] = image;
      // Remember to free the texture later with
      // SDL_DestroyTexture(texture);
    } else {
      printf("Failed to load image %s\n", image_names[i]); //IMG_GetError());
    }
  }

  return textures;
}
