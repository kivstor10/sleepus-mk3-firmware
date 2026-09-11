#include "lua_sandbox_at32.h"

#include <string.h>
#include "at32f435_437_conf.h"
#include "hardware.h"
#include "wk_system.h"

#define AT32_FLASH_SECTOR_BYTES 2048U

extern const uint8_t __lua_storage_slot_a[];
extern const uint8_t __lua_storage_slot_b[];
extern const uint8_t __lua_storage_slot_size[];

static uint32_t slot_address(uint8_t slot)
{
  return slot == 0 ? (uint32_t)__lua_storage_slot_a :
                     (uint32_t)__lua_storage_slot_b;
}

static uint32_t storage_slot_size(void)
{
  return (uint32_t)__lua_storage_slot_size;
}

static uint8_t at32_flash_read(uint8_t slot, uint32_t offset, void *data,
                               uint32_t length)
{
  if(slot > 1 || data == 0 || offset > storage_slot_size() ||
     length > storage_slot_size() - offset)
  {
    return 0;
  }
  memcpy(data, (const void *)(slot_address(slot) + offset), length);
  return 1;
}

static uint8_t at32_flash_erase(uint8_t slot)
{
  uint32_t offset;
  flash_status_type status = FLASH_OPERATE_DONE;
  if(slot > 1)
  {
    return 0;
  }
  flash_unlock();
  for(offset = 0; offset < storage_slot_size();
      offset += AT32_FLASH_SECTOR_BYTES)
  {
    status = flash_sector_erase(slot_address(slot) + offset);
    if(status != FLASH_OPERATE_DONE)
    {
      break;
    }
  }
  flash_lock();
  return status == FLASH_OPERATE_DONE;
}

static uint8_t at32_flash_program(uint8_t slot, uint32_t offset,
                                  const void *data, uint32_t length)
{
  const uint32_t *words = (const uint32_t *)data;
  uint32_t address;
  uint32_t index;
  flash_status_type status = FLASH_OPERATE_DONE;
  if(slot > 1 || data == 0 || (offset & 3U) != 0 || (length & 3U) != 0 ||
     offset > storage_slot_size() || length > storage_slot_size() - offset)
  {
    return 0;
  }
  address = slot_address(slot) + offset;
  flash_unlock();
  for(index = 0; index < length / sizeof(uint32_t); index++)
  {
    status = flash_word_program(address + index * sizeof(uint32_t),
                                words[index]);
    if(status != FLASH_OPERATE_DONE)
    {
      break;
    }
  }
  flash_lock();
  return status == FLASH_OPERATE_DONE &&
         memcmp((const void *)address, data, length) == 0;
}

__attribute__((weak)) uint8_t lua_board_flash_commit_allowed(void)
{
  return 1;
}

static uint8_t at32_read_device_buttons(void)
{
  uint8_t hardware_buttons = read_menu_buttons();
  uint8_t lua_buttons = 0;
  if((hardware_buttons & MENU_BUTTON_UP) != 0)
  {
    lua_buttons |= 1U << LUA_DEVICE_BTN_UP;
  }
  if((hardware_buttons & MENU_BUTTON_DOWN) != 0)
  {
    lua_buttons |= 1U << LUA_DEVICE_BTN_DOWN;
  }
  if((hardware_buttons & MENU_BUTTON_SELECT) != 0)
  {
    lua_buttons |= 1U << LUA_DEVICE_BTN_SELECT;
  }
  if((hardware_buttons & MENU_BUTTON_BACK) != 0)
  {
    lua_buttons |= 1U << LUA_DEVICE_BTN_BACK;
  }
  return lua_buttons;
}

static const lua_sandbox_port_t at32_port =
{
  get_system_tick,
  at32_read_device_buttons,
  oled_clear,
  oled_draw_text_at,
  oled_draw_pixel,
  set_status_led,
  at32_flash_read,
  at32_flash_erase,
  at32_flash_program,
  lua_board_flash_commit_allowed
};

const lua_sandbox_port_t *lua_sandbox_at32_port(void)
{
  return &at32_port;
}