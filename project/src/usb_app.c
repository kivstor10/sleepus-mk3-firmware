/* add user code begin Header */
/**
  **************************************************************************
  * @file     usbd_app.c
  * @brief    usb device app
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

#include "usb_conf.h"
#include "usb_core.h"
#include "usb_app.h"
#include "wk_system.h"

#include "usbd_int.h"
#include "custom_hid_class.h"
#include "custom_hid_desc.h"

#include "usbh_int.h"
#include "usbh_ctrl.h"
#include "usbh_hid_class.h"
#include "usbh_user.h"

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

otg_core_type otg_core_struct_fs1;

otg_core_type otg_core_struct_fs2;

static usb_setup_type control_proxy_setup;
static uint8_t control_proxy_buffer[USBD_XBOX_CONTROL_MAX_SIZE];
static uint8_t control_proxy_busy;
static uint8_t output_proxy_buffer[USBD_CUSTOM_OUT_MAXPACKET_SIZE];
static uint16_t output_proxy_length;
static uint8_t output_proxy_busy;
static uint16_t output_proxy_timer;
static uint8_t speaker_proxy_buffer[USBD_XBOX_AUDIO_OUT_MAXPACKET_SIZE];
static uint16_t speaker_proxy_length;
static uint8_t speaker_proxy_pending;
static uint8_t microphone_proxy_buffer[USBD_XBOX_AUDIO_IN_MAXPACKET_SIZE];
static uint16_t microphone_proxy_length;
static uint8_t microphone_proxy_pending;

static void xbox_control_proxy_task(void)
{
  usbh_core_type *host = &otg_core_struct_fs1.host;
  usb_sts_type status;
  uint16_t response_length;

  if(control_proxy_busy)
  {
    if(host->global_state != USBH_CLASS)
    {
      custom_hid_control_complete(&otg_core_struct_fs2.dev, NULL, 0, 0);
      control_proxy_busy = 0;
      return;
    }

    status = usbh_ctrl_result_check(host, CONTROL_IDLE, ENUM_IDLE);
    if(status == USB_WAIT)
    {
      return;
    }

    response_length = 0;
    if(status == USB_OK && (control_proxy_setup.bmRequestType & 0x80) != 0)
    {
      response_length = host->hch[host->ctrl.hch_in].trans_count;
      response_length = MIN(response_length, control_proxy_setup.wLength);
    }
    custom_hid_control_complete(&otg_core_struct_fs2.dev,
                                control_proxy_buffer, response_length,
                                status == USB_OK);
    control_proxy_busy = 0;
    return;
  }

  if(host->global_state == USBH_CLASS && host->req_state == CMD_SEND &&
     custom_hid_control_request_take(&control_proxy_setup,
                                     control_proxy_buffer,
                                     sizeof(control_proxy_buffer)))
  {
    host->ctrl.setup = control_proxy_setup;
    usbh_ctrl_request(host, control_proxy_buffer,
                      control_proxy_setup.wLength);
    control_proxy_busy = 1;
  }
}

static void xbox_output_proxy_task(void)
{
  usbh_core_type *host = &otg_core_struct_fs1.host;
  usbh_hid_type *hid = (usbh_hid_type *)host->class_handler->pdata;
  urb_sts_type status;

  if(output_proxy_busy)
  {
    if(host->global_state != USBH_CLASS || hid == NULL || hid->chout == 0)
    {
      output_proxy_busy = 0;
      return;
    }

    status = usbh_get_urb_status(host, hid->chout);
    if(status == URB_DONE)
    {
      host->urb_state[hid->chout] = URB_IDLE;
      output_proxy_busy = 0;
    }
    else if(status == URB_NOTREADY)
    {
      if((uint16_t)(usbh_get_frame(host->usb_reg) - output_proxy_timer) >=
         hid->out_poll)
      {
        host->urb_state[hid->chout] = URB_IDLE;
        usbh_interrupt_send(host, hid->chout, output_proxy_buffer,
                            output_proxy_length);
        output_proxy_timer = usbh_get_frame(host->usb_reg);
      }
    }
    else if(status == URB_ERROR || status == URB_STALL)
    {
      host->urb_state[hid->chout] = URB_IDLE;
      output_proxy_busy = 0;
    }
    return;
  }

  if(host->global_state == USBH_CLASS && hid != NULL && hid->chout != 0 &&
     custom_hid_output_packet_take(&otg_core_struct_fs2.dev,
                                   output_proxy_buffer,
                                   sizeof(output_proxy_buffer),
                                   &output_proxy_length))
  {
    usbh_interrupt_send(host, hid->chout, output_proxy_buffer,
                        output_proxy_length);
    output_proxy_timer = usbh_get_frame(host->usb_reg);
    output_proxy_busy = 1;
  }
}

static void xbox_speaker_proxy_task(void)
{
  usbh_core_type *host = &otg_core_struct_fs1.host;
  uint8_t packets_moved = 0;

  if(host->global_state != USBH_CLASS)
  {
    speaker_proxy_pending = 0;
    return;
  }

  while(packets_moved < 8)
  {
    if(!speaker_proxy_pending &&
       custom_hid_audio_output_take(&otg_core_struct_fs2.dev,
                                    speaker_proxy_buffer,
                                    sizeof(speaker_proxy_buffer),
                                    &speaker_proxy_length))
    {
      speaker_proxy_pending = 1;
    }
    if(!speaker_proxy_pending ||
       !usbh_audio_output_send(host, speaker_proxy_buffer,
                               speaker_proxy_length))
    {
      break;
    }
    speaker_proxy_pending = 0;
    packets_moved++;
  }
}

static void xbox_microphone_proxy_task(void)
{
  usbh_core_type *host = &otg_core_struct_fs1.host;

  if(host->global_state != USBH_CLASS)
  {
    microphone_proxy_pending = 0;
    return;
  }

  if(!microphone_proxy_pending &&
     usbh_audio_input_take(microphone_proxy_buffer,
                           sizeof(microphone_proxy_buffer),
                           &microphone_proxy_length))
  {
    microphone_proxy_pending = 1;
  }
  if(microphone_proxy_pending &&
     custom_hid_audio_input_send(&otg_core_struct_fs2.dev,
                                 microphone_proxy_buffer,
                                 microphone_proxy_length) == USB_OK)
  {
    microphone_proxy_pending = 0;
  }
}


/* private user code ---------------------------------------------------------*/
/* add user code begin 0 */

/* add user code end 0 */

/**
  * @brief  usb application initialization
  * @param  none
  * @retval none
  */
void wk_usb_app_init(void)
{
  /* add user code begin usb_app_init 0 */

  /* add user code end usb_app_init 0 */

  wk_usb_device_init();
  wk_usb_host_init();
}

uint8_t wk_usb_device_init(void)
{
  usbd_desc_t *device_descriptor;
  usbd_desc_t *config_descriptor;

  /* FS2 is the console-facing Xbox device on PB14/PB15. */
  usbd_init(&otg_core_struct_fs2,
            USB_FULL_SPEED_CORE_ID,
            USB_OTG2_ID,
            &custom_hid_class_handler,
            &custom_hid_desc_handler);

  device_descriptor = custom_hid_desc_handler.get_device_descriptor();
  config_descriptor = custom_hid_desc_handler.get_device_configuration();
  if(device_descriptor == NULL || config_descriptor == NULL ||
     device_descriptor->descriptor == NULL || config_descriptor->descriptor == NULL ||
     device_descriptor->length != USB_DEVICE_DESC_LEN ||
     device_descriptor->descriptor[8] != LBYTE(USBD_CUSHID_VENDOR_ID) ||
     device_descriptor->descriptor[9] != HBYTE(USBD_CUSHID_VENDOR_ID) ||
     config_descriptor->length != USBD_CUSHID_CONFIG_DESC_SIZE)
  {
    return 0;
  }

  /* Clears DCTL.SFTDISCON and enables the internal 1.5 kOhm D+ pull-up. */
  usbd_connect(&otg_core_struct_fs2.dev);
  return 1;
}

void wk_usb_host_init(void)
{
  /* FS1 is the physical-controller host on PA11/PA12. */
  usbh_init(&otg_core_struct_fs1,
            USB_FULL_SPEED_CORE_ID,
            USB_OTG1_ID,
            &uhost_hid_class_handler,
            &usbh_user_handle);
}

/**
  * @brief  usb application task
  * @param  none
  * @retval none
  */
void wk_usb_app_task(void)
{
  /* add user code begin usb_app_task 0 */

  /* add user code end usb_app_task 0 */

  /* add user code begin usb_app_task 1 */

  /* add user code end usb_app_task 1 */

  /* fs1 device custom hid */
  /*
  after the the usb connected, user can use the 'custom_hid_class_send_report' function
  to report hid events, for example, to report a char led on/off event as follows:
  ALIGNED_HEAD static uint8_t report_buf[64] ALIGNED_TAIL;
  if(usbd_connect_state_get(&otg_core_struct_fs1.dev) == USB_CONN_STATE_CONFIGURED)
  {
    report_buf[0] = HID_REPORT_ID_5;
    report_buf[1] = 0;
    custom_hid_class_send_report(&otg_core_struct_fs1.dev, report_buf, 64);
    usb_delay_ms(100);
    report_buf[0] = HID_REPORT_ID_5;
    report_buf[1] = 1;
    custom_hid_class_send_report(&otg_core_struct_fs1.dev, report_buf, 64);
    usb_delay_ms(100);
  }
  */

  /* FS1 physical-controller host; never wait for attachment. */
  usbh_loop_handler(&otg_core_struct_fs1.host);
  xbox_speaker_proxy_task();
  xbox_control_proxy_task();
  xbox_output_proxy_task();

  /* add user code begin usb_app_task 2 */

  /* add user code end usb_app_task 2 */
}

uint8_t usb_device_send_report(const uint8_t *report, uint16_t length)
{
  return custom_hid_class_send_report(&otg_core_struct_fs2.dev,
                                      (uint8_t *)report, length) == USB_OK;
}

uint8_t usb_audio_output_faults(void)
{
  return custom_hid_audio_output_faults() |
         usbh_audio_output_faults() |
         usbh_iso_out_faults();
}

void usb_audio_output_faults_clear(void)
{
  custom_hid_audio_output_faults_clear();
  usbh_audio_output_faults_clear();
  usbh_iso_out_faults_clear();
}

/**
  * @brief  usb interrupt handler
  * @param  none
  * @retval none
  */
void wk_otgfs1_irq_handler(void)
{
  /* add user code begin otgfs1_irq_handler 0 */

  /* add user code end otgfs1_irq_handler 0 */

  usbh_irq_handler(&otg_core_struct_fs1);
  usbh_audio_irq(&otg_core_struct_fs1.host);
  xbox_microphone_proxy_task();

  /* add user code begin otgfs1_irq_handler 1 */

  /* add user code end otgfs1_irq_handler 1 */
}

/**
  * @brief  usb interrupt handler
  * @param  none
  * @retval none
  */
void wk_otgfs2_irq_handler(void)
{
  /* add user code begin otgfs2_irq_handler 0 */

  /* add user code end otgfs2_irq_handler 0 */

  usbd_irq_handler(&otg_core_struct_fs2);

  /* add user code begin otgfs2_irq_handler 1 */

  /* add user code end otgfs2_irq_handler 1 */
}

/**
  * @brief  usb delay function
  * @param  ms: delay number of milliseconds.
  * @retval none
  */
void usb_delay_ms(uint32_t ms)
{
  /* add user code begin delay_ms 0 */

  /* add user code end delay_ms 0 */

  wk_delay_ms(ms);

  /* add user code begin delay_ms 1 */

  /* add user code end delay_ms 1*/
}

/* add user code begin 1 */

/* add user code end 1 */
