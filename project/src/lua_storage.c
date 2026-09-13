#include "lua_sandbox.h"
#include "lua_storage.h"

#include <string.h>
#include "lauxlib.h"

#define STORAGE_MAGIC          0x53505652UL
#define STORAGE_VERSION        5U
#define STORAGE_LEGACY_VERSION 4U
#define STORAGE_OLDER_VERSION  3U
#define STORAGE_KEY_BYTES      32U
#define STORAGE_STRING_BYTES   64U
#define STORAGE_SIDE_COUNT      2U
#define STORAGE_OPERATOR_COUNT 39U
#define STORAGE_LEGACY_OPERATOR_COUNT 36U
#define STORAGE_LEGACY_ENTRY_COUNT 32U
#define STORAGE_WEAPON_COUNT    2U
#define STORAGE_LOADOUT_COUNT  (STORAGE_SIDE_COUNT * STORAGE_OPERATOR_COUNT * \
                                STORAGE_WEAPON_COUNT)
#define STORAGE_LEGACY_LOADOUT_COUNT \
  (STORAGE_SIDE_COUNT * STORAGE_LEGACY_OPERATOR_COUNT * STORAGE_WEAPON_COUNT)
#define LOADOUT_VALID_MASK      0x01U
#define LOADOUT_FIRST_BULLET    0x02U
#define LOADOUT_RAPID_FIRE      0x04U
#define LOADOUT_LEGACY_RAPID_FIRE 0x80U
#define LOADOUT_PERCENT_MASK    0x7FU

typedef enum
{
  STORAGE_VALUE_EMPTY = 0,
  STORAGE_VALUE_BOOLEAN,
  STORAGE_VALUE_INTEGER,
  STORAGE_VALUE_FLOAT,
  STORAGE_VALUE_STRING
} storage_value_type_t;

typedef struct
{
  char key[STORAGE_KEY_BYTES];
  uint8_t type;
  uint8_t reserved;
  uint16_t value_length;
  union
  {
    uint8_t boolean_value;
    int32_t integer_value;
    float float_value;
    char string_value[STORAGE_STRING_BYTES];
  } value;
} storage_entry_t;

typedef struct
{
  int8_t vertical;
  int8_t horizontal;
  uint8_t movement;
  uint8_t deadzone;
  uint8_t valid;
  uint8_t rapid_fire_percent;
} storage_loadout_t;

typedef struct
{
  uint32_t magic;
  uint16_t version;
  uint16_t entry_count;
  uint32_t generation;
  uint32_t crc32;
  storage_loadout_t loadouts[STORAGE_LOADOUT_COUNT];
  storage_entry_t entries[LUA_SANDBOX_MAX_STORAGE_ENTRIES];
} storage_image_t;

typedef struct
{
  uint32_t magic;
  uint16_t version;
  uint16_t entry_count;
  uint32_t generation;
  uint32_t crc32;
  storage_loadout_t loadouts[STORAGE_LEGACY_LOADOUT_COUNT];
  storage_entry_t entries[STORAGE_LEGACY_ENTRY_COUNT];
} storage_legacy_image_t;

typedef char storage_image_must_fit[
  sizeof(storage_image_t) <= LUA_SANDBOX_STORAGE_IMAGE_BYTES ? 1 : -1];

typedef union
{
  uint32_t alignment;
  uint8_t bytes[LUA_SANDBOX_STORAGE_IMAGE_BYTES];
} aligned_flash_buffer_t;

typedef struct
{
  lua_sandbox_port_t port;
  storage_image_t image;
  uint8_t active_slot;
  uint8_t loaded;
  uint8_t dirty;
  uint8_t commit_requested;
} storage_context_t;

static storage_context_t storage;
static aligned_flash_buffer_t flash_buffer;
static storage_image_t storage_scratch[2];
static storage_legacy_image_t legacy_storage_scratch[2];

static uint32_t crc32_calculate(const void *data, size_t length)
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

static uint8_t image_valid_version(storage_image_t *image, uint16_t version)
{
  uint32_t expected;
  uint32_t actual;
  uint16_t index;
  if(image->magic != STORAGE_MAGIC || image->version != version ||
     image->entry_count > LUA_SANDBOX_MAX_STORAGE_ENTRIES)
  {
    return 0;
  }
  expected = image->crc32;
  image->crc32 = 0;
  actual = crc32_calculate(image, sizeof(*image));
  image->crc32 = expected;
  if(actual != expected)
  {
    return 0;
  }
  for(index = 0; index < STORAGE_LOADOUT_COUNT; index++)
  {
    const storage_loadout_t *loadout = &image->loadouts[index];
     if((loadout->valid & ~(LOADOUT_VALID_MASK | LOADOUT_FIRST_BULLET |
                             (version == STORAGE_VERSION ? LOADOUT_RAPID_FIRE : 0U))) != 0U ||
       (version == STORAGE_LEGACY_VERSION &&
        (loadout->rapid_fire_percent & ~(LOADOUT_LEGACY_RAPID_FIRE |
                                         LOADOUT_PERCENT_MASK)) != 0U) ||
       ((loadout->valid & LOADOUT_VALID_MASK) != 0U &&
        (loadout->vertical < 0 || loadout->vertical > 99 ||
         loadout->horizontal < -30 || loadout->horizontal > 30 ||
         loadout->movement > 99 || loadout->deadzone < 1 ||
        loadout->deadzone > 30 ||
        (version == STORAGE_LEGACY_VERSION ?
          (loadout->rapid_fire_percent & LOADOUT_PERCENT_MASK) > 100 :
          loadout->rapid_fire_percent > 200))))
    {
      return 0;
    }
  }
  for(index = 0; index < image->entry_count; index++)
  {
    const storage_entry_t *entry = &image->entries[index];
    if(entry->key[0] == '\0' ||
       memchr(entry->key, '\0', sizeof(entry->key)) == 0 ||
       entry->type < STORAGE_VALUE_BOOLEAN ||
       entry->type > STORAGE_VALUE_STRING ||
       (entry->type == STORAGE_VALUE_BOOLEAN &&
        entry->value.boolean_value > 1U) ||
       (entry->type == STORAGE_VALUE_STRING &&
        entry->value_length >= STORAGE_STRING_BYTES))
    {
      return 0;
    }
  }
  return 1;
}

static uint8_t image_valid(storage_image_t *image)
{
  return image_valid_version(image, STORAGE_VERSION);
}

static uint8_t legacy_image_valid(storage_legacy_image_t *image,
                                  uint16_t version)
{
  uint32_t expected;
  uint32_t actual;
  if(image->magic != STORAGE_MAGIC || image->version != version ||
     image->entry_count > LUA_SANDBOX_MAX_STORAGE_ENTRIES)
  {
    return 0;
  }
  expected = image->crc32;
  image->crc32 = 0;
  actual = crc32_calculate(image, sizeof(*image));
  image->crc32 = expected;
  return actual == expected;
}

static uint8_t read_slot(uint8_t slot, storage_image_t *image)
{
  if(storage.port.flash_read == 0 ||
     !storage.port.flash_read(slot, 0, flash_buffer.bytes,
                              LUA_SANDBOX_STORAGE_IMAGE_BYTES))
  {
    return 0;
  }
  memcpy(image, flash_buffer.bytes, sizeof(*image));
  return image_valid(image);
}

static uint8_t read_legacy_slot(uint8_t slot, storage_legacy_image_t *image,
                                uint16_t version)
{
  if(storage.port.flash_read == 0 ||
     !storage.port.flash_read(slot, 0, flash_buffer.bytes,
                              LUA_SANDBOX_STORAGE_IMAGE_BYTES))
  {
    return 0;
  }
  memcpy(image, flash_buffer.bytes, sizeof(*image));
  return legacy_image_valid(image, version);
}

static storage_entry_t *find_entry(const char *key)
{
  uint16_t index;
  for(index = 0; index < storage.image.entry_count; index++)
  {
    if(strcmp(storage.image.entries[index].key, key) == 0)
    {
      return &storage.image.entries[index];
    }
  }
  return 0;
}

static void remove_entry(storage_entry_t *entry)
{
  uint16_t index = (uint16_t)(entry - storage.image.entries);
  if(index + 1U < storage.image.entry_count)
  {
    memmove(&storage.image.entries[index], &storage.image.entries[index + 1U],
            (storage.image.entry_count - index - 1U) * sizeof(*entry));
  }
  storage.image.entry_count--;
  memset(&storage.image.entries[storage.image.entry_count], 0, sizeof(*entry));
  storage.dirty = 1;
}

static storage_entry_t *create_entry(const char *key)
{
  storage_entry_t *entry;
  if(storage.image.entry_count >= LUA_SANDBOX_MAX_STORAGE_ENTRIES)
  {
    return 0;
  }
  entry = &storage.image.entries[storage.image.entry_count++];
  memset(entry, 0, sizeof(*entry));
  strncpy(entry->key, key, sizeof(entry->key) - 1U);
  return entry;
}

static int binding_storage_read(lua_State *state)
{
  size_t key_length;
  const char *key = luaL_checklstring(state, 1, &key_length);
  storage_entry_t *entry;
  luaL_argcheck(state, key_length > 0 && key_length < STORAGE_KEY_BYTES, 1,
                "key must contain 1..31 bytes");
  luaL_argcheck(state, memchr(key, '\0', key_length) == 0, 1,
                "key cannot contain NUL bytes");
  entry = find_entry(key);
  if(entry == 0)
  {
    lua_pushnil(state);
    return 1;
  }
  switch(entry->type)
  {
    case STORAGE_VALUE_BOOLEAN:
      lua_pushboolean(state, entry->value.boolean_value);
      break;
    case STORAGE_VALUE_INTEGER:
      lua_pushinteger(state, entry->value.integer_value);
      break;
    case STORAGE_VALUE_FLOAT:
      lua_pushnumber(state, entry->value.float_value);
      break;
    case STORAGE_VALUE_STRING:
      lua_pushlstring(state, entry->value.string_value, entry->value_length);
      break;
    default:
      lua_pushnil(state);
      break;
  }
  return 1;
}

static int binding_storage_write(lua_State *state)
{
  size_t key_length;
  const char *key = luaL_checklstring(state, 1, &key_length);
  storage_entry_t *entry;
  int value_type;
  luaL_argcheck(state, key_length > 0 && key_length < STORAGE_KEY_BYTES, 1,
                "key must contain 1..31 bytes");
  luaL_argcheck(state, memchr(key, '\0', key_length) == 0, 1,
                "key cannot contain NUL bytes");
  entry = find_entry(key);
  value_type = lua_type(state, 2);
  if(value_type == LUA_TNIL)
  {
    if(entry != 0)
    {
      remove_entry(entry);
    }
    lua_pushboolean(state, 1);
    return 1;
  }
  luaL_argcheck(state, value_type == LUA_TBOOLEAN ||
                value_type == LUA_TNUMBER || value_type == LUA_TSTRING, 2,
                "value must be nil, boolean, number, or string");
  if(value_type == LUA_TSTRING)
  {
    size_t value_length;
    lua_tolstring(state, 2, &value_length);
    luaL_argcheck(state, value_length < STORAGE_STRING_BYTES, 2,
                  "string must contain at most 63 bytes");
  }
  if(entry == 0)
  {
    entry = create_entry(key);
    if(entry == 0)
    {
      lua_pushboolean(state, 0);
      lua_pushliteral(state, "storage is full");
      return 2;
    }
  }
  memset(&entry->value, 0, sizeof(entry->value));
  entry->value_length = 0;
  if(value_type == LUA_TBOOLEAN)
  {
    entry->type = STORAGE_VALUE_BOOLEAN;
    entry->value.boolean_value = (uint8_t)lua_toboolean(state, 2);
  }
  else if(value_type == LUA_TNUMBER && lua_isinteger(state, 2))
  {
    lua_Integer value = lua_tointeger(state, 2);
    luaL_argcheck(state, value >= INT32_MIN && value <= INT32_MAX, 2,
                  "integer is outside 32-bit range");
    entry->type = STORAGE_VALUE_INTEGER;
    entry->value.integer_value = (int32_t)value;
  }
  else if(value_type == LUA_TNUMBER)
  {
    entry->type = STORAGE_VALUE_FLOAT;
    entry->value.float_value = (float)lua_tonumber(state, 2);
  }
  else if(value_type == LUA_TSTRING)
  {
    size_t value_length;
    const char *value = lua_tolstring(state, 2, &value_length);
    entry->type = STORAGE_VALUE_STRING;
    entry->value_length = (uint16_t)value_length;
    memcpy(entry->value.string_value, value, value_length);
  }
  storage.dirty = 1;
  lua_pushboolean(state, 1);
  return 1;
}

static uint16_t loadout_index(lua_State *state)
{
  lua_Integer side = luaL_checkinteger(state, 1);
  lua_Integer operator_index = luaL_checkinteger(state, 2);
  lua_Integer weapon = luaL_checkinteger(state, 3);
  luaL_argcheck(state, side >= 1 && side <= STORAGE_SIDE_COUNT, 1,
                "side must be 1 or 2");
  luaL_argcheck(state, operator_index >= 1 &&
                operator_index <= STORAGE_OPERATOR_COUNT, 2,
                "operator must be 1..36");
  luaL_argcheck(state, weapon >= 1 && weapon <= STORAGE_WEAPON_COUNT, 3,
                "weapon must be 1 or 2");
  return (uint16_t)((side - 1) * STORAGE_OPERATOR_COUNT *
                    STORAGE_WEAPON_COUNT +
                    (operator_index - 1) * STORAGE_WEAPON_COUNT +
                    (weapon - 1));
}

static int binding_storage_read_loadout(lua_State *state)
{
  const storage_loadout_t *loadout =
    &storage.image.loadouts[loadout_index(state)];
  if((loadout->valid & LOADOUT_VALID_MASK) == 0U)
  {
    lua_pushnil(state);
    return 1;
  }
  lua_pushinteger(state, loadout->vertical);
  lua_pushinteger(state, loadout->horizontal);
  lua_pushinteger(state, loadout->movement);
  lua_pushinteger(state, loadout->deadzone);
  lua_pushboolean(state, loadout->valid & LOADOUT_RAPID_FIRE);
  lua_pushboolean(state, loadout->valid & LOADOUT_FIRST_BULLET);
  lua_pushinteger(state, loadout->rapid_fire_percent);
  return 7;
}

static int binding_storage_write_loadout(lua_State *state)
{
  storage_loadout_t *loadout = &storage.image.loadouts[loadout_index(state)];
  lua_Integer vertical = luaL_checkinteger(state, 4);
  lua_Integer horizontal = luaL_checkinteger(state, 5);
  lua_Integer movement = luaL_checkinteger(state, 6);
  lua_Integer deadzone = luaL_checkinteger(state, 7);
  uint8_t first_bullet = (uint8_t)lua_toboolean(state, 9);
  lua_Integer first_bullet_percent = lua_isnoneornil(state, 10) ? 100 :
                                     luaL_checkinteger(state, 10);
  uint8_t rapid_fire;
  luaL_argcheck(state, vertical >= 0 && vertical <= 99, 4,
                "vertical must be 0..99");
  luaL_argcheck(state, horizontal >= -30 && horizontal <= 30, 5,
                "horizontal must be -30..30");
  luaL_argcheck(state, movement >= 0 && movement <= 99, 6,
                "movement must be 0..99");
  luaL_argcheck(state, deadzone >= 1 && deadzone <= 30, 7,
                "deadzone must be 1..30");
  luaL_argcheck(state, first_bullet_percent >= 0 &&
                first_bullet_percent <= 200, 10,
                "first bullet percent must be 0..200");
  rapid_fire = (uint8_t)lua_toboolean(state, 8);
  if((loadout->valid & LOADOUT_VALID_MASK) &&
     loadout->vertical == (int8_t)vertical &&
     loadout->horizontal == (int8_t)horizontal &&
     loadout->movement == (uint8_t)movement &&
     loadout->deadzone == (uint8_t)deadzone &&
    ((loadout->valid & LOADOUT_RAPID_FIRE) != 0U) ==
       (rapid_fire != 0U) &&
     ((loadout->valid & LOADOUT_FIRST_BULLET) != 0U) ==
       (first_bullet != 0U) &&
     loadout->rapid_fire_percent == (uint8_t)first_bullet_percent)
  {
    lua_pushboolean(state, 1);
    return 1;
  }
  loadout->vertical = (int8_t)vertical;
  loadout->horizontal = (int8_t)horizontal;
  loadout->movement = (uint8_t)movement;
  loadout->deadzone = (uint8_t)deadzone;
  loadout->rapid_fire_percent = (uint8_t)first_bullet_percent;
  loadout->valid = (uint8_t)(LOADOUT_VALID_MASK |
    (first_bullet ? LOADOUT_FIRST_BULLET : 0U) |
    (rapid_fire ? LOADOUT_RAPID_FIRE : 0U));
  storage.dirty = 1;
  lua_pushboolean(state, 1);
  return 1;
}

static int binding_storage_commit(lua_State *state)
{
  storage.commit_requested = storage.dirty;
  lua_pushboolean(state, storage.dirty);
  return 1;
}

static int binding_storage_reset_loadouts(lua_State *state)
{
  memset(storage.image.loadouts, 0, sizeof(storage.image.loadouts));
  storage.dirty = 1;
  lua_pushboolean(state, 1);
  return 1;
}

const luaL_Reg lua_storage_bindings[] =
{
  {"read", binding_storage_read},
  {"write", binding_storage_write},
  {"read_loadout", binding_storage_read_loadout},
  {"write_loadout", binding_storage_write_loadout},
  {"reset_loadouts", binding_storage_reset_loadouts},
  {"commit", binding_storage_commit},
  {0, 0}
};

void lua_storage_init(const lua_sandbox_port_t *port)
{
  uint8_t valid_a;
  uint8_t valid_b;
  uint16_t index;

  memset(&storage, 0, sizeof(storage));
  storage.port = *port;
  valid_a = read_slot(0, &storage_scratch[0]);
  valid_b = read_slot(1, &storage_scratch[1]);
  if(valid_a && (!valid_b || (int32_t)(storage_scratch[0].generation -
                                       storage_scratch[1].generation) > 0))
  {
    storage.image = storage_scratch[0];
    storage.active_slot = 0;
    storage.loaded = 1;
  }
  else if(valid_b)
  {
    storage.image = storage_scratch[1];
    storage.active_slot = 1;
    storage.loaded = 1;
  }
  else
  {
    storage_legacy_image_t *legacy;
    uint16_t legacy_version = STORAGE_LEGACY_VERSION;
    valid_a = read_legacy_slot(0, &legacy_storage_scratch[0], legacy_version);
    valid_b = read_legacy_slot(1, &legacy_storage_scratch[1], legacy_version);
    if(!valid_a && !valid_b)
    {
      legacy_version = STORAGE_OLDER_VERSION;
      valid_a = read_legacy_slot(0, &legacy_storage_scratch[0], legacy_version);
      valid_b = read_legacy_slot(1, &legacy_storage_scratch[1], legacy_version);
    }
    if(valid_a || valid_b)
    {
      legacy = valid_a && (!valid_b ||
        (int32_t)(legacy_storage_scratch[0].generation -
                  legacy_storage_scratch[1].generation) > 0) ?
        &legacy_storage_scratch[0] : &legacy_storage_scratch[1];
      memset(&storage.image, 0, sizeof(storage.image));
      storage.image.magic = legacy->magic;
      storage.image.entry_count = legacy->entry_count;
      storage.image.generation = legacy->generation;
      memcpy(storage.image.loadouts, legacy->loadouts,
             sizeof(legacy->loadouts));
            memcpy(storage.image.entries, legacy->entries,
              sizeof(storage.image.entries));
      storage.active_slot = valid_a && (!valid_b ||
        (int32_t)(legacy_storage_scratch[0].generation -
                  legacy_storage_scratch[1].generation) > 0) ? 0 : 1;
      storage.image.version = STORAGE_VERSION;
      for(index = 0; index < STORAGE_LEGACY_LOADOUT_COUNT; index++)
      {
        storage_loadout_t *loadout = &storage.image.loadouts[index];
        if(legacy_version == STORAGE_OLDER_VERSION &&
           (loadout->rapid_fire_percent & LOADOUT_LEGACY_RAPID_FIRE) != 0U)
        {
          loadout->valid |= LOADOUT_RAPID_FIRE;
        }
        loadout->rapid_fire_percent &= LOADOUT_PERCENT_MASK;
      }
      storage.dirty = 1;
      storage.commit_requested = 1;
    }
    else
    {
      memset(&storage.image, 0, sizeof(storage.image));
      storage.image.magic = STORAGE_MAGIC;
      storage.image.version = STORAGE_VERSION;
      storage.active_slot = 1;
    }
    storage.loaded = 1;
  }
}

void lua_storage_task(void)
{
  uint8_t next_slot;
  uint32_t next_generation;
  if(!storage.loaded || !storage.dirty || !storage.commit_requested ||
     storage.port.flash_erase == 0 || storage.port.flash_program == 0 ||
     storage.port.flash_commit_allowed == 0 ||
     !storage.port.flash_commit_allowed())
  {
    return;
  }
  next_slot = storage.active_slot ^ 1U;
  next_generation = storage.image.generation + 1U;
  storage.image.generation = next_generation;
  storage.image.crc32 = 0;
  storage.image.crc32 = crc32_calculate(&storage.image, sizeof(storage.image));
  memset(flash_buffer.bytes, 0xFF, sizeof(flash_buffer.bytes));
  memcpy(flash_buffer.bytes, &storage.image, sizeof(storage.image));
  if(storage.port.flash_erase(next_slot) &&
     storage.port.flash_program(next_slot, 0, flash_buffer.bytes,
                        sizeof(flash_buffer.bytes)) &&
      read_slot(next_slot, &storage_scratch[0]) &&
      storage_scratch[0].generation == next_generation)
  {
    storage.active_slot = next_slot;
    storage.dirty = 0;
    storage.commit_requested = 0;
  }
  else
  {
    storage.image.generation = next_generation - 1U;
  }
}

uint8_t lua_storage_export(void *data, size_t capacity, size_t *length)
{
  storage_image_t image;
  if(!storage.loaded || data == 0 || length == 0 ||
  capacity < LUA_SANDBOX_STORAGE_IMAGE_BYTES)
  {
    return 0;
  }
  image = storage.image;
  image.crc32 = 0;
  image.crc32 = crc32_calculate(&image, sizeof(image));
  memset(data, 0xFF, LUA_SANDBOX_STORAGE_IMAGE_BYTES);
  memcpy(data, &image, sizeof(image));
  *length = LUA_SANDBOX_STORAGE_IMAGE_BYTES;
  return 1;
}

uint8_t lua_storage_import(const void *data, size_t length)
{
  storage_image_t image;
  uint32_t generation;
    if(!storage.loaded || data == 0 ||
      length != LUA_SANDBOX_STORAGE_IMAGE_BYTES)
  {
    return 0;
  }
  memcpy(&image, data, sizeof(image));
  if(!image_valid(&image))
  {
    return 0;
  }
  generation = storage.image.generation;
  storage.image = image;
  storage.image.generation = generation;
  storage.image.crc32 = 0;
  storage.dirty = 1;
  storage.commit_requested = 1;
  return 1;
}

uint8_t lua_storage_commit_pending(void)
{
  return storage.dirty && storage.commit_requested;
}