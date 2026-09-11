#include "lua_arena.h"

#include <string.h>

#define ARENA_ALIGNMENT  8U
#define BLOCK_FREE       1U

typedef struct
{
  size_t size;
  uint32_t flags;
  uint32_t reserved;
} arena_block_t;

static size_t align_size(size_t size)
{
  return (size + ARENA_ALIGNMENT - 1U) & ~(ARENA_ALIGNMENT - 1U);
}

static arena_block_t *first_block(lua_arena_t *arena)
{
  return (arena_block_t *)arena->memory;
}

static arena_block_t *next_block(lua_arena_t *arena, arena_block_t *block)
{
  uint8_t *next = (uint8_t *)(block + 1) + block->size;
  return next + sizeof(arena_block_t) <= arena->memory + arena->size ?
         (arena_block_t *)next : 0;
}

static uint8_t block_valid(lua_arena_t *arena, arena_block_t *target)
{
  arena_block_t *block = first_block(arena);
  while(block != 0)
  {
    if(block == target)
    {
      return 1;
    }
    block = next_block(arena, block);
  }
  return 0;
}

static void split_block(lua_arena_t *arena, arena_block_t *block, size_t size)
{
  arena_block_t *remainder;
  size_t remaining;
  (void)arena;
  if(block->size < size + sizeof(arena_block_t) + ARENA_ALIGNMENT)
  {
    return;
  }
  remaining = block->size - size - sizeof(arena_block_t);
  remainder = (arena_block_t *)((uint8_t *)(block + 1) + size);
  remainder->size = remaining;
  remainder->flags = BLOCK_FREE;
  remainder->reserved = 0;
  block->size = size;
}

static void merge_forward(lua_arena_t *arena, arena_block_t *block)
{
  arena_block_t *next = next_block(arena, block);
  while(next != 0 && (next->flags & BLOCK_FREE) != 0)
  {
    block->size += sizeof(arena_block_t) + next->size;
    next = next_block(arena, block);
  }
}

static void free_block(lua_arena_t *arena, arena_block_t *block)
{
  arena_block_t *current;
  arena_block_t *previous = 0;
  block->flags = BLOCK_FREE;
  merge_forward(arena, block);
  current = first_block(arena);
  while(current != 0 && current != block)
  {
    previous = current;
    current = next_block(arena, current);
  }
  if(previous != 0 && (previous->flags & BLOCK_FREE) != 0)
  {
    merge_forward(arena, previous);
  }
}

static void *allocate_block(lua_arena_t *arena, size_t size)
{
  arena_block_t *block = first_block(arena);
  size = align_size(size);
  while(block != 0)
  {
    if((block->flags & BLOCK_FREE) != 0 && block->size >= size)
    {
      split_block(arena, block, size);
      block->flags = 0;
      return block + 1;
    }
    block = next_block(arena, block);
  }
  return 0;
}

uint8_t lua_arena_init(lua_arena_t *arena, void *memory, size_t size)
{
  arena_block_t *block;
  uintptr_t address;
  size_t adjustment;
  if(arena == 0 || memory == 0)
  {
    return 0;
  }
  address = (uintptr_t)memory;
  adjustment = (ARENA_ALIGNMENT - (address & (ARENA_ALIGNMENT - 1U))) &
               (ARENA_ALIGNMENT - 1U);
  if(size <= adjustment + sizeof(arena_block_t) + ARENA_ALIGNMENT)
  {
    return 0;
  }
  arena->memory = (uint8_t *)memory + adjustment;
  arena->size = (size - adjustment) & ~(ARENA_ALIGNMENT - 1U);
  block = first_block(arena);
  block->size = arena->size - sizeof(*block);
  block->flags = BLOCK_FREE;
  block->reserved = 0;
  return 1;
}

void *lua_arena_alloc(void *context, void *pointer, size_t old_size,
                      size_t new_size)
{
  lua_arena_t *arena = (lua_arena_t *)context;
  arena_block_t *block;
  arena_block_t *next;
  void *replacement;
  size_t copy_size;
  (void)old_size;
  if(arena == 0 || arena->memory == 0)
  {
    return 0;
  }
  if(pointer == 0)
  {
    return new_size == 0 ? 0 : allocate_block(arena, new_size);
  }
  block = (arena_block_t *)pointer - 1;
  if(!block_valid(arena, block) || (block->flags & BLOCK_FREE) != 0)
  {
    return 0;
  }
  if(new_size == 0)
  {
    free_block(arena, block);
    return 0;
  }
  new_size = align_size(new_size);
  if(block->size >= new_size)
  {
    split_block(arena, block, new_size);
    return pointer;
  }
  next = next_block(arena, block);
  if(next != 0 && (next->flags & BLOCK_FREE) != 0 &&
     block->size + sizeof(*next) + next->size >= new_size)
  {
    block->size += sizeof(*next) + next->size;
    split_block(arena, block, new_size);
    return pointer;
  }
  replacement = allocate_block(arena, new_size);
  if(replacement == 0)
  {
    return 0;
  }
  copy_size = block->size < new_size ? block->size : new_size;
  memcpy(replacement, pointer, copy_size);
  free_block(arena, block);
  return replacement;
}