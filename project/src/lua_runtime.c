#include "lua_runtime.h"

#include <string.h>
#include "diagnostic_log.h"
#include "lua_archive.h"
#include "lua_arena.h"
#include "lua_sandbox.h"
#include "lua_sandbox_at32.h"

#define LUA_RUNTIME_ARENA_BYTES  (128U * 1024U)
#define LUA_RUNTIME_FRAME_MS     10U
#define LUA_RUNTIME_MAX_FAILURES 3U

static uint64_t lua_memory[LUA_RUNTIME_ARENA_BYTES / sizeof(uint64_t)];
static lua_arena_t lua_arena;
static uint8_t lua_active;
static uint8_t lua_frame_started;
static uint8_t lua_failure_count;
static uint32_t lua_last_frame_ms;
static controller_data_t lua_last_output;
static uint8_t lua_last_output_valid;

static const lua_sandbox_budget_t input_budget =
{
  4000U,
  2U,
  100U
};

uint8_t lua_runtime_init(void)
{
  const char *script;
  size_t script_length;
  char error[96];

  lua_active = 0;
  lua_frame_started = 0;
  lua_failure_count = 0;
  lua_last_frame_ms = 0;
  memset(&lua_last_output, 0, sizeof(lua_last_output));
  lua_last_output_valid = 0;
  if(!lua_archive_open(&script, &script_length))
  {
    diagnostic_log_event("LUA_ARCHIVE", 0, 0, 0);
    return 0;
  }
  if(!lua_arena_init(&lua_arena, lua_memory, sizeof(lua_memory)))
  {
    diagnostic_log_event("LUA_ARENA", 0, 0, 0);
    return 0;
  }
  if(!lua_sandbox_init(lua_arena_alloc, &lua_arena,
                       lua_sandbox_at32_port()))
  {
    diagnostic_log_event("LUA_INIT", 0, 0, 0);
    return 0;
  }
  if(!lua_sandbox_load(script, script_length, error, sizeof(error)))
  {
    diagnostic_log_event("LUA_LOAD", (uint32_t)script_length, 0, 0);
    lua_sandbox_shutdown();
    return 0;
  }
  lua_active = 1;
  diagnostic_log_event("LUA_READY", (uint32_t)script_length, 0, 0);
  return 1;
}

uint8_t lua_runtime_active(void)
{
  return lua_active;
}

void lua_runtime_console_connected(void)
{
  char error[96];

  if(lua_active &&
     !lua_sandbox_run_event("on_console_connected", &input_budget,
                            error, sizeof(error)))
  {
    diagnostic_log_event("LUA_CONNECT", 0, 0, 0);
  }
}

static uint8_t lua_runtime_run_frame(const controller_data_t *input,
                                     uint32_t current_time_ms)
{
  char error[96];
  if(!lua_active || input == 0)
  {
    return 0;
  }
  lua_sandbox_begin_frame(input, current_time_ms);
  lua_sandbox_task(current_time_ms);
  lua_last_frame_ms = current_time_ms;
  lua_frame_started = 1;
  if(!lua_sandbox_run_event("on_input", &input_budget,
                            error, sizeof(error)))
  {
    lua_failure_count++;
    diagnostic_log_text("LUA_ERROR", error);
    diagnostic_log_event("LUA_EVENT", current_time_ms,
                         lua_failure_count, 0);
    if(lua_failure_count >= LUA_RUNTIME_MAX_FAILURES)
    {
      lua_active = 0;
    }
    return 0;
  }
  lua_failure_count = 0;
  return 1;
}

void lua_runtime_task(const controller_data_t *input,
                      uint32_t current_time_ms)
{
  lua_sandbox_storage_task();
  if(!lua_active || input == 0 ||
     (lua_frame_started &&
      current_time_ms - lua_last_frame_ms < LUA_RUNTIME_FRAME_MS))
  {
    return;
  }
  lua_runtime_run_frame(input, current_time_ms);
}

void lua_runtime_process_input(const controller_data_t *input,
                               controller_data_t *output,
                               uint32_t current_time_ms)
{
  if(!lua_active || input == 0 || output == 0)
  {
    return;
  }
  if(lua_runtime_run_frame(input, current_time_ms))
  {
    lua_sandbox_apply(output);
    lua_last_output = *output;
    lua_last_output_valid = 1;
  }
}

uint8_t lua_runtime_apply_pending(controller_data_t *output,
                                  uint32_t current_time_ms)
{
  uint8_t changed;
  if(!lua_active || output == 0)
  {
    return 0;
  }
  lua_sandbox_task(current_time_ms);
  lua_sandbox_apply(output);
  changed = !lua_last_output_valid ||
    memcmp(&lua_last_output, output, sizeof(lua_last_output)) != 0;
  lua_last_output = *output;
  lua_last_output_valid = 1;
  return changed;
}