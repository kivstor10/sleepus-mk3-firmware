#ifndef LUA_SANDBOX_H
#define LUA_SANDBOX_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include "controller_data.h"
#include "lua.h"

#define LUA_SANDBOX_MAX_SCRIPT_BYTES     ((248U * 1024U) - 32U)
#define LUA_SANDBOX_MAX_MACROS           4U
#define LUA_SANDBOX_MAX_NAMED_MACROS     8U
#define LUA_SANDBOX_MACRO_NAME_BYTES     16U
#define LUA_SANDBOX_MAX_MACRO_STEPS      32U
#define LUA_SANDBOX_MAX_STEP_VALUES      12U
#define LUA_SANDBOX_MAX_STORAGE_ENTRIES  32U
#define LUA_SANDBOX_STORAGE_IMAGE_BYTES  4096U

typedef enum
{
  LUA_CTRL_DPAD_UP = 0,
  LUA_CTRL_DPAD_DOWN,
  LUA_CTRL_DPAD_LEFT,
  LUA_CTRL_DPAD_RIGHT,
  LUA_CTRL_A,
  LUA_CTRL_B,
  LUA_CTRL_X,
  LUA_CTRL_Y,
  LUA_CTRL_LB,
  LUA_CTRL_RB,
  LUA_CTRL_VIEW,
  LUA_CTRL_MENU,
  LUA_CTRL_LS,
  LUA_CTRL_RS,
  LUA_CTRL_RESERVED_14,
  LUA_CTRL_RESERVED_15,
  LUA_CTRL_LX,
  LUA_CTRL_LY,
  LUA_CTRL_RX,
  LUA_CTRL_RY,
  LUA_CTRL_LT,
  LUA_CTRL_RT,
  LUA_CTRL_COUNT
} lua_controller_id_t;

typedef enum
{
  LUA_DEVICE_BTN_UP = 0,
  LUA_DEVICE_BTN_DOWN,
  LUA_DEVICE_BTN_SELECT,
  LUA_DEVICE_BTN_BACK,
  LUA_DEVICE_BUTTON_COUNT
} lua_device_button_id_t;

typedef struct
{
  uint32_t (*get_millis)(void);
  uint8_t (*read_device_buttons)(void);
  void (*display_clear)(void);
  void (*display_draw_text)(uint8_t x, uint8_t y, const char *text);
  void (*display_draw_pixel)(uint8_t x, uint8_t y, uint8_t state);
  void (*led_set)(uint8_t state);
  uint8_t (*flash_read)(uint8_t slot, uint32_t offset, void *data,
                        uint32_t length);
  uint8_t (*flash_erase)(uint8_t slot);
  uint8_t (*flash_program)(uint8_t slot, uint32_t offset, const void *data,
                           uint32_t length);
  uint8_t (*flash_commit_allowed)(void);
} lua_sandbox_port_t;

typedef struct
{
  uint32_t instruction_limit;
  uint32_t deadline_ms;
  uint16_t hook_interval;
} lua_sandbox_budget_t;

uint8_t lua_sandbox_init(lua_Alloc allocator, void *allocator_context,
                         const lua_sandbox_port_t *port);
uint8_t lua_sandbox_load(const char *script, size_t script_length,
                         char *error, size_t error_capacity);
void lua_sandbox_begin_frame(const controller_data_t *snapshot,
                             uint32_t current_time_ms);
uint8_t lua_sandbox_run_event(const char *event_name,
                              const lua_sandbox_budget_t *budget,
                              char *error, size_t error_capacity);
void lua_sandbox_apply(controller_data_t *output);
void lua_sandbox_task(uint32_t current_time_ms);
void lua_sandbox_storage_task(void);
void lua_sandbox_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif