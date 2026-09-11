#ifndef LUA_ARCHIVE_H
#define LUA_ARCHIVE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

#define LUA_ARCHIVE_CAPACITY       (248U * 1024U)
#define LUA_ARCHIVE_HEADER_SIZE    32U
#define LUA_ARCHIVE_SCRIPT_CAPACITY \
  (LUA_ARCHIVE_CAPACITY - LUA_ARCHIVE_HEADER_SIZE)

uint8_t lua_archive_open(const char **script, size_t *script_length);

#ifdef __cplusplus
}
#endif

#endif