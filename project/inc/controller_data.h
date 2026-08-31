#ifndef CONTROLLER_DATA_H
#define CONTROLLER_DATA_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef struct
{
  uint16_t buttons;
  int16_t lx, ly, rx, ry;
  uint16_t lt, rt;
} controller_data_t;

#ifdef __cplusplus
}
#endif

#endif