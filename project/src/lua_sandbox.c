#include "lua_sandbox.h"
#include "lua_sandbox_internal.h"
#include "usb_app.h"

#include <limits.h>
#include <string.h>
#include "lauxlib.h"
#include "lualib.h"

#define BUTTON_DPAD_UP      0x0100U
#define BUTTON_DPAD_DOWN    0x0200U
#define BUTTON_DPAD_LEFT    0x0400U
#define BUTTON_DPAD_RIGHT   0x0800U
#define BUTTON_A            0x0010U
#define BUTTON_B            0x0020U
#define BUTTON_X            0x0040U
#define BUTTON_Y            0x0080U
#define BUTTON_LB           0x1000U
#define BUTTON_RB           0x2000U
#define BUTTON_VIEW         0x0008U
#define BUTTON_MENU         0x0004U
#define BUTTON_LS           0x4000U
#define BUTTON_RS           0x8000U
#define TRIGGER_RAW_MAX     1023U
#define LUA_VALUE_MIN       (-100)
#define LUA_VALUE_MAX       100
#define MACRO_DURATION_MAX  60000
#define DISPLAY_TEXT_MAX    64U
#define LOAD_INSTRUCTION_LIMIT  50000U
#define LOAD_DEADLINE_MS         20U
#define LOAD_HOOK_INTERVAL       100U

typedef char lua_integer_must_be_32_bits[sizeof(lua_Integer) == 4 ? 1 : -1];
typedef char lua_number_must_be_32_bits[sizeof(lua_Number) == 4 ? 1 : -1];

typedef struct
{
  lua_State *state;
  lua_sandbox_port_t port;
  controller_data_t snapshot;
  uint8_t device_buttons;
  lua_command_buffer_t script_commands;
  lua_command_buffer_t macro_commands;
  int16_t previous_values[LUA_CTRL_COUNT];
  uint32_t changed_at[LUA_CTRL_COUNT];
  lua_named_macro_t named_macros[LUA_SANDBOX_MAX_NAMED_MACROS];
  lua_macro_t macro_queue[LUA_SANDBOX_MAX_MACROS];
  lua_macro_t active_macro;
  uint8_t macro_head;
  uint8_t macro_tail;
  uint8_t macro_count;
  uint8_t macro_active;
  uint8_t macro_step;
  uint32_t macro_step_started_at;
  uint32_t frame_time_ms;
  uint32_t hook_calls_remaining;
  uint32_t hook_deadline;
  uint16_t hook_interval;
  uint8_t initialized;
  uint8_t ptime_initialized;
} lua_sandbox_context_t;

static lua_sandbox_context_t sandbox;

static const uint16_t button_masks[16] =
{
  BUTTON_DPAD_UP, BUTTON_DPAD_DOWN, BUTTON_DPAD_LEFT, BUTTON_DPAD_RIGHT,
  BUTTON_A, BUTTON_B, BUTTON_X, BUTTON_Y, BUTTON_LB, BUTTON_RB,
  BUTTON_VIEW, BUTTON_MENU, BUTTON_LS, BUTTON_RS, 0, 0
};

static lua_sandbox_context_t *binding_context(lua_State *state)
{
  return (lua_sandbox_context_t *)lua_touserdata(state, lua_upvalueindex(1));
}

static int16_t clamp_value(lua_Integer value)
{
  if(value < LUA_VALUE_MIN)
  {
    return LUA_VALUE_MIN;
  }
  if(value > LUA_VALUE_MAX)
  {
    return LUA_VALUE_MAX;
  }
  return (int16_t)value;
}

static uint8_t valid_id(lua_Integer id)
{
  return id >= 0 && id < LUA_CTRL_COUNT &&
         (id >= 16 || button_masks[id] != 0);
}

static int16_t stick_to_percent(int16_t value)
{
  if(value == INT16_MIN)
  {
    return -100;
  }
  return (int16_t)(((int32_t)value * 100) / INT16_MAX);
}

static int16_t snapshot_value(const controller_data_t *data, uint8_t id)
{
  if(id < 16)
  {
    return (data->buttons & button_masks[id]) != 0 ? 100 : 0;
  }
  switch(id)
  {
    case LUA_CTRL_LX: return stick_to_percent(data->lx);
    case LUA_CTRL_LY: return (int16_t)-stick_to_percent(data->ly);
    case LUA_CTRL_RX: return stick_to_percent(data->rx);
    case LUA_CTRL_RY: return (int16_t)-stick_to_percent(data->ry);
    case LUA_CTRL_LT:
      return (int16_t)(((uint32_t)data->lt * 100U) / TRIGGER_RAW_MAX);
    case LUA_CTRL_RT:
      return (int16_t)(((uint32_t)data->rt * 100U) / TRIGGER_RAW_MAX);
    default: return 0;
  }
}

static void apply_value(controller_data_t *data, uint8_t id, int16_t value)
{
  if(id < 16)
  {
    if(value != 0)
    {
      data->buttons |= button_masks[id];
    }
    else
    {
      data->buttons &= (uint16_t)~button_masks[id];
    }
    return;
  }
  switch(id)
  {
    case LUA_CTRL_LX: data->lx = (int16_t)(((int32_t)value * INT16_MAX) / 100); break;
    case LUA_CTRL_LY: data->ly = (int16_t)-(((int32_t)value * INT16_MAX) / 100); break;
    case LUA_CTRL_RX: data->rx = (int16_t)(((int32_t)value * INT16_MAX) / 100); break;
    case LUA_CTRL_RY: data->ry = (int16_t)-(((int32_t)value * INT16_MAX) / 100); break;
    case LUA_CTRL_LT: data->lt = value > 0 ? (uint16_t)((value * TRIGGER_RAW_MAX) / 100) : 0; break;
    case LUA_CTRL_RT: data->rt = value > 0 ? (uint16_t)((value * TRIGGER_RAW_MAX) / 100) : 0; break;
    default: break;
  }
}

static void apply_commands(controller_data_t *output,
                           const lua_command_buffer_t *commands)
{
  uint8_t id;
  if(commands->block_inputs)
  {
    memset(output, 0, sizeof(*output));
  }
  for(id = 0; id < LUA_CTRL_COUNT; id++)
  {
    if((commands->override_mask & (1UL << id)) != 0)
    {
      apply_value(output, id, commands->values[id]);
    }
  }
}

static int binding_get_val(lua_State *state)
{
  lua_sandbox_context_t *context = binding_context(state);
  lua_Integer id = luaL_checkinteger(state, 1);
  luaL_argcheck(state, valid_id(id), 1, "unsupported controller id");
  lua_pushinteger(state, snapshot_value(&context->snapshot, (uint8_t)id));
  return 1;
}

static int binding_set_val(lua_State *state)
{
  lua_sandbox_context_t *context = binding_context(state);
  lua_Integer id = luaL_checkinteger(state, 1);
  lua_Integer value = luaL_checkinteger(state, 2);
  luaL_argcheck(state, valid_id(id), 1, "unsupported controller id");
  context->script_commands.values[id] = clamp_value(value);
  context->script_commands.override_mask |= 1UL << id;
  return 0;
}

static int binding_block_inputs(lua_State *state)
{
  binding_context(state)->script_commands.block_inputs = 1;
  return 0;
}

static int binding_cancel_macros(lua_State *state)
{
  lua_sandbox_context_t *context = binding_context(state);
  context->macro_head = 0;
  context->macro_tail = 0;
  context->macro_count = 0;
  context->macro_active = 0;
  context->macro_step = 0;
  memset(&context->active_macro, 0, sizeof(context->active_macro));
  memset(context->macro_queue, 0, sizeof(context->macro_queue));
  memset(context->named_macros, 0, sizeof(context->named_macros));
  memset(&context->macro_commands, 0, sizeof(context->macro_commands));
  return 0;
}

static int binding_get_millis(lua_State *state)
{
  lua_pushinteger(state, binding_context(state)->frame_time_ms);
  return 1;
}

static int binding_get_ptime(lua_State *state)
{
  lua_sandbox_context_t *context = binding_context(state);
  lua_Integer id = luaL_checkinteger(state, 1);
  luaL_argcheck(state, valid_id(id), 1, "unsupported controller id");
  lua_pushinteger(state, context->frame_time_ms - context->changed_at[id]);
  return 1;
}

static int binding_device_get_val(lua_State *state)
{
  lua_sandbox_context_t *context = binding_context(state);
  lua_Integer id = luaL_checkinteger(state, 1);
  luaL_argcheck(state, id >= 0 && id < LUA_DEVICE_BUTTON_COUNT, 1,
                "unsupported device button id");
  lua_pushinteger(state,
    (context->device_buttons & (1U << (uint8_t)id)) != 0 ? 100 : 0);
  return 1;
}

extern uint8_t usb_config_request(usb_config_operation_type operation)
  __attribute__((weak));
extern usb_config_status_type usb_config_status(void)
  __attribute__((weak));
extern usb_config_error_type usb_config_error(void)
  __attribute__((weak));
extern uint8_t usb_config_reset(void) __attribute__((weak));

static int binding_device_config_request(lua_State *state)
{
  lua_Integer operation = luaL_checkinteger(state, 1);
  lua_pushboolean(state, usb_config_request != 0 &&
    usb_config_request((usb_config_operation_type)operation));
  return 1;
}

static int binding_device_config_status(lua_State *state)
{
  lua_pushinteger(state, usb_config_status == 0 ? USB_CONFIG_IDLE :
                  usb_config_status());
  return 1;
}

static int binding_device_config_error(lua_State *state)
{
  lua_pushinteger(state, usb_config_error == 0 ? USB_CONFIG_ERROR_NONE :
                  usb_config_error());
  return 1;
}

static int binding_device_config_reset(lua_State *state)
{
  lua_pushboolean(state, usb_config_reset != 0 && usb_config_reset());
  return 1;
}

static void merge_macro_step(lua_command_buffer_t *commands,
                             const lua_macro_t *macro, uint8_t step_index)
{
  const lua_macro_step_t *step = &macro->steps[step_index];
  uint8_t value_index;
  for(value_index = 0; value_index < step->value_count; value_index++)
  {
    uint8_t id = step->values[value_index].id;
    commands->values[id] = step->values[value_index].value;
    commands->override_mask |= 1UL << id;
  }
}

static void rebuild_macro_commands(lua_sandbox_context_t *context)
{
  uint8_t index;
  memset(&context->macro_commands, 0, sizeof(context->macro_commands));
  if(context->macro_active)
  {
    merge_macro_step(&context->macro_commands, &context->active_macro,
                     context->macro_step);
  }
  for(index = 0; index < LUA_SANDBOX_MAX_NAMED_MACROS; index++)
  {
    lua_named_macro_t *named = &context->named_macros[index];
    if(named->active)
    {
      merge_macro_step(&context->macro_commands, &named->macro, named->step);
    }
  }
}

static void parse_macro(lua_State *state, int table_index,
                        lua_macro_t *candidate)
{
  size_t step_count;
  size_t step_index;

  table_index = lua_absindex(state, table_index);
  luaL_checktype(state, table_index, LUA_TTABLE);
  step_count = lua_rawlen(state, table_index);
  luaL_argcheck(state, step_count > 0 &&
                step_count <= LUA_SANDBOX_MAX_MACRO_STEPS, table_index,
                "macro must contain 1..32 steps");

  memset(candidate, 0, sizeof(*candidate));
  candidate->step_count = (uint8_t)step_count;
  for(step_index = 0; step_index < step_count; step_index++)
  {
    lua_macro_step_t *step = &candidate->steps[step_index];
    lua_Integer duration;
    lua_rawgeti(state, table_index, (lua_Integer)step_index + 1);
    luaL_checktype(state, -1, LUA_TTABLE);

    lua_getfield(state, -1, "btn");
    if(!lua_isnil(state, -1))
    {
      lua_Integer id = luaL_checkinteger(state, -1);
      lua_pop(state, 1);
      luaL_argcheck(state, valid_id(id), table_index,
                    "macro contains unsupported controller id");
      lua_getfield(state, -1, "val");
      step->values[0].value = clamp_value(luaL_checkinteger(state, -1));
      lua_pop(state, 1);
      lua_getfield(state, -1, "wait");
      duration = luaL_checkinteger(state, -1);
      lua_pop(state, 1);
      luaL_argcheck(state, duration > 0 && duration <= MACRO_DURATION_MAX,
            table_index,
                    "step wait must be 1..60000");
      step->duration_ms = (uint16_t)duration;
      step->value_count = 1;
      step->values[0].id = (uint8_t)id;
      lua_pop(state, 1);
      continue;
    }
    lua_pop(state, 1);

    lua_getfield(state, -1, "duration_ms");
    duration = luaL_checkinteger(state, -1);
    luaL_argcheck(state, duration > 0 && duration <= MACRO_DURATION_MAX,
            table_index,
                  "step duration_ms must be 1..60000");
    step->duration_ms = (uint16_t)duration;
    lua_pop(state, 1);

    lua_getfield(state, -1, "values");
    luaL_checktype(state, -1, LUA_TTABLE);
    lua_pushnil(state);
    while(lua_next(state, -2) != 0)
    {
      lua_Integer id = luaL_checkinteger(state, -2);
      lua_Integer value = luaL_checkinteger(state, -1);
      luaL_argcheck(state, valid_id(id), table_index,
                    "macro contains unsupported controller id");
      luaL_argcheck(state, step->value_count < LUA_SANDBOX_MAX_STEP_VALUES,
            table_index,
                    "macro step has more than 12 values");
      step->values[step->value_count].id = (uint8_t)id;
      step->values[step->value_count].value = clamp_value(value);
      step->value_count++;
      lua_pop(state, 1);
    }
    lua_pop(state, 2);
  }
}

static int find_named_macro(lua_sandbox_context_t *context,
                            const char *name, size_t name_length)
{
  uint8_t index;
  for(index = 0; index < LUA_SANDBOX_MAX_NAMED_MACROS; index++)
  {
    if(context->named_macros[index].active &&
       context->named_macros[index].name[name_length] == '\0' &&
       memcmp(context->named_macros[index].name, name, name_length) == 0)
    {
      return index;
    }
  }
  return -1;
}

static int binding_submit_macro(lua_State *state)
{
  lua_sandbox_context_t *context = binding_context(state);
  lua_macro_t candidate;

  if(context->macro_count >= LUA_SANDBOX_MAX_MACROS)
  {
    lua_pushboolean(state, 0);
    lua_pushliteral(state, "macro queue full");
    return 2;
  }
  parse_macro(state, 1, &candidate);

  context->macro_queue[context->macro_head] = candidate;
  context->macro_head = (context->macro_head + 1U) % LUA_SANDBOX_MAX_MACROS;
  context->macro_count++;
  lua_pushboolean(state, 1);
  return 1;
}

static int binding_start_macro(lua_State *state)
{
  lua_sandbox_context_t *context = binding_context(state);
  size_t name_length;
  const char *name = luaL_checklstring(state, 1, &name_length);
  lua_macro_t candidate;
  uint8_t index;

  luaL_argcheck(state, name_length > 0 &&
                name_length < LUA_SANDBOX_MACRO_NAME_BYTES, 1,
                "macro name must contain 1..15 bytes");
  luaL_argcheck(state, memchr(name, '\0', name_length) == 0, 1,
                "macro name cannot contain NUL bytes");
  if(find_named_macro(context, name, name_length) >= 0)
  {
    lua_pushboolean(state, 0);
    return 1;
  }
  for(index = 0; index < LUA_SANDBOX_MAX_NAMED_MACROS; index++)
  {
    if(!context->named_macros[index].active)
    {
      lua_named_macro_t *named = &context->named_macros[index];
      parse_macro(state, 2, &candidate);
      memset(named, 0, sizeof(*named));
      memcpy(named->name, name, name_length);
      named->macro = candidate;
      named->step_started_at = context->frame_time_ms;
      named->active = 1;
      rebuild_macro_commands(context);
      lua_pushboolean(state, 1);
      return 1;
    }
  }
  lua_pushboolean(state, 0);
  lua_pushliteral(state, "named macro channels full");
  return 2;
}

static int binding_stop_macro(lua_State *state)
{
  lua_sandbox_context_t *context = binding_context(state);
  size_t name_length;
  const char *name = luaL_checklstring(state, 1, &name_length);
  int index;

  luaL_argcheck(state, name_length > 0 &&
                name_length < LUA_SANDBOX_MACRO_NAME_BYTES, 1,
                "macro name must contain 1..15 bytes");
  index = find_named_macro(context, name, name_length);
  if(index < 0)
  {
    lua_pushboolean(state, 0);
    return 1;
  }
  memset(&context->named_macros[index], 0,
         sizeof(context->named_macros[index]));
  rebuild_macro_commands(context);
  lua_pushboolean(state, 1);
  return 1;
}

static int binding_macro_running(lua_State *state)
{
  lua_sandbox_context_t *context = binding_context(state);
  size_t name_length;
  const char *name = luaL_checklstring(state, 1, &name_length);
  luaL_argcheck(state, name_length > 0 &&
                name_length < LUA_SANDBOX_MACRO_NAME_BYTES, 1,
                "macro name must contain 1..15 bytes");
  lua_pushboolean(state, find_named_macro(context, name, name_length) >= 0);
  return 1;
}

static int binding_display_clear(lua_State *state)
{
  lua_sandbox_context_t *context = binding_context(state);
  if(context->port.display_clear != 0)
  {
    context->port.display_clear();
  }
  return 0;
}

static int binding_display_draw_text(lua_State *state)
{
  lua_sandbox_context_t *context = binding_context(state);
  lua_Integer x = luaL_checkinteger(state, 1);
  lua_Integer y = luaL_checkinteger(state, 2);
  size_t length;
  const char *text = luaL_checklstring(state, 3, &length);
  luaL_argcheck(state, x >= 0 && x < 128, 1, "x must be 0..127");
  luaL_argcheck(state, y >= 0 && y < 32, 2, "y must be 0..31");
  luaL_argcheck(state, length <= DISPLAY_TEXT_MAX, 3, "text is too long");
  luaL_argcheck(state, memchr(text, '\0', length) == 0, 3,
                "text cannot contain NUL bytes");
  if(context->port.display_draw_text != 0)
  {
    context->port.display_draw_text((uint8_t)x, (uint8_t)y, text);
  }
  return 0;
}

static int binding_display_draw_pixel(lua_State *state)
{
  lua_sandbox_context_t *context = binding_context(state);
  lua_Integer x = luaL_checkinteger(state, 1);
  lua_Integer y = luaL_checkinteger(state, 2);
  luaL_argcheck(state, x >= 0 && x < 128, 1, "x must be 0..127");
  luaL_argcheck(state, y >= 0 && y < 32, 2, "y must be 0..31");
  if(context->port.display_draw_pixel != 0)
  {
    context->port.display_draw_pixel((uint8_t)x, (uint8_t)y,
                                     (uint8_t)lua_toboolean(state, 3));
  }
  return 0;
}

static int binding_led_set(lua_State *state)
{
  lua_sandbox_context_t *context = binding_context(state);
  luaL_checktype(state, 1, LUA_TBOOLEAN);
  if(context->port.led_set != 0)
  {
    context->port.led_set((uint8_t)lua_toboolean(state, 1));
  }
  return 0;
}

static const luaL_Reg controller_bindings[] =
{
  {"get_val", binding_get_val},
  {"set_val", binding_set_val},
  {"block_inputs", binding_block_inputs},
  {"cancel_macros", binding_cancel_macros},
  {"submit_macro", binding_submit_macro},
  {"start_macro", binding_start_macro},
  {"stop_macro", binding_stop_macro},
  {"macro_running", binding_macro_running},
  {"get_millis", binding_get_millis},
  {"get_ptime", binding_get_ptime},
  {0, 0}
};

static const luaL_Reg device_bindings[] =
{
  {"get_val", binding_device_get_val},
  {"config_request", binding_device_config_request},
  {"config_status", binding_device_config_status},
  {"config_error", binding_device_config_error},
  {"config_reset", binding_device_config_reset},
  {0, 0}
};

static const luaL_Reg display_bindings[] =
{
  {"clear", binding_display_clear},
  {"draw_text", binding_display_draw_text},
  {"draw_pixel", binding_display_draw_pixel},
  {0, 0}
};

static const luaL_Reg led_bindings[] =
{
  {"set", binding_led_set},
  {0, 0}
};

extern const luaL_Reg lua_storage_bindings[];
extern void lua_storage_init(const lua_sandbox_port_t *port);
extern void lua_storage_task(void);

static void register_module(lua_State *state, const char *name,
                            const luaL_Reg *bindings)
{
  lua_newtable(state);
  lua_pushlightuserdata(state, &sandbox);
  luaL_setfuncs(state, bindings, 1);
  lua_setglobal(state, name);
}

static void register_controller_constants(lua_State *state)
{
  static const char *names[LUA_CTRL_COUNT] =
  {
    "DPAD_UP", "DPAD_DOWN", "DPAD_LEFT", "DPAD_RIGHT",
    "A", "B", "X", "Y", "LB", "RB", "VIEW", "MENU", "LS", "RS",
    0, 0, "LX", "LY", "RX", "RY", "LT", "RT"
  };
  uint8_t id;
  lua_getglobal(state, "controller");
  for(id = 0; id < LUA_CTRL_COUNT; id++)
  {
    if(names[id] == 0)
    {
      continue;
    }
    lua_pushinteger(state, id);
    lua_setfield(state, -2, names[id]);
  }
  lua_pop(state, 1);
}

static void register_device_constants(lua_State *state)
{
  static const char *names[LUA_DEVICE_BUTTON_COUNT] =
  {
    "BTN_UP", "BTN_DOWN", "BTN_SELECT", "BTN_BACK"
  };
  uint8_t id;
  lua_getglobal(state, "device");
  for(id = 0; id < LUA_DEVICE_BUTTON_COUNT; id++)
  {
    lua_pushinteger(state, id);
    lua_setfield(state, -2, names[id]);
  }
  lua_pop(state, 1);
}

static void remove_global(lua_State *state, const char *name)
{
  lua_pushnil(state);
  lua_setglobal(state, name);
}

static void instruction_hook(lua_State *state, lua_Debug *debug)
{
  lua_sandbox_context_t *context =
    *(lua_sandbox_context_t **)lua_getextraspace(state);
  (void)debug;
  if(context->hook_calls_remaining == 0)
  {
    luaL_error(state, "instruction budget exceeded");
  }
  context->hook_calls_remaining--;
  if(context->port.get_millis != 0 && context->hook_deadline != 0 &&
     (int32_t)(context->port.get_millis() - context->hook_deadline) >= 0)
  {
    luaL_error(state, "execution deadline exceeded");
  }
}

static void copy_error(lua_State *state, char *error, size_t capacity)
{
  const char *message;
  if(error == 0 || capacity == 0)
  {
    return;
  }
  message = lua_tostring(state, -1);
  if(message == 0)
  {
    message = "unknown Lua error";
  }
  strncpy(error, message, capacity - 1U);
  error[capacity - 1U] = '\0';
}

uint8_t lua_sandbox_init(lua_Alloc allocator, void *allocator_context,
                         const lua_sandbox_port_t *port)
{
  lua_State *state;
  if(allocator == 0 || port == 0 || port->get_millis == 0)
  {
    return 0;
  }
  memset(&sandbox, 0, sizeof(sandbox));
  sandbox.port = *port;
  state = lua_newstate(allocator, allocator_context);
  if(state == 0)
  {
    return 0;
  }
  sandbox.state = state;
  *(lua_sandbox_context_t **)lua_getextraspace(state) = &sandbox;

  luaL_requiref(state, LUA_GNAME, luaopen_base, 1);
  lua_pop(state, 1);
  luaL_requiref(state, LUA_TABLIBNAME, luaopen_table, 1);
  lua_pop(state, 1);
  luaL_requiref(state, LUA_STRLIBNAME, luaopen_string, 1);
  lua_pop(state, 1);
  luaL_requiref(state, LUA_MATHLIBNAME, luaopen_math, 1);
  lua_pop(state, 1);
  remove_global(state, "dofile");
  remove_global(state, "load");
  remove_global(state, "loadfile");
  remove_global(state, "collectgarbage");

  register_module(state, "controller", controller_bindings);
  register_controller_constants(state);
  register_module(state, "device", device_bindings);
  register_device_constants(state);
  register_module(state, "display", display_bindings);
  register_module(state, "led", led_bindings);
  register_module(state, "storage", lua_storage_bindings);
  lua_storage_init(port);
  sandbox.initialized = 1;
  return 1;
}

uint8_t lua_sandbox_load(const char *script, size_t script_length,
                         char *error, size_t error_capacity)
{
  int status;
  if(!sandbox.initialized || script == 0 || script_length == 0 ||
     script_length > LUA_SANDBOX_MAX_SCRIPT_BYTES)
  {
    return 0;
  }
  status = luaL_loadbufferx(sandbox.state, script, script_length,
                            "user-script", "t");
  if(status == LUA_OK)
  {
    sandbox.hook_interval = LOAD_HOOK_INTERVAL;
    sandbox.hook_calls_remaining =
      LOAD_INSTRUCTION_LIMIT / LOAD_HOOK_INTERVAL;
    sandbox.hook_deadline = sandbox.port.get_millis() + LOAD_DEADLINE_MS;
    lua_sethook(sandbox.state, instruction_hook, LUA_MASKCOUNT,
                LOAD_HOOK_INTERVAL);
    status = lua_pcall(sandbox.state, 0, 0, 0);
    lua_sethook(sandbox.state, 0, 0, 0);
  }
  if(status != LUA_OK)
  {
    copy_error(sandbox.state, error, error_capacity);
    lua_pop(sandbox.state, 1);
    return 0;
  }
  return 1;
}

void lua_sandbox_begin_frame(const controller_data_t *snapshot,
                             uint32_t current_time_ms)
{
  uint8_t id;
  if(!sandbox.initialized || snapshot == 0)
  {
    return;
  }
  sandbox.snapshot = *snapshot;
  sandbox.frame_time_ms = current_time_ms;
  sandbox.device_buttons = sandbox.port.read_device_buttons != 0 ?
    sandbox.port.read_device_buttons() : 0;
  memset(&sandbox.script_commands, 0, sizeof(sandbox.script_commands));
  for(id = 0; id < LUA_CTRL_COUNT; id++)
  {
    int16_t value;
    if(!valid_id(id))
    {
      continue;
    }
    value = snapshot_value(snapshot, id);
    if(!sandbox.ptime_initialized || value != sandbox.previous_values[id])
    {
      sandbox.previous_values[id] = value;
      sandbox.changed_at[id] = current_time_ms;
    }
  }
  sandbox.ptime_initialized = 1;
}

uint8_t lua_sandbox_run_event(const char *event_name,
                              const lua_sandbox_budget_t *budget,
                              char *error, size_t error_capacity)
{
  int status;
  if(!sandbox.initialized || event_name == 0 || budget == 0 ||
     budget->instruction_limit == 0 || budget->hook_interval == 0)
  {
    return 0;
  }
  lua_getglobal(sandbox.state, event_name);
  if(lua_isnil(sandbox.state, -1))
  {
    lua_pop(sandbox.state, 1);
    return 1;
  }
  if(!lua_isfunction(sandbox.state, -1))
  {
    lua_pop(sandbox.state, 1);
    return 0;
  }
  sandbox.hook_interval = budget->hook_interval;
  sandbox.hook_calls_remaining =
    (budget->instruction_limit + budget->hook_interval - 1U) /
    budget->hook_interval;
  sandbox.hook_deadline = budget->deadline_ms == 0 ? 0 :
    sandbox.port.get_millis() + budget->deadline_ms;
  lua_sethook(sandbox.state, instruction_hook, LUA_MASKCOUNT,
              sandbox.hook_interval);
  status = lua_pcall(sandbox.state, 0, 0, 0);
  lua_sethook(sandbox.state, 0, 0, 0);
  if(status != LUA_OK)
  {
    copy_error(sandbox.state, error, error_capacity);
    lua_pop(sandbox.state, 1);
    memset(&sandbox.script_commands, 0, sizeof(sandbox.script_commands));
    return 0;
  }
  return 1;
}

void lua_sandbox_task(uint32_t current_time_ms)
{
  uint8_t index;
  if(!sandbox.initialized)
  {
    return;
  }
  if(!sandbox.macro_active && sandbox.macro_count != 0)
  {
    sandbox.active_macro = sandbox.macro_queue[sandbox.macro_tail];
    sandbox.macro_tail = (sandbox.macro_tail + 1U) % LUA_SANDBOX_MAX_MACROS;
    sandbox.macro_count--;
    sandbox.macro_active = 1;
    sandbox.macro_step = 0;
    sandbox.macro_step_started_at = current_time_ms;
  }
  while(sandbox.macro_active &&
        current_time_ms - sandbox.macro_step_started_at >=
          sandbox.active_macro.steps[sandbox.macro_step].duration_ms)
  {
    sandbox.macro_step_started_at +=
      sandbox.active_macro.steps[sandbox.macro_step].duration_ms;
    sandbox.macro_step++;
    if(sandbox.macro_step >= sandbox.active_macro.step_count)
    {
      sandbox.macro_active = 0;
      break;
    }
  }
  for(index = 0; index < LUA_SANDBOX_MAX_NAMED_MACROS; index++)
  {
    lua_named_macro_t *named = &sandbox.named_macros[index];
    while(named->active &&
          current_time_ms - named->step_started_at >=
            named->macro.steps[named->step].duration_ms)
    {
      named->step_started_at += named->macro.steps[named->step].duration_ms;
      named->step++;
      if(named->step >= named->macro.step_count)
      {
        memset(named, 0, sizeof(*named));
        break;
      }
    }
  }
  rebuild_macro_commands(&sandbox);
}

void lua_sandbox_apply(controller_data_t *output)
{
  if(!sandbox.initialized || output == 0)
  {
    return;
  }
  apply_commands(output, &sandbox.script_commands);
  apply_commands(output, &sandbox.macro_commands);
}

void lua_sandbox_storage_task(void)
{
  if(sandbox.initialized)
  {
    lua_storage_task();
  }
}

void lua_sandbox_shutdown(void)
{
  if(sandbox.state != 0)
  {
    lua_close(sandbox.state);
  }
  memset(&sandbox, 0, sizeof(sandbox));
}