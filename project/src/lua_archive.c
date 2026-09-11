#include "lua_archive.h"

#include <stdint.h>

#define LUA_ARCHIVE_VERSION  1U
#define LUA_ARCHIVE_CRC_OFFSET 20U

extern const uint8_t __lua_archive_start[];

static uint16_t read_u16(const uint8_t *bytes)
{
  return (uint16_t)bytes[0] | (uint16_t)((uint16_t)bytes[1] << 8);
}

static uint32_t read_u32(const uint8_t *bytes)
{
  return (uint32_t)bytes[0] |
         ((uint32_t)bytes[1] << 8) |
         ((uint32_t)bytes[2] << 16) |
         ((uint32_t)bytes[3] << 24);
}

static uint32_t crc32(const uint8_t *bytes, size_t length,
                      uint8_t clear_archive_crc)
{
  uint32_t crc = 0xFFFFFFFFUL;
  size_t index;
  uint8_t bit;
  for(index = 0; index < length; index++)
  {
    uint8_t value = bytes[index];
    if(clear_archive_crc && index >= LUA_ARCHIVE_CRC_OFFSET &&
       index < LUA_ARCHIVE_CRC_OFFSET + sizeof(uint32_t))
    {
      value = 0;
    }
    crc ^= value;
    for(bit = 0; bit < 8; bit++)
    {
      crc = (crc >> 1) ^ (0xEDB88320UL &
            (uint32_t)-(int32_t)(crc & 1U));
    }
  }
  return ~crc;
}

uint8_t lua_archive_open(const char **script, size_t *script_length)
{
  const uint8_t *archive = __lua_archive_start;
  uint32_t total_length;
  uint32_t source_length;
  uint32_t expected_source_crc;
  uint32_t expected_archive_crc;

  if(script == 0 || script_length == 0 ||
     archive[0] != 'S' || archive[1] != 'L' ||
     archive[2] != 'U' || archive[3] != 'A' ||
     read_u16(archive + 4) != LUA_ARCHIVE_VERSION ||
     read_u16(archive + 6) != LUA_ARCHIVE_HEADER_SIZE ||
     read_u32(archive + 24) != 0 || read_u32(archive + 28) != 0)
  {
    return 0;
  }

  total_length = read_u32(archive + 8);
  source_length = read_u32(archive + 12);
  if(source_length == 0 || source_length > LUA_ARCHIVE_SCRIPT_CAPACITY ||
     total_length != LUA_ARCHIVE_HEADER_SIZE + source_length ||
     total_length > LUA_ARCHIVE_CAPACITY)
  {
    return 0;
  }

  expected_source_crc = read_u32(archive + 16);
  expected_archive_crc = read_u32(archive + LUA_ARCHIVE_CRC_OFFSET);
  if(crc32(archive + LUA_ARCHIVE_HEADER_SIZE, source_length, 0) !=
       expected_source_crc ||
     crc32(archive, total_length, 1) != expected_archive_crc)
  {
    return 0;
  }

  *script = (const char *)(archive + LUA_ARCHIVE_HEADER_SIZE);
  *script_length = source_length;
  return 1;
}