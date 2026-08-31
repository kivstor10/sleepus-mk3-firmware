#ifndef MOD_ENGINE_H
#define MOD_ENGINE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "controller_data.h"

uint8_t mod_engine_process(controller_data_t *data, uint32_t current_time_ms);
void mod_engine_toggle_repeat_features(uint32_t current_time_ms);
uint8_t mod_engine_repeat_features_enabled(void);

#ifdef __cplusplus
}
#endif

#endif