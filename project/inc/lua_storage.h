#ifndef LUA_STORAGE_H
#define LUA_STORAGE_H

#include <stddef.h>
#include <stdint.h>

uint8_t lua_storage_export(void *data, size_t capacity, size_t *length);
uint8_t lua_storage_import(const void *data, size_t length);
uint8_t lua_storage_commit_pending(void);
void lua_storage_task(void);

#endif