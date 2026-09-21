#include <stdio.h>
#include <string.h>
#include "lua_arena.h"
#include "lua_sandbox.h"
#include "lua_storage.h"

#define TEST_ARENA_BYTES  (128U * 1024U)
#define TEST_FLASH_BYTES   LUA_SANDBOX_STORAGE_IMAGE_BYTES
#define BUTTON_A           0x0010U
#define BUTTON_B           0x0020U
#define BUTTON_X           0x0040U
#define BUTTON_Y           0x0080U
#define BUTTON_DPAD_DOWN   0x0200U
#define BUTTON_LS          0x4000U
#define BUTTON_RS          0x8000U
#define TEST_STORAGE_MAGIC 0x53505652UL
#define TEST_STORAGE_LEGACY_LOADOUTS (2U * 36U * 2U)
#define TEST_STORAGE_ENTRIES 32U

typedef struct
{
  char key[32];
  uint8_t type;
  uint8_t reserved;
  uint16_t value_length;
  union
  {
    uint8_t boolean_value;
    int32_t integer_value;
    float float_value;
    char string_value[64];
  } value;
} test_storage_entry_t;

typedef struct
{
  int8_t vertical;
  int8_t horizontal;
  uint8_t movement;
  uint8_t deadzone;
  uint8_t valid;
  uint8_t rapid_fire_percent;
} test_storage_loadout_t;

typedef struct
{
  uint32_t magic;
  uint16_t version;
  uint16_t entry_count;
  uint32_t generation;
  uint32_t crc32;
  test_storage_loadout_t loadouts[TEST_STORAGE_LEGACY_LOADOUTS];
  test_storage_entry_t entries[TEST_STORAGE_ENTRIES];
} test_storage_legacy_image_t;

static uint64_t test_memory[TEST_ARENA_BYTES / sizeof(uint64_t)];
static lua_arena_t test_arena;
static uint8_t mock_device_buttons;
static uint8_t mock_led_state;
static uint32_t mock_time_ms = 1U;
static char mock_display_lines[4][22];
static uint8_t mock_flash[2][TEST_FLASH_BYTES];
static uint32_t mock_flash_erase_count;
static uint32_t mock_flash_program_count;

static uint32_t test_crc32(const void *data, size_t length)
{
  const uint8_t *bytes = (const uint8_t *)data;
  uint32_t crc = 0xFFFFFFFFUL;
  size_t index;
  uint8_t bit;
  for(index = 0; index < length; index++)
  {
    crc ^= bytes[index];
    for(bit = 0; bit < 8; bit++)
    {
      crc = (crc >> 1) ^ (0xEDB88320UL &
            (uint32_t)-(int32_t)(crc & 1U));
    }
  }
  return ~crc;
}

static uint32_t mock_millis(void)
{
  return mock_time_ms;
}

static uint8_t mock_read_device_buttons(void)
{
  return mock_device_buttons;
}

static void mock_display_clear(void)
{
  memset(mock_display_lines, 0, sizeof(mock_display_lines));
}

static void mock_display_draw_text(uint8_t x, uint8_t y, const char *text)
{
  size_t offset = x / 6U;
  size_t capacity = sizeof(mock_display_lines[0]) - offset - 1U;
  if(y >= 32U || offset >= sizeof(mock_display_lines[0]) - 1U)
  {
    return;
  }
  strncpy(&mock_display_lines[y / 8U][offset], text, capacity);
  mock_display_lines[y / 8U][sizeof(mock_display_lines[0]) - 1U] = '\0';
}

static uint8_t mock_flash_read(uint8_t slot, uint32_t offset, void *data,
                               uint32_t length)
{
  if(slot > 1 || data == 0 || offset > TEST_FLASH_BYTES ||
     length > TEST_FLASH_BYTES - offset)
  {
    return 0;
  }
  memcpy(data, &mock_flash[slot][offset], length);
  return 1;
}

static uint8_t mock_flash_erase(uint8_t slot)
{
  if(slot > 1)
  {
    return 0;
  }
  mock_flash_erase_count++;
  memset(mock_flash[slot], 0xFF, TEST_FLASH_BYTES);
  return 1;
}

static uint8_t mock_flash_program(uint8_t slot, uint32_t offset,
                                  const void *data, uint32_t length)
{
  const uint8_t *bytes = (const uint8_t *)data;
  uint32_t index;
  if(slot > 1 || data == 0 || offset > TEST_FLASH_BYTES ||
     length > TEST_FLASH_BYTES - offset)
  {
    return 0;
  }
  mock_flash_program_count++;
  for(index = 0; index < length; index++)
  {
    mock_flash[slot][offset + index] &= bytes[index];
  }
  return 1;
}

static uint8_t mock_flash_commit_allowed(void)
{
  return 1;
}

static void mock_led_set(uint8_t state)
{
  mock_led_state = state != 0;
}

static const lua_sandbox_port_t test_port =
{
  mock_millis,
  mock_read_device_buttons,
  mock_display_clear,
  mock_display_draw_text,
  0,
  mock_led_set,
  mock_flash_read,
  mock_flash_erase,
  mock_flash_program,
  mock_flash_commit_allowed
};

static uint8_t run_script(const char *script, uint16_t expected_buttons)
{
  static const lua_sandbox_budget_t budget = {4000U, 1U, 100U};
  controller_data_t input;
  controller_data_t output;
  char error[128];

  memset(&input, 0, sizeof(input));
  output = input;
  if(!lua_arena_init(&test_arena, test_memory, sizeof(test_memory)) ||
     !lua_sandbox_init(lua_arena_alloc, &test_arena, &test_port) ||
     !lua_sandbox_load(script, strlen(script), error, sizeof(error)))
  {
    fprintf(stderr, "Lua load failed: %s\n", error);
    return 0;
  }
  lua_sandbox_begin_frame(&input, 1U);
  if(!lua_sandbox_run_event("on_input", &budget, error, sizeof(error)))
  {
    fprintf(stderr, "Lua event failed: %s\n", error);
    lua_sandbox_shutdown();
    return 0;
  }
  lua_sandbox_apply(&output);
  lua_sandbox_shutdown();
  return output.buttons == expected_buttons;
}

static uint8_t test_compact_macro(void)
{
  static const lua_sandbox_budget_t budget = {4000U, 1U, 100U};
  const char *script =
    "function on_input() controller.submit_macro({"
    "{btn=controller.B,val=100,wait=60},"
    "{btn=controller.B,val=0,wait=40}}) end";
  controller_data_t input;
  controller_data_t output;
  char error[128];

  memset(&input, 0, sizeof(input));
  if(!lua_arena_init(&test_arena, test_memory, sizeof(test_memory)) ||
     !lua_sandbox_init(lua_arena_alloc, &test_arena, &test_port) ||
     !lua_sandbox_load(script, strlen(script), error, sizeof(error)))
  {
    return 0;
  }
  lua_sandbox_begin_frame(&input, 10U);
  if(!lua_sandbox_run_event("on_input", &budget, error, sizeof(error)))
  {
    lua_sandbox_shutdown();
    return 0;
  }
  output = input;
  lua_sandbox_task(10U);
  lua_sandbox_apply(&output);
  if((output.buttons & BUTTON_B) == 0)
  {
    lua_sandbox_shutdown();
    return 0;
  }
  output = input;
  lua_sandbox_task(70U);
  lua_sandbox_apply(&output);
  lua_sandbox_shutdown();
  return (output.buttons & BUTTON_B) == 0;
}

static uint8_t test_cancel_macros(void)
{
  static const lua_sandbox_budget_t budget = {4000U, 1U, 100U};
  const char *script =
    "local macro={{btn=controller.B,val=100,wait=100}} "
    "function on_input() "
    "if device.get_val(device.BTN_SELECT)==100 then "
    "controller.cancel_macros() else "
    "controller.submit_macro(macro) controller.submit_macro(macro) end end";
  controller_data_t input;
  controller_data_t output;
  char error[128];

  memset(&input, 0, sizeof(input));
  input.buttons = BUTTON_A;
  mock_device_buttons = 0;
  mock_led_state = 1;
  if(!lua_arena_init(&test_arena, test_memory, sizeof(test_memory)) ||
     !lua_sandbox_init(lua_arena_alloc, &test_arena, &test_port) ||
     !lua_sandbox_load(script, strlen(script), error, sizeof(error)))
  {
    return 0;
  }
  lua_sandbox_begin_frame(&input, 10U);
  if(!lua_sandbox_run_event("on_input", &budget, error, sizeof(error)))
  {
    lua_sandbox_shutdown();
    return 0;
  }
  lua_sandbox_task(10U);
  output = input;
  lua_sandbox_apply(&output);
  if(output.buttons != (BUTTON_A | BUTTON_B))
  {
    lua_sandbox_shutdown();
    return 0;
  }

  mock_device_buttons = 1U << LUA_DEVICE_BTN_SELECT;
  lua_sandbox_begin_frame(&input, 20U);
  lua_sandbox_task(20U);
  if(!lua_sandbox_run_event("on_input", &budget, error, sizeof(error)))
  {
    lua_sandbox_shutdown();
    return 0;
  }
  output = input;
  lua_sandbox_apply(&output);
  if(output.buttons != BUTTON_A)
  {
    lua_sandbox_shutdown();
    return 0;
  }

  output = input;
  lua_sandbox_task(1000U);
  lua_sandbox_apply(&output);
  lua_sandbox_shutdown();
  return output.buttons == BUTTON_A;
}

static uint8_t test_named_macros(void)
{
  static const lua_sandbox_budget_t budget = {4000U, 1U, 100U};
  const char *script =
    "local started=false "
    "local b={{btn=controller.B,val=100,wait=100}} "
    "local x={{btn=controller.X,val=100,wait=200}} "
    "function on_input() "
    "if not started then "
    "assert(controller.start_macro('crouch',b)) "
    "assert(not controller.start_macro('crouch',b)) "
    "assert(controller.start_macro('repeat',x)) started=true end "
    "if device.get_val(device.BTN_SELECT)==100 then "
    "assert(controller.macro_running('crouch')) "
    "assert(controller.stop_macro('crouch')) "
    "assert(not controller.macro_running('crouch')) "
    "assert(controller.macro_running('repeat')) end end";
  controller_data_t input;
  controller_data_t output;
  char error[128];

  memset(&input, 0, sizeof(input));
  mock_device_buttons = 0;
  if(!lua_arena_init(&test_arena, test_memory, sizeof(test_memory)) ||
     !lua_sandbox_init(lua_arena_alloc, &test_arena, &test_port) ||
     !lua_sandbox_load(script, strlen(script), error, sizeof(error)))
  {
    return 0;
  }
  lua_sandbox_begin_frame(&input, 10U);
  if(!lua_sandbox_run_event("on_input", &budget, error, sizeof(error)))
  {
    lua_sandbox_shutdown();
    return 0;
  }
  output = input;
  lua_sandbox_apply(&output);
  if(output.buttons != (BUTTON_B | BUTTON_X))
  {
    lua_sandbox_shutdown();
    return 0;
  }

  mock_device_buttons = 1U << LUA_DEVICE_BTN_SELECT;
  lua_sandbox_begin_frame(&input, 20U);
  if(!lua_sandbox_run_event("on_input", &budget, error, sizeof(error)))
  {
    lua_sandbox_shutdown();
    return 0;
  }
  output = input;
  lua_sandbox_apply(&output);
  if(output.buttons != BUTTON_X)
  {
    lua_sandbox_shutdown();
    return 0;
  }

  output = input;
  lua_sandbox_task(150U);
  lua_sandbox_apply(&output);
  if(output.buttons != BUTTON_X)
  {
    lua_sandbox_shutdown();
    return 0;
  }
  output = input;
  lua_sandbox_task(210U);
  lua_sandbox_apply(&output);
  lua_sandbox_shutdown();
  return output.buttons == 0;
}

static uint8_t run_default_frame(controller_data_t *input,
                                 const lua_sandbox_budget_t *budget,
                                 char *error, size_t error_capacity,
                                 uint8_t buttons, uint32_t time_ms)
{
  mock_device_buttons = buttons;
  mock_time_ms = time_ms;
  lua_sandbox_begin_frame(input, time_ms);
  lua_sandbox_task(time_ms);
  return lua_sandbox_run_event("on_input", budget, error, error_capacity);
}

static uint8_t test_bf6_script(void)
{
  static const lua_sandbox_budget_t budget = {4000U, 1U, 100U};
  static char script[LUA_SANDBOX_MAX_SCRIPT_BYTES + 1U];
  controller_data_t input;
  controller_data_t output;
  char error[128];
  FILE *file;
  size_t length;
  uint32_t time_ms = 1U;
  uint8_t field;

  file = fopen("scripts/default.lua", "rb");
  if(file == 0)
  {
    return 0;
  }
  length = fread(script, 1, sizeof(script), file);
  fclose(file);
  if(length == 0 || length > LUA_SANDBOX_MAX_SCRIPT_BYTES ||
     !lua_arena_init(&test_arena, test_memory, sizeof(test_memory)) ||
     !lua_sandbox_init(lua_arena_alloc, &test_arena, &test_port))
  {
    return 0;
  }
  if(!lua_sandbox_load(script, length, error, sizeof(error)))
  {
    fprintf(stderr, "BF6 script load failed: %s\n", error);
    lua_sandbox_shutdown();
    return 0;
  }

  memset(&input, 0, sizeof(input));
  mock_flash_erase_count = 0;
  mock_flash_program_count = 0;
  if(!run_default_frame(&input, &budget, error, sizeof(error), 0, time_ms++) ||
     !run_default_frame(&input, &budget, error, sizeof(error),
                        1U << LUA_DEVICE_BTN_BACK, time_ms++) ||
     !run_default_frame(&input, &budget, error, sizeof(error), 0, time_ms++) ||
     !run_default_frame(&input, &budget, error, sizeof(error),
                        1U << LUA_DEVICE_BTN_UP, time_ms++) ||
    !run_default_frame(&input, &budget, error, sizeof(error), 0, time_ms++) ||
    !run_default_frame(&input, &budget, error, sizeof(error),
              1U << LUA_DEVICE_BTN_UP, time_ms++) ||
    !run_default_frame(&input, &budget, error, sizeof(error), 0, time_ms++) ||
    !run_default_frame(&input, &budget, error, sizeof(error),
               1U << LUA_DEVICE_BTN_BACK, time_ms++) ||
    !run_default_frame(&input, &budget, error, sizeof(error), 0, time_ms++) ||
    !run_default_frame(&input, &budget, error, sizeof(error),
           1U << LUA_DEVICE_BTN_BACK, time_ms++) ||
    !run_default_frame(&input, &budget, error, sizeof(error), 0, time_ms++) ||
        !run_default_frame(&input, &budget, error, sizeof(error),
          1U << LUA_DEVICE_BTN_BACK, time_ms++) ||
        !run_default_frame(&input, &budget, error, sizeof(error), 0, time_ms++) ||
    !run_default_frame(&input, &budget, error, sizeof(error),
               1U << LUA_DEVICE_BTN_UP, time_ms++) ||
    !run_default_frame(&input, &budget, error, sizeof(error), 0, time_ms++))
  {
    fprintf(stderr, "BF6 setup failed: %s\n", error);
    lua_sandbox_shutdown();
    return 0;
  }

  input.lt = 1023U;
  input.rt = 1023U;
  if(!run_default_frame(&input, &budget, error, sizeof(error), 0, time_ms++))
  {
    fprintf(stderr, "BF6 unsaved gameplay frame failed: %s\n", error);
    lua_sandbox_shutdown();
    return 0;
  }
  output = input;
  lua_sandbox_apply(&output);
  if(output.rx == input.rx && output.ry == input.ry)
  {
    fprintf(stderr, "BF6 output inactive before save\n");
    lua_sandbox_shutdown();
    return 0;
  }

  for(field = 1U; field < 7U; field++)
  {
    if(!run_default_frame(&input, &budget, error, sizeof(error),
                          1U << LUA_DEVICE_BTN_BACK, time_ms++) ||
       !run_default_frame(&input, &budget, error, sizeof(error),
                          0, time_ms++))
    {
      fprintf(stderr, "BF6 navigation failed: %s\n", error);
      lua_sandbox_shutdown();
      return 0;
    }
  }
  if(!run_default_frame(&input, &budget, error, sizeof(error),
                        1U << LUA_DEVICE_BTN_UP, time_ms++) ||
     !run_default_frame(&input, &budget, error, sizeof(error), 0, time_ms++))
  {
    fprintf(stderr, "BF6 save failed: %s\n", error);
    lua_sandbox_shutdown();
    return 0;
  }

  input.lt = 1023U;
  input.rt = 1023U;
  if(!run_default_frame(&input, &budget, error, sizeof(error), 0, 100U))
  {
    fprintf(stderr, "BF6 gameplay frame failed: %s\n", error);
    lua_sandbox_shutdown();
    return 0;
  }
  output = input;
  lua_sandbox_apply(&output);
  if(mock_flash_erase_count != 0U || mock_flash_program_count != 0U)
  {
    fprintf(stderr, "BF6 saved unchanged settings\n");
    lua_sandbox_shutdown();
    return 0;
  }
  if(output.rx == input.rx && output.ry == input.ry)
  {
    fprintf(stderr, "BF6 output inactive: [%s] [%s]\n",
            mock_display_lines[0], mock_display_lines[1]);
  }
  lua_sandbox_shutdown();
  return output.rx != input.rx || output.ry != input.ry;
}

static uint8_t test_default_script(void)
{
  static const lua_sandbox_budget_t budget = {4000U, 1U, 100U};
  static const uint8_t setup_buttons[] =
  {
    0,
    1U << LUA_DEVICE_BTN_UP,
    0,
    1U << LUA_DEVICE_BTN_UP,
    0,
    1U << LUA_DEVICE_BTN_BACK,
    0
  };
  static char script[LUA_SANDBOX_MAX_SCRIPT_BYTES + 1U];
  controller_data_t input;
  controller_data_t output;
  char error[128];
  FILE *file;
  size_t length;
  size_t step;

  file = fopen("scripts/default.lua", "rb");
  if(file == 0)
  {
    return 0;
  }
  length = fread(script, 1, sizeof(script), file);
  fclose(file);
  if(length == 0 || length > LUA_SANDBOX_MAX_SCRIPT_BYTES)
  {
    return 0;
  }

  memset(&input, 0, sizeof(input));
  input.buttons = BUTTON_A;
  mock_device_buttons = 0;
  if(!lua_arena_init(&test_arena, test_memory, sizeof(test_memory)) ||
     !lua_sandbox_init(lua_arena_alloc, &test_arena, &test_port) ||
     !lua_sandbox_load(script, length, error, sizeof(error)))
  {
    fprintf(stderr, "Default Lua load failed: %s\n", error);
    return 0;
  }
  lua_sandbox_begin_frame(&input, 1U);
  if(!lua_sandbox_run_event("on_input", &budget, error, sizeof(error)))
  {
    fprintf(stderr, "Default Lua initial frame failed: %s\n", error);
    lua_sandbox_shutdown();
    return 0;
  }

  if(strcmp(mock_display_lines[0], "SELECT GAME") != 0 ||
      strcmp(mock_display_lines[3], "> R6 <") != 0 || mock_led_state != 0)
  {
    fprintf(stderr, "Default Lua did not start at game selection\n");
    lua_sandbox_shutdown();
    return 0;
  }

  mock_device_buttons = 1U << LUA_DEVICE_BTN_BACK;
  mock_time_ms = 2U;
  lua_sandbox_begin_frame(&input, mock_time_ms);
  if(!lua_sandbox_run_event("on_input", &budget, error, sizeof(error)) ||
     strcmp(mock_display_lines[3], "> RUST <") != 0)
  {
    fprintf(stderr, "Default Lua did not select RUST\n");
    lua_sandbox_shutdown();
    return 0;
  }

  mock_device_buttons = 0;
  mock_time_ms = 3U;
  lua_sandbox_begin_frame(&input, mock_time_ms);
  if(!lua_sandbox_run_event("on_input", &budget, error, sizeof(error)))
  {
    lua_sandbox_shutdown();
    return 0;
  }

  mock_device_buttons = 1U << LUA_DEVICE_BTN_UP;
  mock_time_ms = 4U;
  lua_sandbox_begin_frame(&input, mock_time_ms);
  if(!lua_sandbox_run_event("on_input", &budget, error, sizeof(error)))
  {
    fprintf(stderr, "Default Lua RUST frame failed: %s\n", error);
    lua_sandbox_shutdown();
    return 0;
  }
  if(strcmp(mock_display_lines[0], "RUST") != 0 ||
      strcmp(mock_display_lines[1], "COMING SOON") != 0 ||
      mock_led_state != 0)
  {
    fprintf(stderr, "Default Lua entered R6 settings for RUST\n");
    lua_sandbox_shutdown();
    return 0;
  }

  if(!run_default_frame(&input, &budget, error, sizeof(error), 0, 5U) ||
    !run_default_frame(&input, &budget, error, sizeof(error),
              1U << LUA_DEVICE_BTN_DOWN, 6U) ||
     strcmp(mock_display_lines[0], "SELECT GAME") != 0 ||
     !run_default_frame(&input, &budget, error, sizeof(error), 0, 7U) ||
     !run_default_frame(&input, &budget, error, sizeof(error),
                        1U << LUA_DEVICE_BTN_SELECT, 8U) ||
     strcmp(mock_display_lines[3], "> R6 <") != 0 ||
     !run_default_frame(&input, &budget, error, sizeof(error), 0, 9U) ||
    !run_default_frame(&input, &budget, error, sizeof(error),
              1U << LUA_DEVICE_BTN_UP, 10U) ||
     strcmp(mock_display_lines[0], "SELECT SIDE") != 0 ||
     !run_default_frame(&input, &budget, error, sizeof(error), 0, 11U) ||
    !run_default_frame(&input, &budget, error, sizeof(error),
              1U << LUA_DEVICE_BTN_UP, 12U))
  {
    fprintf(stderr, "Default Lua did not route R6 into settings\n");
    lua_sandbox_shutdown();
    return 0;
  }
      if(strcmp(&mock_display_lines[0][6], ">SLEDGE<") != 0 ||
        strcmp(&mock_display_lines[1][6], "THATCHER") != 0 ||
        strcmp(&mock_display_lines[2][9], "ASH") != 0 ||
        strcmp(&mock_display_lines[3][6], "THERMITE") != 0 ||
        mock_led_state == 0)
  {
       fprintf(stderr, "Default Lua operator rows: [%s] [%s] [%s] [%s]\n",
            mock_display_lines[0], mock_display_lines[1],
            mock_display_lines[2], mock_display_lines[3]);
    lua_sandbox_shutdown();
    return 0;
  }
  output = input;
  lua_sandbox_apply(&output);
  if(output.buttons != BUTTON_A)
  {
    lua_sandbox_shutdown();
    return 0;
  }

  for(step = 0; step < sizeof(setup_buttons); step++)
  {
    mock_device_buttons = setup_buttons[step];
    mock_time_ms = (uint32_t)step + 13U;
    lua_sandbox_begin_frame(&input, mock_time_ms);
    lua_sandbox_task(mock_time_ms);
    if(!lua_sandbox_run_event("on_input", &budget, error, sizeof(error)))
    {
      fprintf(stderr, "Default Lua setup frame %u failed: %s\n",
              (unsigned int)step, error);
      lua_sandbox_shutdown();
      return 0;
    }
  }

  mock_device_buttons = 1U << LUA_DEVICE_BTN_BACK;
  mock_time_ms = 20U;
  lua_sandbox_begin_frame(&input, mock_time_ms);
  if(!lua_sandbox_run_event("on_input", &budget, error, sizeof(error)))
  {
    fprintf(stderr, "Default Lua held increment failed: %s\n", error);
    lua_sandbox_shutdown();
    return 0;
  }
  mock_time_ms = 370U;
  lua_sandbox_begin_frame(&input, mock_time_ms);
  if(!lua_sandbox_run_event("on_input", &budget, error, sizeof(error)))
  {
    fprintf(stderr, "Default Lua first repeat failed: %s\n", error);
    lua_sandbox_shutdown();
    return 0;
  }
  mock_time_ms = 450U;
  lua_sandbox_begin_frame(&input, mock_time_ms);
  if(!lua_sandbox_run_event("on_input", &budget, error, sizeof(error)))
  {
    fprintf(stderr, "Default Lua second repeat failed: %s\n", error);
    lua_sandbox_shutdown();
    return 0;
  }
  mock_device_buttons = 0;

  input.lt = 1023U;
  input.rt = 1023U;
  mock_time_ms = 451U;
  lua_sandbox_begin_frame(&input, mock_time_ms);
  lua_sandbox_task(mock_time_ms);
  if(!lua_sandbox_run_event("on_input", &budget, error, sizeof(error)))
  {
    fprintf(stderr, "Default Lua gameplay frame failed: %s\n", error);
    lua_sandbox_shutdown();
    return 0;
  }
  output = input;
  lua_sandbox_apply(&output);
  if(output.buttons != BUTTON_A || output.ry != -1310)
  {
    lua_sandbox_shutdown();
    return 0;
  }

  input.rx = 16384;
  input.ry = 0;
  mock_time_ms = 452U;
  lua_sandbox_begin_frame(&input, mock_time_ms);
  if(!lua_sandbox_run_event("on_input", &budget, error, sizeof(error)))
  {
    fprintf(stderr, "Default Lua moving recoil frame failed: %s\n", error);
    lua_sandbox_shutdown();
    return 0;
  }
  output = input;
  lua_sandbox_apply(&output);
  if(output.buttons != BUTTON_A || output.rx != input.rx || output.ry != -655)
  {
    lua_sandbox_shutdown();
    return 0;
  }

  input.rx = 0;
  mock_device_buttons = 1U << LUA_DEVICE_BTN_DOWN;
  mock_time_ms = 453U;
  lua_sandbox_begin_frame(&input, mock_time_ms);
  if(!lua_sandbox_run_event("on_input", &budget, error, sizeof(error)))
  {
    lua_sandbox_shutdown();
    return 0;
  }
  mock_device_buttons = 0;
  mock_time_ms = 454U;
  lua_sandbox_begin_frame(&input, mock_time_ms);
  if(!lua_sandbox_run_event("on_input", &budget, error, sizeof(error)))
  {
    lua_sandbox_shutdown();
    return 0;
  }
  mock_device_buttons = 1U << LUA_DEVICE_BTN_BACK;
  mock_time_ms = 455U;
  lua_sandbox_begin_frame(&input, mock_time_ms);
  if(!lua_sandbox_run_event("on_input", &budget, error, sizeof(error)))
  {
    lua_sandbox_shutdown();
    return 0;
  }
  output = input;
  lua_sandbox_apply(&output);
  if(output.ry != 0)
  {
    fprintf(stderr, "Default Lua secondary inherited primary recoil\n");
    lua_sandbox_shutdown();
    return 0;
  }

  mock_device_buttons = 0;
  mock_time_ms = 456U;
  lua_sandbox_begin_frame(&input, mock_time_ms);
  if(!lua_sandbox_run_event("on_input", &budget, error, sizeof(error)))
  {
    lua_sandbox_shutdown();
    return 0;
  }
  mock_device_buttons = 1U << LUA_DEVICE_BTN_BACK;
  mock_time_ms = 457U;
  lua_sandbox_begin_frame(&input, mock_time_ms);
  if(!lua_sandbox_run_event("on_input", &budget, error, sizeof(error)))
  {
    lua_sandbox_shutdown();
    return 0;
  }
  output = input;
  lua_sandbox_apply(&output);
  if(output.buttons != BUTTON_A || output.ry != -1310)
  {
    lua_sandbox_shutdown();
    return 0;
  }

  input.lt = 0;
  input.rt = 0;
  if(!run_default_frame(&input, &budget, error, sizeof(error), 0, 458U))
  {
    lua_sandbox_shutdown();
    return 0;
  }
  for(step = 0; step < 10U; step++)
  {
    uint8_t buttons = (step & 1U) == 0 ?
      1U << LUA_DEVICE_BTN_UP : 0;
    if(!run_default_frame(&input, &budget, error, sizeof(error), buttons,
                          459U + (uint32_t)step))
    {
      lua_sandbox_shutdown();
      return 0;
    }
  }
  if(!run_default_frame(&input, &budget, error, sizeof(error),
                        1U << LUA_DEVICE_BTN_BACK, 471U) ||
     strcmp(mock_display_lines[3], "> ON <") != 0)
  {
    fprintf(stderr, "Default Lua did not enable primary rapid fire\n");
    lua_sandbox_shutdown();
    return 0;
  }
  if(!run_default_frame(&input, &budget, error, sizeof(error), 0, 472U))
  {
    lua_sandbox_shutdown();
    return 0;
  }
  for(step = 0; step < 10U; step++)
  {
    uint8_t buttons = (step & 1U) == 0 ?
      1U << LUA_DEVICE_BTN_DOWN : 0;
    if(!run_default_frame(&input, &budget, error, sizeof(error), buttons,
                          473U + (uint32_t)step))
    {
      lua_sandbox_shutdown();
      return 0;
    }
  }
  if(!run_default_frame(&input, &budget, error, sizeof(error),
                        1U << LUA_DEVICE_BTN_BACK, 485U))
  {
    lua_sandbox_shutdown();
    return 0;
  }
  if(!run_default_frame(&input, &budget, error, sizeof(error), 0, 486U))
  {
    lua_sandbox_shutdown();
    return 0;
  }
  for(step = 0; step < 10U; step++)
  {
    uint8_t buttons = (step & 1U) == 0 ?
      1U << LUA_DEVICE_BTN_UP : 0;
    if(!run_default_frame(&input, &budget, error, sizeof(error), buttons,
                          487U + (uint32_t)step))
    {
      lua_sandbox_shutdown();
      return 0;
    }
  }
  if(strcmp(mock_display_lines[3], "> OFF <") != 0 ||
     !run_default_frame(&input, &budget, error, sizeof(error),
                        (1U << LUA_DEVICE_BTN_UP) |
                        (1U << LUA_DEVICE_BTN_DOWN), 500U) ||
     strncmp(mock_display_lines[0], "SLEDGE ", 7U) != 0)
  {
    fprintf(stderr, "Default Lua UP+DOWN did not save and exit settings\n");
    lua_sandbox_shutdown();
    return 0;
  }
  input.buttons = BUTTON_A | BUTTON_Y;
  if(!run_default_frame(&input, &budget, error, sizeof(error), 0, 510U) ||
      strcmp(mock_display_lines[0], "SLEDGE SECONDARY") != 0 ||
      mock_led_state != 0)
  {
    fprintf(stderr, "Default Lua switched weapon on Y press\n");
    lua_sandbox_shutdown();
    return 0;
  }
  input.buttons = BUTTON_A;
  if(!run_default_frame(&input, &budget, error, sizeof(error), 0, 610U) ||
      strcmp(mock_display_lines[0], "SLEDGE PRIMARY") != 0 ||
      mock_led_state == 0)
  {
    fprintf(stderr, "Default Lua did not switch weapon after short Y tap\n");
    lua_sandbox_shutdown();
    return 0;
  }
  input.buttons = BUTTON_A | BUTTON_Y;
  if(!run_default_frame(&input, &budget, error, sizeof(error), 0, 620U) ||
     !run_default_frame(&input, &budget, error, sizeof(error), 0, 1120U))
  {
    lua_sandbox_shutdown();
    return 0;
  }
  input.buttons = BUTTON_A;
  if(!run_default_frame(&input, &budget, error, sizeof(error), 0, 1220U) ||
      strcmp(mock_display_lines[0], "SLEDGE PRIMARY") != 0 ||
      mock_led_state == 0)
  {
    fprintf(stderr, "Default Lua switched weapon after held Y scan\n");
    lua_sandbox_shutdown();
    return 0;
  }
  input.buttons = BUTTON_A | BUTTON_Y;
  if(!run_default_frame(&input, &budget, error, sizeof(error), 0, 1230U))
  {
    lua_sandbox_shutdown();
    return 0;
  }
  input.buttons = BUTTON_A;
  if(!run_default_frame(&input, &budget, error, sizeof(error), 0, 1330U) ||
      strcmp(mock_display_lines[0], "SLEDGE SECONDARY") != 0 ||
      mock_led_state != 0)
  {
    fprintf(stderr, "Default Lua did not restore weapon after final Y tap\n");
    lua_sandbox_shutdown();
    return 0;
  }
  if(!run_default_frame(&input, &budget, error, sizeof(error),
                        1U << LUA_DEVICE_BTN_UP, 1340U) ||
     !run_default_frame(&input, &budget, error, sizeof(error), 0, 1341U))
  {
    lua_sandbox_shutdown();
    return 0;
  }
  for(step = 0; step < 18U; step++)
  {
    uint8_t buttons = (step & 1U) == 0 ?
      1U << LUA_DEVICE_BTN_UP : 0;
    if(!run_default_frame(&input, &budget, error, sizeof(error), buttons,
                          1350U + (uint32_t)step))
    {
      lua_sandbox_shutdown();
      return 0;
    }
  }
  if(!run_default_frame(&input, &budget, error, sizeof(error),
                        1U << LUA_DEVICE_BTN_BACK, 1370U) ||
     strcmp(mock_display_lines[3], "> ON <") != 0 ||
     !run_default_frame(&input, &budget, error, sizeof(error), 0, 1371U) ||
     !run_default_frame(&input, &budget, error, sizeof(error),
                        (1U << LUA_DEVICE_BTN_UP) |
                        (1U << LUA_DEVICE_BTN_DOWN), 1372U))
  {
    fprintf(stderr, "Default Lua did not enable tea bag\n");
    lua_sandbox_shutdown();
    return 0;
  }

  input.buttons = BUTTON_A | BUTTON_DPAD_DOWN;
  if(!run_default_frame(&input, &budget, error, sizeof(error), 0, 1500U))
  {
    lua_sandbox_shutdown();
    return 0;
  }
  output = input;
  lua_sandbox_apply(&output);
  if((output.buttons & BUTTON_DPAD_DOWN) != 0)
  {
    fprintf(stderr, "Default Lua forwarded DOWN before tap completed\n");
    lua_sandbox_shutdown();
    return 0;
  }
  input.buttons = BUTTON_A;
  if(!run_default_frame(&input, &budget, error, sizeof(error), 0, 1600U))
  {
    lua_sandbox_shutdown();
    return 0;
  }
  output = input;
  lua_sandbox_apply(&output);
  if((output.buttons & BUTTON_DPAD_DOWN) == 0)
  {
    fprintf(stderr, "Default Lua did not emit DOWN after short tap\n");
    lua_sandbox_shutdown();
    return 0;
  }
  if(!run_default_frame(&input, &budget, error, sizeof(error), 0, 1610U))
  {
    lua_sandbox_shutdown();
    return 0;
  }
  output = input;
  lua_sandbox_apply(&output);
  if((output.buttons & BUTTON_DPAD_DOWN) != 0)
  {
    fprintf(stderr, "Default Lua did not release synthetic DOWN tap\n");
    lua_sandbox_shutdown();
    return 0;
  }

  input.buttons = BUTTON_A | BUTTON_DPAD_DOWN;
  if(!run_default_frame(&input, &budget, error, sizeof(error), 0, 1700U) ||
     !run_default_frame(&input, &budget, error, sizeof(error), 0, 1950U) ||
     !run_default_frame(&input, &budget, error, sizeof(error), 0, 1960U))
  {
    lua_sandbox_shutdown();
    return 0;
  }
  output = input;
  lua_sandbox_apply(&output);
  if((output.buttons & BUTTON_DPAD_DOWN) != 0 ||
     (output.buttons & BUTTON_B) == 0)
  {
    fprintf(stderr, "Default Lua tea-bag hold exposed DOWN or missed B\n");
    lua_sandbox_shutdown();
    return 0;
  }
  lua_sandbox_shutdown();
  return 1;
}

static uint8_t run_persistence_script(const char *script,
                                      uint16_t expected_buttons,
                                      uint8_t commit)
{
  static const lua_sandbox_budget_t budget = {4000U, 1U, 100U};
  controller_data_t input;
  controller_data_t output;
  char error[128];

  memset(&input, 0, sizeof(input));
  output = input;
  if(!lua_arena_init(&test_arena, test_memory, sizeof(test_memory)) ||
     !lua_sandbox_init(lua_arena_alloc, &test_arena, &test_port) ||
     !lua_sandbox_load(script, strlen(script), error, sizeof(error)))
  {
    return 0;
  }
  lua_sandbox_begin_frame(&input, 1U);
  if(!lua_sandbox_run_event("on_input", &budget, error, sizeof(error)))
  {
    lua_sandbox_shutdown();
    return 0;
  }
  if(commit)
  {
    lua_sandbox_storage_task();
  }
  lua_sandbox_apply(&output);
  lua_sandbox_shutdown();
  return output.buttons == expected_buttons;
}

static uint8_t test_loadout_persistence(void)
{
  const char *save_first =
    "function on_input() "
    "storage.write_loadout(2,36,2,42,-7,81,13,true) "
    "storage.commit() end";
  const char *save_second =
    "function on_input() "
    "local v=storage.read_loadout(2,36,2) "
    "if v==42 then storage.write_loadout(2,36,2,43,-6,82,14,false) "
    "storage.commit() end end";
  const char *verify_first =
    "function on_input() "
    "local v,h,m,d,r=storage.read_loadout(2,36,2) "
    "if v==42 and h==-7 and m==81 and d==13 and r then "
    "controller.set_val(controller.A,100) end end";
  const char *verify_second =
    "function on_input() "
    "local v,h,m,d,r=storage.read_loadout(2,36,2) "
    "if v==43 and h==-6 and m==82 and d==14 and not r then "
    "controller.set_val(controller.A,100) end end";

  memset(mock_flash, 0xFF, sizeof(mock_flash));
    mock_flash_erase_count = 0;
    mock_flash_program_count = 0;
    if(!run_persistence_script(save_first, 0, 1) ||
      mock_flash_erase_count != 1 || mock_flash_program_count != 1 ||
      !run_persistence_script(save_first, 0, 1) ||
      mock_flash_erase_count != 1 || mock_flash_program_count != 1 ||
     !run_persistence_script(verify_first, BUTTON_A, 0) ||
     !run_persistence_script(save_second, 0, 1) ||
     !run_persistence_script(verify_second, BUTTON_A, 0))
  {
    return 0;
  }
  mock_flash[1][0] ^= 1U;
  return run_persistence_script(verify_first, BUTTON_A, 0);
}

static uint8_t test_config_blob_round_trip(void)
{
  static uint8_t blob[LUA_SANDBOX_STORAGE_IMAGE_BYTES];
  const char *save_first =
    "function on_input() "
    "storage.write_loadout(1,4,2,31,-9,76,11,true) "
    "storage.commit() end";
  const char *save_second =
    "function on_input() "
    "storage.write_loadout(1,4,2,2,3,40,20,false) "
    "storage.commit() end";
  const char *verify_first =
    "function on_input() "
    "local v,h,m,d,r=storage.read_loadout(1,4,2) "
    "if v==31 and h==-9 and m==76 and d==11 and r then "
    "controller.set_val(controller.A,100) end end";
  size_t length = 0;

  memset(mock_flash, 0xFF, sizeof(mock_flash));
  if(!run_persistence_script(save_first, 0, 1) ||
     !lua_storage_export(blob, sizeof(blob), &length) ||
     length != sizeof(blob) ||
     !run_persistence_script(save_second, 0, 1))
  {
    return 0;
  }
  blob[100] ^= 1U;
  if(lua_storage_import(blob, sizeof(blob)))
  {
    return 0;
  }
  blob[100] ^= 1U;
  if(!lua_storage_import(blob, sizeof(blob)) ||
     !lua_storage_commit_pending())
  {
    return 0;
  }
  lua_storage_task();
  return !lua_storage_commit_pending() &&
    run_persistence_script(verify_first, BUTTON_A, 0);
}

static uint8_t test_legacy_config_import(void)
{
  static uint8_t blob[LUA_SANDBOX_STORAGE_IMAGE_BYTES];
  test_storage_legacy_image_t legacy;
  const char *verify_v4 =
    "function on_input() "
    "local v,h,m,d,r,f,p=storage.read_loadout(1,1,1) "
    "if v==27 and h==-8 and m==74 and d==14 and not r and f and p==77 "
    "then controller.set_val(controller.A,100) end end";
  const char *verify_v3 =
    "function on_input() "
    "local v,h,m,d,r,f,p=storage.read_loadout(1,1,1) "
    "if v==28 and h==-7 and m==75 and d==15 and r and f and p==88 "
    "then controller.set_val(controller.A,100) end end";

  if(sizeof(legacy) > sizeof(blob))
  {
    return 0;
  }
  memset(mock_flash, 0xFF, sizeof(mock_flash));
  if(!run_persistence_script("function on_input() end", 0, 0))
  {
    return 0;
  }
  memset(&legacy, 0, sizeof(legacy));
  legacy.magic = TEST_STORAGE_MAGIC;
  legacy.version = 4U;
  legacy.generation = 7U;
  legacy.loadouts[0].vertical = 27;
  legacy.loadouts[0].horizontal = -8;
  legacy.loadouts[0].movement = 74U;
  legacy.loadouts[0].deadzone = 14U;
  legacy.loadouts[0].valid = 0x03U;
  legacy.loadouts[0].rapid_fire_percent = 77U;
  legacy.crc32 = test_crc32(&legacy, sizeof(legacy));
  memset(blob, 0xFF, sizeof(blob));
  memcpy(blob, &legacy, sizeof(legacy));
  if(!lua_storage_import(blob, sizeof(blob)) ||
     !lua_storage_commit_pending())
  {
    return 0;
  }
  lua_storage_task();
  if(lua_storage_commit_pending() ||
     !run_persistence_script(verify_v4, BUTTON_A, 0))
  {
    return 0;
  }

  legacy.version = 3U;
  legacy.generation = 8U;
  legacy.loadouts[0].vertical = 28;
  legacy.loadouts[0].horizontal = -7;
  legacy.loadouts[0].movement = 75U;
  legacy.loadouts[0].deadzone = 15U;
  legacy.loadouts[0].rapid_fire_percent = 0x80U | 88U;
  legacy.crc32 = 0;
  legacy.crc32 = test_crc32(&legacy, sizeof(legacy));
  memset(blob, 0xFF, sizeof(blob));
  memcpy(blob, &legacy, sizeof(legacy));
  if(!lua_storage_import(blob, sizeof(blob)) ||
     !lua_storage_commit_pending())
  {
    return 0;
  }
  lua_storage_task();
  return !lua_storage_commit_pending() &&
    run_persistence_script(verify_v3, BUTTON_A, 0);
}

int main(int argc, char *argv[])
{
  const char *no_op = "function on_input()\nend\n";
  const char *set_a =
    "function on_input() "
    "controller.set_val(controller.A, 100) end";
  const char *device_up_sets_a =
    "function on_input() if device.get_val(device.BTN_UP) == 100 then "
    "controller.set_val(controller.A, 100) end end";

  if(argc == 2 && strcmp(argv[1], "storage-import") == 0)
  {
    if(!test_legacy_config_import())
    {
      return 1;
    }
    puts("Legacy configuration import tests: PASS");
    return 0;
  }
  if(argc == 2 && strcmp(argv[1], "bf6") == 0)
  {
    memset(mock_flash, 0xFF, sizeof(mock_flash));
    if(!test_bf6_script())
    {
      return 1;
    }
    puts("BF6 script test: PASS");
    return 0;
  }

  mock_device_buttons = 1U << LUA_DEVICE_BTN_UP;
  memset(mock_flash, 0xFF, sizeof(mock_flash));
  if(!run_script(no_op, 0) || !run_script(device_up_sets_a, BUTTON_A))
  {
    return 1;
  }
  mock_device_buttons = 0;
  if(!run_script(set_a, BUTTON_A) || !test_compact_macro() ||
      !test_cancel_macros() || !test_named_macros() ||
      !test_default_script() || !test_loadout_persistence() ||
      !test_config_blob_round_trip() || !test_legacy_config_import())
  {
    return 1;
  }
  puts("Lua runtime, loadout persistence, and recovery tests: PASS");
  return 0;
}