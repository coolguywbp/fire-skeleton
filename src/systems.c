#include "components.h"
#include "systems.h"
#include <assert.h>

SYSTEM_IMPL(SpriteRenderSystem)
void SpriteRenderSystem_update(Entity e, Component **c, SpriteRenderSystem *system)
{
  (void)e;
  TransformComponent *transform = c[0];
  SpriteComponent *sprite = c[1];
  if (!transform || !sprite || !system->renderer || !system->images) return;

  int id = sprite->gameImageId;
  SDL_Texture *tex = system->images[id];
  if (!tex) return;

  // Preserve the texture's aspect ratio so the sprite isn't squished: keep the
  // height authoritative and derive the width from the texture. The aspect is
  // cached per image so SDL_GetTextureSize isn't called every frame per entity.
  static float aspect_cache[16];
  static bool aspect_init;
  if (!aspect_init) {
    for (int i = 0; i < 16; i++) aspect_cache[i] = -1.0f;
    aspect_init = true;
  }
  if (id >= 0 && id < 16) {
    if (aspect_cache[id] < 0.0f) {
      float tw = 0.0f, th = 0.0f;
      aspect_cache[id] = (SDL_GetTextureSize(tex, &tw, &th) && th > 0.0f) ? tw / th : 1.0f;
    }
    transform->w = transform->h * aspect_cache[id]; // writing back keeps bounce exact
  }

  SDL_FRect dst = { transform->x, transform->y, transform->w, transform->h };
  SDL_RenderTexture(system->renderer, tex, NULL, &dst);
}

bool SpriteRenderSystem_event(Event *event, SpriteRenderSystem *system)
{
	return false;
}



SYSTEM_IMPL(VelocitySystem)
// Mouse repulsion: sprites within this radius of the cursor are pushed away.
#define REPEL_RADIUS 120.0f
#define REPEL_STRENGTH 1.2f   // acceleration near the cursor (per frame)
#define MAX_SPEED 18.0f       // velocity clamp so repulsion can't run away

void VelocitySystem_update(Entity e, Component **c, VelocitySystem *system)
{
  (void)e;
  TransformComponent *transform = c[0];
  VelocityComponent *velocity = c[1];
  if (!transform || !velocity) return;

  const float w = transform->w;
  const float h = transform->h;

  // Repel from a circular area around the mouse cursor.
  if (system->mouse) {
    const float cx = transform->x + w * 0.5f;
    const float cy = transform->y + h * 0.5f;
    const float dx = cx - (float)system->mouse->x;
    const float dy = cy - (float)system->mouse->y;
    const float d2 = dx * dx + dy * dy;
    if (d2 < REPEL_RADIUS * REPEL_RADIUS && d2 > 0.0001f) {
      const float d = sqrtf(d2);
      const float falloff = (REPEL_RADIUS - d) / REPEL_RADIUS; // 1 at center -> 0 at edge
      velocity->vx += (dx / d) * REPEL_STRENGTH * falloff * 10.0f;
      velocity->vy += (dy / d) * REPEL_STRENGTH * falloff * 10.0f;

      // Clamp speed so repeated pushes don't accelerate forever.
      const float s2 = velocity->vx * velocity->vx + velocity->vy * velocity->vy;
      if (s2 > MAX_SPEED * MAX_SPEED) {
        const float s = MAX_SPEED / sqrtf(s2);
        velocity->vx *= s;
        velocity->vy *= s;
      }
    }
  }

  transform->x += velocity->vx;
  transform->y += velocity->vy;

  // Bounce off the window edges exactly on contact. The sprite is drawn from
  // its top-left corner, so the right/bottom edges are at x+w / y+h. Clamp the
  // edge to the boundary instead of letting it overshoot before reversing.
  if (transform->x < 0.0f) {
    transform->x = 0.0f;
    velocity->vx = -velocity->vx;
  } else if (transform->x + w > WINDOW_WIDTH) {
    transform->x = WINDOW_WIDTH - w;
    velocity->vx = -velocity->vx;
  }

  if (transform->y < 0.0f) {
    transform->y = 0.0f;
    velocity->vy = -velocity->vy;
  } else if (transform->y + h > WINDOW_HEIGHT) {
    transform->y = WINDOW_HEIGHT - h;
    velocity->vy = -velocity->vy;
  }
}

bool VelocitySystem_event(Event *event, VelocitySystem *system)
{
	return false;
}

static const char *SpriteRenderSystem_afterSystems[] = {"VelocitySystem", NULL};
// IsThreadSafe = false: this system calls into the SDL renderer, which must
// only be used on the main thread. Keeping it main-thread-only avoids issuing
// render calls from a background ECS worker.
const SystemUpdateInfo SpriteRenderSystem_update_info = {
	false, false, false, SpriteRenderSystem_afterSystems};

static const char *VelocitySystem_afterSystems[] = {NULL};
// IsThreadSafe=true, UpdatesOtherEntities=false: each update only touches its
// own entity's components, so the ECS can run it across worker threads.
const SystemUpdateInfo VelocitySystem_update_info = {
	true, false, false, VelocitySystem_afterSystems};

bool ecs_systems_register(struct Game *G){
  //////////////////////////////////
  //
  //    VELOCITY SYSTEM
  //
  const char *VelocitySystem_components[] = {
	"TransformComponent", "VelocityComponent", NULL};
  VelocitySystem_reg.archetype = ECS_EntityRegisterArchetype(G->ecs, "VelocitySystemArchetype", VelocitySystem_components);
  VelocitySystem *velocity_sys = malloc(sizeof(VelocitySystem));
  velocity_sys->mouse = G->mouse;
	assert(REGISTER_SYSTEM(G->ecs, VelocitySystem, velocity_sys));
  LOG_DEBUG("ECS VelocitySystem OK");
  //////////////////////////////////
  //
  //    SPRITE RENDER SYSTEM
  //
  const char *SpriteRenderSystem_components[] = {
	 "TransformComponent", "SpriteComponent", NULL};
	SpriteRenderSystem_reg.archetype = ECS_EntityRegisterArchetype(G->ecs, "SpriteRenderSystemArchetype", SpriteRenderSystem_components);
	SpriteRenderSystem *spriteRender_sys = malloc(sizeof(SpriteRenderSystem));
	spriteRender_sys->renderer = G->renderer;
	spriteRender_sys->images = G->images;
	assert(REGISTER_SYSTEM(G->ecs, SpriteRenderSystem, spriteRender_sys));
  LOG_DEBUG("ECS SpriteRenderSystem OK");
  //////////////////////////////////

  return true;
}

bool ecs_systems_free(struct Game *G){
  return true;
}


