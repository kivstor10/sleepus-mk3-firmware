#ifndef LUA_SANDBOX_INTERNAL_H
#define LUA_SANDBOX_INTERNAL_H

#include <stdint.h>
#include "lua_sandbox.h"

typedef struct
{
  uint8_t id;
  int16_t value;
} lua_macro_value_t;

typedef struct
{
  uint16_t duration_ms;
  uint8_t value_count;
  lua_macro_value_t values[LUA_SANDBOX_MAX_STEP_VALUES];
} lua_macro_step_t;

typedef struct
{
  uint8_t step_count;
  lua_macro_step_t steps[LUA_SANDBOX_MAX_MACRO_STEPS];
} lua_macro_t;

typedef struct
{
  char name[LUA_SANDBOX_MACRO_NAME_BYTES];
  lua_macro_t macro;
  uint32_t step_started_at;
  uint8_t step;
  uint8_t active;
} lua_named_macro_t;

typedef struct
{
  int16_t values[LUA_CTRL_COUNT];
  uint32_t override_mask;
  uint8_t block_inputs;
} lua_command_buffer_t;

#endif