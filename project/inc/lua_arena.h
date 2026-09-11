#ifndef LUA_ARENA_H
#define LUA_ARENA_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

typedef struct
{
  uint8_t *memory;
  size_t size;
} lua_arena_t;

uint8_t lua_arena_init(lua_arena_t *arena, void *memory, size_t size);
void *lua_arena_alloc(void *context, void *pointer, size_t old_size,
                      size_t new_size);

#ifdef __cplusplus
}
#endif

#endif