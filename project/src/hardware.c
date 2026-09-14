#include "hardware.h"

#ifdef HARDWARE_IMPLEMENTATION

#include <string.h>
#include "at32f435_437_usart.h"
#include "wk_system.h"
#include "diagnostic_log.h"

#ifdef USB_DIAGNOSTICS
#define DIAGNOSTIC_UART_BUFFER_SIZE      2048U
#define DIAGNOSTIC_UART_BUFFER_MASK      (DIAGNOSTIC_UART_BUFFER_SIZE - 1U)

static uint8_t diagnostic_uart_buffer[DIAGNOSTIC_UART_BUFFER_SIZE];
static volatile uint16_t diagnostic_uart_head;
static volatile uint16_t diagnostic_uart_tail;

static char *diagnostic_append_text(char *output, const char *text)
{
  while(*text != '\0')
  {
    *output++ = *text++;
  }
  return output;
}

static char *diagnostic_append_hex32(char *output, uint32_t value)
{
  static const char digits[] = "0123456789ABCDEF";
  int8_t shift;

  for(shift = 28; shift >= 0; shift -= 4)
  {
    *output++ = digits[(value >> shift) & 0x0FU];
  }
  return output;
}

void diagnostic_uart_init(void)
{
  gpio_init_type gpio_init_struct;

  crm_periph_clock_enable(CRM_GPIOA_PERIPH_CLOCK, TRUE);
  crm_periph_clock_enable(CRM_USART1_PERIPH_CLOCK, TRUE);
  gpio_pin_mux_config(GPIOA, GPIO_PINS_SOURCE9, GPIO_MUX_7);
  gpio_default_para_init(&gpio_init_struct);
  gpio_init_struct.gpio_pins = GPIO_PINS_9;
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_STRONGER;
  gpio_init(GPIOA, &gpio_init_struct);
  usart_init(USART1, 115200U, USART_DATA_8BITS, USART_STOP_1_BIT);
  usart_transmitter_enable(USART1, TRUE);
  usart_enable(USART1, TRUE);
}

void diagnostic_log_event(const char *event, uint32_t value_a,
                          uint32_t value_b, uint32_t value_c)
{
  char line[72];
  char *output = line;
  uint16_t next;
  uint32_t interrupt_state;

  output = diagnostic_append_text(output, "T=");
  output = diagnostic_append_hex32(output, get_system_tick());
  *output++ = ' ';
  output = diagnostic_append_text(output, event);
  output = diagnostic_append_text(output, " A=");
  output = diagnostic_append_hex32(output, value_a);
  output = diagnostic_append_text(output, " B=");
  output = diagnostic_append_hex32(output, value_b);
  output = diagnostic_append_text(output, " C=");
  output = diagnostic_append_hex32(output, value_c);
  *output++ = '\r';
  *output++ = '\n';

  interrupt_state = __get_PRIMASK();
  __disable_irq();
  for(char *input = line; input < output; input++)
  {
    next = (uint16_t)((diagnostic_uart_head + 1U) &
                      DIAGNOSTIC_UART_BUFFER_MASK);
    if(next == diagnostic_uart_tail)
    {
      break;
    }
    diagnostic_uart_buffer[diagnostic_uart_head] = (uint8_t)*input;
    diagnostic_uart_head = next;
  }
  if(interrupt_state == 0U)
  {
    __enable_irq();
  }
}

void diagnostic_log_text(const char *event, const char *text)
{
  char line[144];
  char *output = line;
  uint16_t next;
  uint32_t interrupt_state;

  output = diagnostic_append_text(output, "T=");
  output = diagnostic_append_hex32(output, get_system_tick());
  *output++ = ' ';
  output = diagnostic_append_text(output, event);
  *output++ = ' ';
  while(*text != '\0' && output < line + sizeof(line) - 3U)
  {
    *output++ = *text++;
  }
  *output++ = '\r';
  *output++ = '\n';

  interrupt_state = __get_PRIMASK();
  __disable_irq();
  for(char *input = line; input < output; input++)
  {
    next = (uint16_t)((diagnostic_uart_head + 1U) &
                      DIAGNOSTIC_UART_BUFFER_MASK);
    if(next == diagnostic_uart_tail)
    {
      break;
    }
    diagnostic_uart_buffer[diagnostic_uart_head] = (uint8_t)*input;
    diagnostic_uart_head = next;
  }
  if(interrupt_state == 0U)
  {
    __enable_irq();
  }
}

void diagnostic_uart_task(void)
{
  static uint32_t heartbeat_time;
  uint8_t byte;
  uint32_t interrupt_state;
  uint32_t current_time = get_system_tick();

  if((uint32_t)(current_time - heartbeat_time) >= 1000U)
  {
    heartbeat_time = current_time;
    diagnostic_log_event("ALIVE", current_time, diagnostic_uart_head,
                         diagnostic_uart_tail);
  }

  if(diagnostic_uart_tail == diagnostic_uart_head ||
     usart_flag_get(USART1, USART_TDBE_FLAG) == RESET)
  {
    return;
  }

  interrupt_state = __get_PRIMASK();
  __disable_irq();
  byte = diagnostic_uart_buffer[diagnostic_uart_tail];
  diagnostic_uart_tail = (uint16_t)((diagnostic_uart_tail + 1U) &
                                    DIAGNOSTIC_UART_BUFFER_MASK);
  if(interrupt_state == 0U)
  {
    __enable_irq();
  }
  usart_data_transmit(USART1, byte);
}
#endif

#define BUTTON_DEBOUNCE_MS               20U
#define OLED_I2C_ADDRESS                 0x78U
#define OLED_I2C_CLKCTRL                 0x80504C4EU
#define OLED_WIDTH                       128U
#define OLED_PAGES                       4U
#define OLED_BUFFER_SIZE                 (OLED_WIDTH * OLED_PAGES)
#define OLED_TRANSFER_PAYLOAD_SIZE       31U
#define OLED_SOFT_I2C_DELAY_US           5U
#define OLED_TRANSFER_TIMEOUT_MS         20U

typedef enum
{
  OLED_TX_IDLE,
  OLED_TX_SENDING,
  OLED_TX_WAIT_STOP
} oled_tx_state_t;

typedef enum
{
  OLED_TRANSFER_NONE,
  OLED_TRANSFER_INIT,
  OLED_TRANSFER_ADDRESS,
  OLED_TRANSFER_DATA
} oled_transfer_t;

static const uint8_t oled_init_commands[] =
{
  0xAE, 0xD5, 0x80, 0xA8, 0x1F, 0xD3, 0x00, 0x40,
  0x8D, 0x14, 0x20, 0x00, 0xA0, 0xC0, 0xDA, 0x02,
  0x81, 0x8F, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6,
  0x2E, 0xAF
};

static const uint8_t oled_font_5x7[][5] =
{
  {0x00, 0x00, 0x00, 0x00, 0x00}, /* space */
  {0x00, 0x00, 0x5F, 0x00, 0x00},
  {0x00, 0x07, 0x00, 0x07, 0x00},
  {0x14, 0x7F, 0x14, 0x7F, 0x14},
  {0x24, 0x2A, 0x7F, 0x2A, 0x12},
  {0x23, 0x13, 0x08, 0x64, 0x62},
  {0x36, 0x49, 0x56, 0x20, 0x50},
  {0x00, 0x08, 0x07, 0x03, 0x00},
  {0x00, 0x1C, 0x22, 0x41, 0x00},
  {0x00, 0x41, 0x22, 0x1C, 0x00},
  {0x2A, 0x1C, 0x7F, 0x1C, 0x2A},
  {0x08, 0x08, 0x3E, 0x08, 0x08},
  {0x00, 0x80, 0x70, 0x30, 0x00},
  {0x08, 0x08, 0x08, 0x08, 0x08},
  {0x00, 0x00, 0x60, 0x60, 0x00},
  {0x20, 0x10, 0x08, 0x04, 0x02},
  {0x3E, 0x51, 0x49, 0x45, 0x3E},
  {0x00, 0x42, 0x7F, 0x40, 0x00},
  {0x72, 0x49, 0x49, 0x49, 0x46},
  {0x21, 0x41, 0x49, 0x4D, 0x33},
  {0x18, 0x14, 0x12, 0x7F, 0x10},
  {0x27, 0x45, 0x45, 0x45, 0x39},
  {0x3C, 0x4A, 0x49, 0x49, 0x31},
  {0x41, 0x21, 0x11, 0x09, 0x07},
  {0x36, 0x49, 0x49, 0x49, 0x36},
  {0x46, 0x49, 0x49, 0x29, 0x1E},
  {0x00, 0x00, 0x14, 0x00, 0x00},
  {0x00, 0x40, 0x34, 0x00, 0x00},
  {0x00, 0x08, 0x14, 0x22, 0x41},
  {0x14, 0x14, 0x14, 0x14, 0x14},
  {0x00, 0x41, 0x22, 0x14, 0x08},
  {0x02, 0x01, 0x59, 0x09, 0x06},
  {0x3E, 0x41, 0x5D, 0x59, 0x4E},
  {0x7C, 0x12, 0x11, 0x12, 0x7C},
  {0x7F, 0x49, 0x49, 0x49, 0x36},
  {0x3E, 0x41, 0x41, 0x41, 0x22},
  {0x7F, 0x41, 0x41, 0x41, 0x3E},
  {0x7F, 0x49, 0x49, 0x49, 0x41},
  {0x7F, 0x09, 0x09, 0x09, 0x01},
  {0x3E, 0x41, 0x41, 0x51, 0x73},
  {0x7F, 0x08, 0x08, 0x08, 0x7F},
  {0x00, 0x41, 0x7F, 0x41, 0x00},
  {0x20, 0x40, 0x41, 0x3F, 0x01},
  {0x7F, 0x08, 0x14, 0x22, 0x41},
  {0x7F, 0x40, 0x40, 0x40, 0x40},
  {0x7F, 0x02, 0x1C, 0x02, 0x7F},
  {0x7F, 0x04, 0x08, 0x10, 0x7F},
  {0x3E, 0x41, 0x41, 0x41, 0x3E},
  {0x7F, 0x09, 0x09, 0x09, 0x06},
  {0x3E, 0x41, 0x51, 0x21, 0x5E},
  {0x7F, 0x09, 0x19, 0x29, 0x46},
  {0x26, 0x49, 0x49, 0x49, 0x32},
  {0x03, 0x01, 0x7F, 0x01, 0x03},
  {0x3F, 0x40, 0x40, 0x40, 0x3F},
  {0x1F, 0x20, 0x40, 0x20, 0x1F},
  {0x3F, 0x40, 0x38, 0x40, 0x3F},
  {0x63, 0x14, 0x08, 0x14, 0x63},
  {0x03, 0x04, 0x78, 0x04, 0x03},
  {0x61, 0x59, 0x49, 0x4D, 0x43}
};

static uint8_t oled_buffer[OLED_BUFFER_SIZE];
static uint8_t oled_refresh_buffer[OLED_BUFFER_SIZE];
static uint8_t oled_tx_buffer[OLED_TRANSFER_PAYLOAD_SIZE + 1U];
static uint8_t oled_tx_length;
static uint8_t oled_tx_index;
static uint8_t oled_payload_length;
static uint32_t oled_transfer_started_at;
static oled_tx_state_t oled_tx_state;
static oled_transfer_t oled_transfer;
static uint16_t oled_refresh_offset;
static uint8_t oled_initialized;
static uint8_t oled_address_sent;
static uint8_t oled_refresh_active;
static uint8_t oled_refresh_pending;
static uint8_t previous_controller_connected = 0xFF;
static uint8_t previous_mods_enabled = 0xFF;
static uint8_t status_led_state;
static uint16_t oled_soft_scl_pin = OLED_I2C_SCL_PIN;
static uint16_t oled_soft_sda_pin = OLED_I2C_SDA_PIN;

static void oled_soft_scl(uint8_t state)
{
  gpio_bits_write(OLED_I2C_PORT, oled_soft_scl_pin, state ? TRUE : FALSE);
}

static void oled_soft_sda(uint8_t state)
{
  gpio_bits_write(OLED_I2C_PORT, oled_soft_sda_pin, state ? TRUE : FALSE);
}

static void oled_soft_delay(void)
{
  wk_delay_us(OLED_SOFT_I2C_DELAY_US);
}

static void oled_soft_stop(void)
{
  oled_soft_sda(0);
  oled_soft_delay();
  oled_soft_scl(1);
  oled_soft_delay();
  oled_soft_sda(1);
  oled_soft_delay();
}

static uint8_t oled_soft_bus_idle(void)
{
  oled_soft_scl(1);
  oled_soft_sda(1);
  oled_soft_delay();
  return gpio_input_data_bit_read(OLED_I2C_PORT, oled_soft_scl_pin) == SET &&
         gpio_input_data_bit_read(OLED_I2C_PORT, oled_soft_sda_pin) == SET;
}

static void oled_soft_recover_bus(void)
{
  uint8_t pulse;

  oled_soft_sda(1);
  oled_soft_delay();
  for(pulse = 0; pulse < 9U; pulse++)
  {
    oled_soft_scl(0);
    oled_soft_delay();
    oled_soft_scl(1);
    oled_soft_delay();
  }
  oled_soft_stop();
}

static void oled_recover_transfer(void)
{
  gpio_init_type gpio_init_struct;

  i2c_reset(I2C1);
  gpio_default_para_init(&gpio_init_struct);
  gpio_init_struct.gpio_pins = OLED_I2C_SCL_PIN | OLED_I2C_SDA_PIN;
  gpio_init_struct.gpio_mode = GPIO_MODE_OUTPUT;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_OPEN_DRAIN;
  gpio_init_struct.gpio_pull = GPIO_PULL_UP;
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_STRONGER;
  gpio_init(OLED_I2C_PORT, &gpio_init_struct);
  oled_soft_recover_bus();
  gpio_pin_mux_config(OLED_I2C_PORT, GPIO_PINS_SOURCE6, GPIO_MUX_4);
  gpio_pin_mux_config(OLED_I2C_PORT, GPIO_PINS_SOURCE7, GPIO_MUX_4);
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init(OLED_I2C_PORT, &gpio_init_struct);
  i2c_init(I2C1, 0x0FU, OLED_I2C_CLKCTRL);
  i2c_enable(I2C1, TRUE);
  oled_tx_state = OLED_TX_IDLE;
  oled_transfer = OLED_TRANSFER_NONE;
  oled_refresh_active = 0;
  oled_refresh_pending = 1;
}

static uint8_t raw_button_state(void)
{
  uint8_t state = 0;

  if(gpio_input_data_bit_read(MENU_UP_PORT, MENU_UP_PIN) == RESET)
  {
    state |= MENU_BUTTON_UP;
  }
  if(gpio_input_data_bit_read(MENU_SELECT_PORT, MENU_SELECT_PIN) == RESET)
  {
    state |= MENU_BUTTON_SELECT;
  }
  if(gpio_input_data_bit_read(MENU_DOWN_PORT, MENU_DOWN_PIN) == RESET)
  {
    state |= MENU_BUTTON_DOWN;
  }
  if(gpio_input_data_bit_read(MENU_BACK_PORT, MENU_BACK_PIN) == RESET)
  {
    state |= MENU_BUTTON_BACK;
  }
  return state;
}

static const uint8_t *oled_glyph(char character)
{
  uint8_t code = (uint8_t)character;
  if(code >= 'a' && code <= 'z')
  {
    code = (uint8_t)(code - 'a' + 'A');
  }
  return code >= ' ' && code <= 'Z' ? oled_font_5x7[code - ' '] : 0;
}

static void oled_draw_text(uint8_t x, uint8_t page, const char *text)
{
  const uint8_t *glyph;
  uint8_t column;

  while(*text != '\0' && x + 5U < OLED_WIDTH && page < OLED_PAGES)
  {
    glyph = oled_glyph(*text++);
    for(column = 0; column < 5; column++)
    {
      oled_buffer[(uint16_t)page * OLED_WIDTH + x++] =
        glyph == 0 ? 0 : glyph[column];
    }
    oled_buffer[(uint16_t)page * OLED_WIDTH + x++] = 0;
  }
}

void oled_clear(void)
{
  memset(oled_buffer, 0, sizeof(oled_buffer));
  oled_refresh_pending = 1;
}

void oled_draw_text_at(uint8_t x, uint8_t y, const char *text)
{
  if(text == 0 || x >= OLED_WIDTH || y >= OLED_PAGES * 8U)
  {
    return;
  }
  oled_draw_text(x, y / 8U, text);
  oled_refresh_pending = 1;
}

void oled_draw_pixel(uint8_t x, uint8_t y, uint8_t state)
{
  uint16_t index;
  uint8_t mask;
  if(x >= OLED_WIDTH || y >= OLED_PAGES * 8U)
  {
    return;
  }
  index = (uint16_t)(y / 8U) * OLED_WIDTH + x;
  mask = (uint8_t)(1U << (y & 7U));
  if(state)
  {
    oled_buffer[index] |= mask;
  }
  else
  {
    oled_buffer[index] &= (uint8_t)~mask;
  }
  oled_refresh_pending = 1;
}

static void oled_start_transfer(uint8_t control, const uint8_t *data,
                                uint8_t length, oled_transfer_t transfer)
{
  uint8_t index;

  oled_tx_buffer[0] = control;
  for(index = 0; index < length; index++)
  {
    oled_tx_buffer[index + 1U] = data[index];
  }
  oled_tx_length = length + 1U;
  oled_payload_length = length;
  oled_tx_index = 0;
  oled_transfer = transfer;
  oled_transfer_started_at = get_system_tick();
  i2c_transmit_set(I2C1, OLED_I2C_ADDRESS, oled_tx_length,
                   I2C_AUTO_STOP_MODE, I2C_GEN_START_WRITE);
  oled_tx_state = OLED_TX_SENDING;
}

static void oled_transfer_complete(void)
{
  if(oled_transfer == OLED_TRANSFER_INIT)
  {
    oled_initialized = 1;
  }
  else if(oled_transfer == OLED_TRANSFER_ADDRESS)
  {
    oled_address_sent = 1;
  }
  else if(oled_transfer == OLED_TRANSFER_DATA)
  {
    oled_refresh_offset += oled_payload_length;
    if(oled_refresh_offset >= OLED_BUFFER_SIZE)
    {
      oled_refresh_active = 0;
    }
  }
  oled_transfer = OLED_TRANSFER_NONE;
}

static void oled_schedule_transfer(void)
{
  static const uint8_t address_commands[] = {0x22, 0x00, 0x03,
                                              0x21, 0x00, 0x7F};
  uint16_t remaining;
  uint8_t length;

  if(!oled_initialized)
  {
    oled_start_transfer(0x00, oled_init_commands,
                        (uint8_t)sizeof(oled_init_commands), OLED_TRANSFER_INIT);
    return;
  }

  if(!oled_refresh_active)
  {
    if(!oled_refresh_pending)
    {
      return;
    }
    oled_refresh_pending = 0;
    oled_refresh_active = 1;
    oled_address_sent = 0;
    oled_refresh_offset = 0;
    memcpy(oled_refresh_buffer, oled_buffer, sizeof(oled_refresh_buffer));
  }

  if(!oled_address_sent)
  {
    oled_start_transfer(0x00, address_commands,
                        (uint8_t)sizeof(address_commands), OLED_TRANSFER_ADDRESS);
    return;
  }

  remaining = OLED_BUFFER_SIZE - oled_refresh_offset;
  length = remaining > OLED_TRANSFER_PAYLOAD_SIZE ?
           OLED_TRANSFER_PAYLOAD_SIZE : (uint8_t)remaining;
  oled_start_transfer(0x40, &oled_refresh_buffer[oled_refresh_offset], length,
                      OLED_TRANSFER_DATA);
}

void hardware_init(void)
{
  gpio_init_type gpio_init_struct;

  crm_periph_clock_enable(CRM_GPIOA_PERIPH_CLOCK, TRUE);
  crm_periph_clock_enable(CRM_GPIOB_PERIPH_CLOCK, TRUE);
  crm_periph_clock_enable(CRM_I2C1_PERIPH_CLOCK, TRUE);

  gpio_default_para_init(&gpio_init_struct);
  gpio_init_struct.gpio_pins = MENU_UP_PIN | MENU_SELECT_PIN |
                               MENU_DOWN_PIN | MENU_BACK_PIN;
  gpio_init_struct.gpio_mode = GPIO_MODE_INPUT;
  gpio_init_struct.gpio_pull = GPIO_PULL_UP;
  gpio_init(GPIOA, &gpio_init_struct);

  gpio_default_para_init(&gpio_init_struct);
  gpio_init_struct.gpio_pins = OLED_I2C_SCL_PIN | OLED_I2C_SDA_PIN;
  gpio_init_struct.gpio_mode = GPIO_MODE_OUTPUT;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_OPEN_DRAIN;
  gpio_init_struct.gpio_pull = GPIO_PULL_UP;
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_STRONGER;
  gpio_init(OLED_I2C_PORT, &gpio_init_struct);
  oled_soft_scl_pin = OLED_I2C_SCL_PIN;
  oled_soft_sda_pin = OLED_I2C_SDA_PIN;
  oled_soft_scl(1);
  oled_soft_sda(1);
  wk_delay_ms(1);
  if(!oled_soft_bus_idle())
  {
    oled_soft_recover_bus();
  }

  gpio_pin_mux_config(OLED_I2C_PORT, GPIO_PINS_SOURCE6, GPIO_MUX_4);
  gpio_pin_mux_config(OLED_I2C_PORT, GPIO_PINS_SOURCE7, GPIO_MUX_4);
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init(OLED_I2C_PORT, &gpio_init_struct);

  i2c_reset(I2C1);
  i2c_init(I2C1, 0x0FU, OLED_I2C_CLKCTRL);
  i2c_enable(I2C1, TRUE);
  memset(oled_buffer, 0, sizeof(oled_buffer));
  oled_refresh_pending = 1;
}

void hardware_debug_led_init(void)
{
  gpio_init_type gpio_init_struct;

  crm_periph_clock_enable(CRM_GPIOC_PERIPH_CLOCK, TRUE);

  gpio_default_para_init(&gpio_init_struct);
  gpio_init_struct.gpio_pins = STATUS_LED_PIN;
  gpio_init_struct.gpio_mode = GPIO_MODE_OUTPUT;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init(STATUS_LED_PORT, &gpio_init_struct);
}

uint8_t read_menu_buttons(void)
{
  static uint8_t initialized;
  static uint8_t candidate_state;
  static uint8_t stable_state;
  static uint32_t changed_at;
  uint8_t state;
  uint32_t current_time_ms;

  state = raw_button_state();
  current_time_ms = get_system_tick();
  if(!initialized)
  {
    initialized = 1;
    candidate_state = state;
    stable_state = state;
    changed_at = current_time_ms;
  }
  else if(state != candidate_state)
  {
    candidate_state = state;
    changed_at = current_time_ms;
  }
  else if(stable_state != candidate_state &&
          current_time_ms - changed_at >= BUTTON_DEBOUNCE_MS)
  {
    stable_state = candidate_state;
  }
  return stable_state;
}

void set_status_led(uint8_t state)
{
  status_led_state = state != 0;
  gpio_bits_write(STATUS_LED_PORT, STATUS_LED_PIN,
                  status_led_state ? TRUE : FALSE);
}

void toggle_status_led(void)
{
  set_status_led(!status_led_state);
}

void oled_update_status(uint8_t controller_connected, uint8_t mods_enabled)
{
  if(controller_connected == previous_controller_connected &&
     mods_enabled == previous_mods_enabled)
  {
    return;
  }

  previous_controller_connected = controller_connected;
  previous_mods_enabled = mods_enabled;
  memset(oled_buffer, 0, sizeof(oled_buffer));
  oled_draw_text(20, 0, controller_connected ? "CTRL:ON" : "CTRL:OFF");
  oled_draw_text(20, 2, mods_enabled ? "MODS:ON" : "MODS:OFF");
  oled_refresh_pending = 1;
}

void hardware_task(void)
{
  uint32_t current_time;

  if(oled_tx_state == OLED_TX_IDLE)
  {
    oled_schedule_transfer();
    return;
  }

  current_time = get_system_tick();
  if((uint32_t)(current_time - oled_transfer_started_at) >=
     OLED_TRANSFER_TIMEOUT_MS)
  {
    oled_recover_transfer();
    return;
  }

  if(i2c_flag_get(I2C1, I2C_ACKFAIL_FLAG) != RESET ||
     i2c_flag_get(I2C1, I2C_BUSERR_FLAG) != RESET ||
     i2c_flag_get(I2C1, I2C_ARLOST_FLAG) != RESET)
  {
    i2c_flag_clear(I2C1, I2C_ACKFAIL_FLAG | I2C_BUSERR_FLAG | I2C_ARLOST_FLAG);
    oled_recover_transfer();
    return;
  }

  if(oled_tx_state == OLED_TX_SENDING)
  {
    if(oled_tx_index < oled_tx_length &&
       i2c_flag_get(I2C1, I2C_TDIS_FLAG) != RESET)
    {
      i2c_data_send(I2C1, oled_tx_buffer[oled_tx_index++]);
      if(oled_tx_index == oled_tx_length)
      {
        oled_tx_state = OLED_TX_WAIT_STOP;
      }
    }
  }
  else if(i2c_flag_get(I2C1, I2C_STOPF_FLAG) != RESET)
  {
    i2c_flag_clear(I2C1, I2C_STOPF_FLAG);
    oled_tx_state = OLED_TX_IDLE;
    oled_transfer_complete();
  }
}

#endif