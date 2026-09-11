#ifndef LUA_RUNTIME_H
#define LUA_RUNTIME_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "controller_data.h"

uint8_t lua_runtime_init(void);
uint8_t lua_runtime_active(void);
void lua_runtime_console_connected(void);
void lua_runtime_task(const controller_data_t *input,
                      uint32_t current_time_ms);
void lua_runtime_process_input(const controller_data_t *input,
                               controller_data_t *output,
                               uint32_t current_time_ms);
uint8_t lua_runtime_apply_pending(controller_data_t *output,
                                  uint32_t current_time_ms);

#ifdef __cplusplus
}
#endif

#endif