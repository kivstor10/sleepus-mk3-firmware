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

#define MOD_ENGINE_IMPLEMENTATION
#include "mod_engine.c"
#define HARDWARE_IMPLEMENTATION
#include "hardware.c"

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
  uint8_t menu_buttons;
  uint8_t menu_up_was_held = 0;
  uint8_t cached_input_ready = 0;
  uint8_t report_pending = 0;
  uint8_t repeat_led_state = 0xFF;
  uint8_t audio_faults;
  uint8_t audio_fault_display = 0;
  uint32_t audio_fault_display_after = 0;
  uint32_t audio_fault_display_started = 0;

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
  set_status_led(mod_engine_repeat_features_enabled());

  /* init acc function. */
  wk_acc_init();

  /* init usb_otgfs2 function. */
  wk_usb_otgfs2_init();

  /* Expose the device immediately; controller enumeration remains nonblocking. */
  if(wk_usb_device_init())
  {
    set_status_led(0);
  }

  wk_usb_otgfs1_init();
  wk_usb_host_init();

  /* add user code begin 2 */

  /* add user code end 2 */

  while(1)
  {
    wk_usb_app_task();

    if(report_pending && usb_device_send_report(report, report_length))
    {
      report_pending = 0;
    }

    if(!report_pending)
    {
      if(usbh_get_latest_report(&raw_data))
      {
        current_time = get_system_tick();
        report_length = usbh_encode_latest_report(&raw_data, report,
                                                  sizeof(report));
        if(report_length >= 2 && report[0] == 0x20 && report[1] == 0x00)
        {
          cached_data = raw_data;
          cached_input_ready = 1;
          output_data = cached_data;
          mod_engine_process(&output_data, current_time);
          report_length = usbh_encode_latest_report(&output_data, report,
                                                    sizeof(report));
        }
        report_pending = report_length != 0;
      }
      else if(cached_input_ready)
      {
        current_time = get_system_tick();
        output_data = cached_data;
        if(mod_engine_process(&output_data, current_time))
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

    menu_buttons = read_menu_buttons();
    if((menu_buttons & MENU_BUTTON_UP) != 0)
    {
      if(!menu_up_was_held)
      {
        mod_engine_toggle_repeat_features(get_system_tick());
      }
      menu_up_was_held = 1;
    }
    else
    {
      menu_up_was_held = 0;
    }

    if(repeat_led_state != mod_engine_repeat_features_enabled())
    {
      repeat_led_state = mod_engine_repeat_features_enabled();
      usb_audio_output_faults_clear();
      audio_fault_display = 0;
      set_status_led(repeat_led_state);
      audio_fault_display_after = get_system_tick() + 1000U;
    }

    current_time = get_system_tick();
    audio_faults = usb_audio_output_faults();
    if(audio_fault_display == 0 && audio_faults != 0 &&
       (int32_t)(current_time - audio_fault_display_after) >= 0)
    {
      audio_fault_display = audio_faults;
      audio_fault_display_started = current_time;
      usb_audio_output_faults_clear();
    }
    if(audio_fault_display != 0)
    {
      uint32_t elapsed = current_time - audio_fault_display_started;
      uint8_t pulse_count = (audio_fault_display & 0x04) != 0 ? 3U :
                            ((audio_fault_display & 0x02) != 0 ? 2U : 1U);

      if(elapsed < (uint32_t)pulse_count * 250U)
      {
        set_status_led((uint8_t)((elapsed % 250U) < 100U));
      }
      else if(elapsed < 1200U)
      {
        set_status_led(0);
      }
      else
      {
        audio_fault_display = 0;
        set_status_led(repeat_led_state);
      }
    }
    /* add user code begin 3 */

    /* add user code end 3 */
  }
}

  /* add user code begin 4 */

  /* add user code end 4 */
