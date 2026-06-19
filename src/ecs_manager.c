#include "ecs_manager.h"
#include "ecs_hash.h"
#include "ecs_hashset.h"
#include "logger.h"

int string_arr_to_type(hash_t **dst, const char **src)
{
    if (!dst || !src) return -1;

    size_t idx = 0;
    while(src[idx]) idx++;

    hash_t *ptr = calloc(idx, sizeof(hash_t));
    if (!ptr) return -1;

    *dst = ptr;
    for (size_t _i = 0; _i < idx; _i++) {
        ptr[_i] = hash_string(src[_i]);
    }

    return idx;
}

// Get a component type's info.
ComponentType* Manager_GetComponentType(ECS *ecs, hash_t type)
{
	assert(ecs);

	return ht_get(ecs->cm_types, type);
}

// Registers a new component type.
bool Manager_RegisterComponentType(ECS *ecs, ComponentType *type)
{
	assert(ecs && ecs->cm_types && type);

	type = ht_insert(ecs->cm_types, type->type_hash, type);
	if (type == NULL) return false;

	type->components = cpool_alloc(ecs->alloc_info.components, type->type_size);
	if (!type->components) {
		free((char *)type->type);
		ht_delete(ecs->cm_types, type->type_hash);
		return false;
	}

  LOG_DEBUG("Registered component: %s", type->type);

	return true;
}

// Return whether a component type has been registered.
bool Manager_HasComponentType(ECS *ecs, hash_t type)
{
	assert(ecs && ecs->cm_types);

	return ht_get(ecs->cm_types, type) ? true : false;
}

Component* Manager_CreateComponent(ECS *ecs, ComponentType *type, hash_t id)
{
	assert(ecs && type);

	// Create the component
	Component *comp = cpool_insert(type->components, id, NULL);
	if (!comp) return NULL;

	// And run the creation function.
	if (type->cr_func) type->cr_func(comp);

	return comp;
}

Component* Manager_GetComponent(ECS *ecs, ComponentType *type, hash_t id)
{
	assert(ecs);

	return cpool_get(type->components, id);
}

Component* Manager_GetComponentByID(ECS *ecs, ComponentID id)
{
	assert(ecs);

	ComponentType *cm_type = ht_get(ecs->cm_types, id.type);
	if (!cm_type) return NULL;

	return cpool_get(cm_type->components, id.id);
}

void Manager_DeleteComponent(ECS *ecs, ComponentType *type, hash_t id)
{
	assert(ecs && type && type->components);

	Component *comp = cpool_get(type->components, id);
	if (!comp) return;

	// Call the dtor.
	if (type->dl_func) type->dl_func(comp);

	// Delete the component and it's data.
	cpool_delete(type->components, id);
}

/* -------------------------------------------------------------------------- */

// An entity is literally a 32-bit integer. We drop it in the entity list for
// reasons that need to be re-evaluated.
//
// It might be possible to replace the entity list with a simple gap-spanning
// tracker for free IDs. This would make entity deletion slightly more complex,
// but would likely significantly reduce the amount of complexity and memory
// required to create and manage entities.
//
// This function should not be called from a thread that does not have a lock
// on the entity list.
//
// Due to the time cost of acquiring a lock, locking should be performed in a
// higher-level logic structure, preferably one that handles creating multiple
// entities at once.
Entity Manager_CreateEntity(ECS *ecs)
{
	assert(ecs);

    hash_t entity;
    (void) ha_insert_free(ecs->entities, &entity, NULL);

	return entity;
}

// This function should not be called on a thread that does not have a global lock
// on the ECS.
void Manager_DeleteEntity(ECS *ecs, Entity entity)
{
	assert(ecs);

	// Because we don't keep state on the entity, we iterate through all
	// possible components and delete the ones matching the entity.
	HT_FOR(ecs->cm_types) {
		ComponentType *cm_type = ht_get(ecs->cm_types, idx);
		if (cpool_get(cm_type->components, entity))
			Manager_DeleteComponent(ecs, cm_type, entity);
	}

	// Remove the entity from system queues.
	HT_FOR(ecs->systems) {
		System *system = ht_get(ecs->systems, idx);
		ha_delete(system->ent_queue, entity);
	}

    ha_delete(ecs->entities, entity);

	// Queues shrank, so the cached update ranges are stale.
	ecs->update_systems_dirty = true;
}

/* -------------------------------------------------------------------------- */

bool Manager_RegisterSystem(ECS *ecs, System *info)
{
	assert(ecs && ecs->systems && info);

	info->ent_queue = ha_alloc(128, sizeof(hash_t));
    ERR_RET_ZERO(info->ent_queue, "Error creating system entity queue.\n");

	// Resolve the archetype's component types once, so the per-entity update
	// loop only needs a single hashtable lookup per component (not two).
	info->comp_types = NULL;
	if (info->archetype && info->archetype->size > 0) {
		info->comp_types = malloc(sizeof(ComponentType *) * info->archetype->size);
		ERR_RET_ZERO(info->comp_types, "Error allocating system component types.\n");
		for (uint32_t i = 0; i < info->archetype->size; i++)
			info->comp_types[i] = ht_get(ecs->cm_types, info->archetype->components[i]);
	}

	System *_info = ht_insert(ecs->systems, hash_string(info->name), info);
	if (!_info) return false;

	LOG_DEBUG("Registering system: %s", info->name);

	// Resolve dependency ordering against already-registered systems.
	// Circular references in the dependencies cause undefined behavior.
	// TODO: this is a simplistic implementation that does not handle
	// dependencies very well. Improve this.
	size_t first_insert = 0;
	size_t last_insert = ecs->system_order.size;

	for (size_t idx = 0; idx < ecs->system_order.size; idx++) {
		System **system_ptr = (System **)dyn_get(&ecs->system_order, idx);
		System *system = system_ptr ? *system_ptr : NULL;
		if (!system) continue;

		// If an existing system depends on the new one, the new one must be
		// ordered before it.
		if (system->dependencies && hs_get(system->dependencies, info->name_hash))
			if (idx < last_insert) last_insert = idx;
		// If the new system depends on an existing one, it must be ordered
		// after it.
		if (info->dependencies && hs_get(info->dependencies, system->name_hash))
			first_insert = idx;
	}

	size_t insert = first_insert > last_insert ? first_insert : last_insert;

	// system_order must reference the canonical, hashtable-owned copy (_info),
	// whose address is stable (mempool-backed), NOT the temporary `info` shell.
	// dyn_append handles the empty-array case (dyn_insert misbehaves at size 0).
	if (ecs->system_order.size == 0)
		dyn_append(&ecs->system_order, &_info);
	else
		dyn_insert(&ecs->system_order, insert, &_info);

	ecs->update_systems_dirty = true;

	// The hashtable copied the struct on insert and now owns the canonical
	// System. _info shares info's member pointers, so free only the temporary
	// shell here; ht_delete will release the table copy on unregister.
	free(info);

	return true;
}

System* Manager_GetSystem(ECS *ecs, const char *name)
{
	assert(ecs && ecs->systems && name);

	return ht_get(ecs->systems, hash_string(name));
}

void Manager_UnregisterSystem(ECS *ecs, System *system)
{
	assert(ecs && ecs->systems && system);

    int idx = dyn_find(&ecs->system_order, &system);
    dyn_remove(&ecs->system_order, idx, false);
    ecs->update_systems_dirty = true;

	EventQueue_Free(system->ev_queue);
	free((char *)system->name);
	ha_free(system->ent_queue);
    if (system->dependencies) hs_free(system->dependencies);

	// The system owns its archetype, user data and resolved type table.
	ECS_EntityFreeArchetype(system->archetype);
	free(system->udata);
	free(system->comp_types);

	// ht_delete frees the table-owned System chunk. `system` points into the
	// hashtable's mempool storage, so it must NOT be free()'d directly.
	ht_delete(ecs->systems, system->name_hash);
}

void Manager_ArrangeSystems(ECS *ecs)
{

}

void Manager_UpdateCollections(ECS *ecs, Entity entity)
{
	HT_FOR(ecs->systems) {
		System *system = ht_get(ecs->systems, idx);
		if (Manager_ShouldSystemQueueEntity(ecs, system, entity)) {
			ha_insert_free(system->ent_queue, NULL, &entity);
		}
		else {
			ha_delete(system->ent_queue, entity);
		}
	}

	// System entity queues changed, so the cached update ranges are stale.
	ecs->update_systems_dirty = true;
}

bool Manager_ShouldSystemQueueEntity(ECS *ecs, System *system, Entity entity)
{
	assert(ecs && system);

	// If we don't want at least one component, we only update the system once.
	if (system->archetype->size < 1) return false;

	bool should_queue = true;
	for (size_t idx = 0; idx < system->archetype->size; idx++) {
		ComponentID id = {entity, system->archetype->components[idx]};
		if (!Manager_GetComponentByID(ecs, id)) {
			should_queue = false;
			break;
		}
	}

	return should_queue;
}

void Manager_UpdateSystem(ECS *ecs, System *system, Entity entity)
{
	assert(ecs && system && system->archetype->size > 0);

	const size_t size = system->archetype->size;
	Component *components[size];

	// Component types are pre-resolved (system->comp_types), so this is a single
	// hashtable lookup per component instead of two (type + component).
	for (size_t idx = 0; idx < size; idx++) {
		ComponentType *ct = system->comp_types[idx];
		components[idx] = ct ? cpool_get(ct->components, entity) : NULL;
	}

	system->up_func(entity, components, system->udata);
}

void Manager_SystemEvent(ECS *ecs, System *system, Event *ev)
{
	assert(ecs && system && ev);
	if (!system->ev_func) return;

	system->ev_func(ev, system->udata);
}
