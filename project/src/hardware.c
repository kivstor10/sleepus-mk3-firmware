#include "hardware.h"

#ifdef HARDWARE_IMPLEMENTATION

#include <string.h>
#include "wk_system.h"

#define BUTTON_DEBOUNCE_MS               20U
#define OLED_I2C_ADDRESS                 0x78U
#define OLED_I2C_CLKCTRL                 0x00F03B58U
#define OLED_WIDTH                       128U
#define OLED_PAGES                       8U
#define OLED_BUFFER_SIZE                 (OLED_WIDTH * OLED_PAGES)
#define OLED_TRANSFER_PAYLOAD_SIZE       31U

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
  0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40,
  0x8D, 0x14, 0x20, 0x00, 0xA1, 0xC8, 0xDA, 0x12,
  0x81, 0x7F, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6,
  0xAF
};

static const uint8_t glyph_c[5] = {0x3E, 0x41, 0x41, 0x41, 0x22};
static const uint8_t glyph_d[5] = {0x7F, 0x41, 0x41, 0x22, 0x1C};
static const uint8_t glyph_f[5] = {0x7F, 0x09, 0x09, 0x09, 0x01};
static const uint8_t glyph_l[5] = {0x7F, 0x40, 0x40, 0x40, 0x40};
static const uint8_t glyph_m[5] = {0x7F, 0x02, 0x0C, 0x02, 0x7F};
static const uint8_t glyph_n[5] = {0x7F, 0x04, 0x08, 0x10, 0x7F};
static const uint8_t glyph_o[5] = {0x3E, 0x41, 0x41, 0x41, 0x3E};
static const uint8_t glyph_r[5] = {0x7F, 0x09, 0x19, 0x29, 0x46};
static const uint8_t glyph_s[5] = {0x46, 0x49, 0x49, 0x49, 0x31};
static const uint8_t glyph_t[5] = {0x01, 0x01, 0x7F, 0x01, 0x01};
static const uint8_t glyph_colon[5] = {0x00, 0x36, 0x36, 0x00, 0x00};

static uint8_t oled_buffer[OLED_BUFFER_SIZE];
static uint8_t oled_tx_buffer[OLED_TRANSFER_PAYLOAD_SIZE + 1U];
static uint8_t oled_tx_length;
static uint8_t oled_tx_index;
static uint8_t oled_payload_length;
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
  switch(character)
  {
    case 'C': return glyph_c;
    case 'D': return glyph_d;
    case 'F': return glyph_f;
    case 'L': return glyph_l;
    case 'M': return glyph_m;
    case 'N': return glyph_n;
    case 'O': return glyph_o;
    case 'R': return glyph_r;
    case 'S': return glyph_s;
    case 'T': return glyph_t;
    case ':': return glyph_colon;
    default: return 0;
  }
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
  static const uint8_t address_commands[] = {0x21, 0x00, 0x7F, 0x22, 0x00, 0x07};
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
  oled_start_transfer(0x40, &oled_buffer[oled_refresh_offset], length,
                      OLED_TRANSFER_DATA);
}

void hardware_init(void)
{
  gpio_init_type gpio_init_struct;

  crm_periph_clock_enable(CRM_GPIOA_PERIPH_CLOCK, TRUE);

  gpio_default_para_init(&gpio_init_struct);
  gpio_init_struct.gpio_pins = MENU_UP_PIN | MENU_SELECT_PIN |
                               MENU_DOWN_PIN | MENU_BACK_PIN;
  gpio_init_struct.gpio_mode = GPIO_MODE_INPUT;
  gpio_init_struct.gpio_pull = GPIO_PULL_UP;
  gpio_init(GPIOA, &gpio_init_struct);
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
  oled_draw_text(20, 2, controller_connected ? "CTRL:ON" : "CTRL:OFF");
  oled_draw_text(20, 4, mods_enabled ? "MODS:ON" : "MODS:OFF");
  oled_refresh_pending = 1;
}

void hardware_task(void)
{
  if(oled_tx_state == OLED_TX_IDLE)
  {
    oled_schedule_transfer();
    return;
  }

  if(i2c_flag_get(I2C1, I2C_ACKFAIL_FLAG) != RESET ||
     i2c_flag_get(I2C1, I2C_BUSERR_FLAG) != RESET ||
     i2c_flag_get(I2C1, I2C_ARLOST_FLAG) != RESET)
  {
    i2c_flag_clear(I2C1, I2C_ACKFAIL_FLAG | I2C_BUSERR_FLAG | I2C_ARLOST_FLAG);
    oled_tx_state = OLED_TX_IDLE;
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