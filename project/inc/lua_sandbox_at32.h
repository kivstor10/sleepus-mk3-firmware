#ifndef LUA_SANDBOX_AT32_H
#define LUA_SANDBOX_AT32_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "lua_sandbox.h"

const lua_sandbox_port_t *lua_sandbox_at32_port(void);
uint8_t lua_board_flash_commit_allowed(void);

#ifdef __cplusplus
}
#endif

#endif