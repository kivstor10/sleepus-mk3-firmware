#ifndef HARDWARE_H
#define HARDWARE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "at32f435_437_conf.h"

#define MENU_UP_PORT                     GPIOA
#define MENU_UP_PIN                      GPIO_PINS_0
#define MENU_SELECT_PORT                 GPIOA
#define MENU_SELECT_PIN                  GPIO_PINS_1
#define MENU_DOWN_PORT                   GPIOA
#define MENU_DOWN_PIN                    GPIO_PINS_2
#define MENU_BACK_PORT                   GPIOA
#define MENU_BACK_PIN                    GPIO_PINS_3
#define STATUS_LED_PORT                  GPIOC
#define STATUS_LED_PIN                   GPIO_PINS_15
#define OLED_I2C_PORT                    GPIOB
#define OLED_I2C_SCL_PIN                 GPIO_PINS_6
#define OLED_I2C_SDA_PIN                 GPIO_PINS_7

#define MENU_BUTTON_UP                   0x01U
#define MENU_BUTTON_SELECT               0x02U
#define MENU_BUTTON_DOWN                 0x04U
#define MENU_BUTTON_BACK                 0x08U

void hardware_init(void);
void hardware_debug_led_init(void);
void hardware_task(void);
uint8_t read_menu_buttons(void);
void set_status_led(uint8_t state);
void toggle_status_led(void);
void oled_clear(void);
void oled_draw_text_at(uint8_t x, uint8_t y, const char *text);
void oled_draw_pixel(uint8_t x, uint8_t y, uint8_t state);
void oled_update_status(uint8_t controller_connected, uint8_t mods_enabled);

#ifdef __cplusplus
}
#endif

#endif