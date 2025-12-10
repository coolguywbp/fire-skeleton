#include "utils.h"

float random_float(float min, float max){
   return ((max - min) * ((float)rand() / RAND_MAX)) + min;
}
