/* add user code begin Header */
/**
  **************************************************************************
  * @file     main.c
  * @brief    main program
  **************************************************************************
  * Copyright (c) 2025, Artery Technology, All rights reserved.
  *
  * The software Board Support Package (BSP) that is made available to
  * download from Artery official website is the copyrighted work of Artery.
  * Artery authorizes customers to use, copy, and distribute the BSP
  * software and its related documentation for the purpose of design and
  * development in conjunction with Artery microcontrollers. Use of the
  * software is governed by this copyright notice and the following disclaimer.
  *
  * THIS SOFTWARE IS PROVIDED ON "AS IS" BASIS WITHOUT WARRANTIES,
  * GUARANTEES OR REPRESENTATIONS OF ANY KIND. ARTERY EXPRESSLY DISCLAIMS,
  * TO THE FULLEST EXTENT PERMITTED BY LAW, ALL EXPRESS, IMPLIED OR
  * STATUTORY OR OTHER WARRANTIES, GUARANTEES OR REPRESENTATIONS,
  * INCLUDING BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY,
  * FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT.
  *
  **************************************************************************
  */
/* add user code end Header */

/* Includes ------------------------------------------------------------------*/
#include "at32f435_437_wk_config.h"
#include "wk_acc.h"
#include "wk_usb_otgfs.h"
#include "usb_app.h"
#include "wk_system.h"
#include "hardware.h"
#include "controller_data.h"
#include "mod_engine.h"
#include "usbh_hid_class.h"
#include "diagnostic_log.h"
#include "lua_runtime.h"

#define MOD_ENGINE_IMPLEMENTATION
#include "mod_engine.c"
#define HARDWARE_IMPLEMENTATION
#include "hardware.c"

#define CONSOLE_SESSION_SETTLE_MS 1000U

/* private includes ----------------------------------------------------------*/
/* add user code begin private includes */

/* add user code end private includes */

/* private typedef -----------------------------------------------------------*/
/* add user code begin private typedef */

/* add user code end private typedef */

/* private define ------------------------------------------------------------*/
/* add user code begin private define */

/* add user code end private define */

/* private macro -------------------------------------------------------------*/
/* add user code begin private macro */

/* add user code end private macro */

/* private variables ---------------------------------------------------------*/
/* add user code begin private variables */

/* add user code end private variables */

/* private function prototypes --------------------------------------------*/
/* add user code begin function prototypes */

/* add user code end function prototypes */

/* private user code ---------------------------------------------------------*/
/* add user code begin 0 */

/* add user code end 0 */

/**
  * @brief main function.
  * @param  none
  * @retval none
  */
int main(void)
{
  controller_data_t raw_data;
  controller_data_t cached_data;
  controller_data_t output_data;
  uint8_t report[64];
  uint16_t report_length;
  uint32_t current_time;
  uint8_t cached_input_ready = 0;
  uint8_t report_pending = 0;
  uint8_t repeat_led_state = 0xFF;
  uint8_t oled_controller_connected = 0;
  uint8_t console_was_configured = 0;
  uint8_t console_session_announced = 0;
  uint8_t oled_status_dirty = 1;
  uint8_t oled_lua_active;
  uint32_t last_controller_report = 0;
  uint32_t console_ready_since = 0;
  uint8_t output_changed;

  /* add user code begin 1 */

  /* add user code end 1 */

  /* system clock config. */
  wk_system_clock_config();

  /* config periph clock. */
  wk_periph_clock_config();

  /* nvic config. */
  wk_nvic_config();

  /* timebase config for
     void wk_delay_us(uint32_t delay);
     void wk_delay_ms(uint32_t delay); */
  wk_timebase_init();

  hardware_init();
  hardware_debug_led_init();
  set_status_led(0);
  diagnostic_uart_init();
  diagnostic_log_event("BOOT", 0, 0, 0);
  oled_lua_active = lua_runtime_init();
  memset(&cached_data, 0, sizeof(cached_data));
  lua_runtime_task(&cached_data, get_system_tick());

  /* init acc function. */
  wk_acc_init();

#ifndef USB_UPSTREAM_ISOLATION_TEST
  /* init usb_otgfs2 function. */
  wk_usb_otgfs2_init();
#endif

#ifndef USB_DOWNSTREAM_ISOLATION_TEST
  wk_usb_otgfs1_init();
#endif

  wk_usb_app_init();
  wk_usb_device_init();
  wk_usb_host_init();

  /* add user code begin 2 */

  /* add user code end 2 */

  while(1)
  {
    uint8_t console_is_configured;

    wk_usb_app_task();
    hardware_task();
    diagnostic_uart_task();
    console_is_configured = usb_device_configured();
    current_time = get_system_tick();
    if(console_is_configured)
    {
      if(!console_was_configured)
      {
        console_ready_since = current_time;
      }
      if(!console_session_announced &&
         (uint32_t)(current_time - console_ready_since) >=
           CONSOLE_SESSION_SETTLE_MS)
      {
        oled_clear();
        if(lua_runtime_active())
        {
          lua_runtime_console_connected();
        }
        else
        {
          oled_status_dirty = 1;
        }
        console_session_announced = 1;
      }
    }
    else
    {
      console_session_announced = 0;
      if(console_was_configured && usb_config_status() == USB_CONFIG_IDLE)
      {
        usb_show_startup_screen();
        set_status_led(0);
      }
    }
    console_was_configured = console_is_configured;
    lua_runtime_task(&cached_data, get_system_tick());

    if(report_pending && usb_device_send_report(report, report_length))
    {
      report_pending = 0;
    }

    if(!report_pending)
    {
      if(usbh_get_latest_report(&raw_data))
      {
        current_time = get_system_tick();
        last_controller_report = current_time;
        if(!oled_controller_connected)
        {
          oled_controller_connected = 1;
          oled_status_dirty = 1;
        }
        report_length = usbh_encode_latest_report(&raw_data, report,
                                                  sizeof(report));
        if(report_length >= 2 && report[0] == 0x20 && report[1] == 0x00)
        {
          cached_data = raw_data;
          cached_input_ready = 1;
          output_data = cached_data;
          if(!lua_runtime_active())
          {
            mod_engine_process(&output_data, current_time);
          }
          lua_runtime_process_input(&cached_data, &output_data, current_time);
          report_length = usbh_encode_latest_report(&output_data, report,
                                                    sizeof(report));
        }
        report_pending = report_length != 0;
      }
      else if(cached_input_ready)
      {
        current_time = get_system_tick();
        output_data = cached_data;
        output_changed = 0;
        if(!lua_runtime_active())
        {
          output_changed = mod_engine_process(&output_data, current_time);
        }
        if(lua_runtime_apply_pending(&output_data, current_time))
        {
          output_changed = 1;
        }
        if(output_changed)
        {
          report_length = usbh_encode_controller_report(&output_data, report,
                                                        sizeof(report));
          report_pending = report_length != 0;
        }
      }
    }

    if(report_pending && usb_device_send_report(report, report_length))
    {
      report_pending = 0;
    }

    if(repeat_led_state != mod_engine_repeat_features_enabled())
    {
      repeat_led_state = mod_engine_repeat_features_enabled();
      oled_status_dirty = 1;
    }

    current_time = get_system_tick();
    if(oled_lua_active != lua_runtime_active())
    {
      oled_lua_active = lua_runtime_active();
      oled_status_dirty = 1;
      if(!oled_lua_active)
      {
        set_status_led(0);
      }
    }
    if(oled_controller_connected &&
       (uint32_t)(current_time - last_controller_report) >= 1000U)
    {
      oled_controller_connected = 0;
      oled_status_dirty = 1;
    }
    if(oled_status_dirty)
    {
      if(!oled_lua_active)
      {
        oled_update_status(oled_controller_connected,
                           mod_engine_repeat_features_enabled());
      }
      oled_status_dirty = 0;
    }
    if(!console_is_configured)
    {
      set_status_led(0);
    }
    /* add user code begin 3 */

    /* add user code end 3 */
  }
}

  /* add user code begin 4 */

  /* add user code end 4 */
