/**
  **************************************************************************
  * @file     usbh_hid_class.h
  * @brief    usb host hid class header file
  **************************************************************************
  *
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
/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __USBH_HID_CLASS_H
#define __USBH_HID_CLASS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "usbh_core.h"
#include "usb_conf.h"
#include "controller_data.h"

/** @addtogroup AT32F435_437_middlewares_usbh_class
  * @{
  */

/** @addtogroup USBH_hid_class
  * @{
  */

/** @defgroup USBH_hid_class_definition
  * @{
  */

/**
  * @brief  usb hid protocol code
  */
#define USB_HID_NONE_PROTOCOL_CODE       0x00
#define USB_HID_KEYBOARD_PROTOCOL_CODE   0x01
#define USB_HID_MOUSE_PROTOCOL_CODE      0x02

#define USB_XBOX_VENDOR_ID               0x045E
#define USB_XBOX_PRODUCT_ID              0x0B12
#define USB_XBOX_CLASS_CODE              0xFF
#define USB_XBOX_SUBCLASS_CODE           0x47
#define USB_XBOX_PROTOCOL_CODE           0xD0
#define USB_XBOX_INPUT_REPORT_SIZE       18
#define USB_XBOX_AUDIO_INPUT_SIZE        64
#define USB_XBOX_AUDIO_OUTPUT_SIZE       228

/**
  * @brief  usb hid request code
  */
#define USB_HID_GET_REPORT               0x01
#define USB_HID_GET_IDLE                 0x02
#define USB_HID_GET_PROTOCOL             0x03
#define USB_HID_SET_REPORT               0x09
#define USB_HID_SET_IDLE                 0x0A
#define USB_HID_SET_PROTOCOL             0x0B

/**
  * @brief  usb hid request state
  */
typedef enum
{
  USB_HID_STATE_IDLE,
  USB_XBOX_STATE_SET_INPUT_INTERFACE,
  USB_XBOX_STATE_DISABLE_CHAT_INTERFACE,
  USB_XBOX_STATE_SET_AUDIO_INTERFACE,
  USB_HID_STATE_COMPLETE,
}usb_hid_ctrl_state_type;

/**
  * @brief  usb hid process state
  */
typedef enum
{
  USB_HID_INIT,
  USB_HID_GET,
  USB_HID_SEND,
  USB_HID_POLL,
  USB_HID_BUSY,
  USB_HID_ERROR,
}usb_hid_state_type;

/**
  * @brief  usb hid descriptor type
  */
typedef struct
{
  uint8_t bLength;
  uint8_t bDescriptorType;
  uint16_t bcdHID;
  uint8_t bCountryCode;
  uint8_t bNumDescriptors;
  uint8_t bReportDescriptorType;
  uint16_t wItemLength;
}usb_hid_desc_type;

/**
  * @brief  usb hid struct
  */
typedef struct
{
  uint8_t                                chin;
  uint8_t                                eptin;
  uint16_t                               in_maxpacket;
  uint8_t                                in_poll;

  uint8_t                                chout;
  uint8_t                                eptout;
  uint16_t                               out_maxpacket;
  uint8_t                                out_poll;
  uint8_t                                protocol;

  uint8_t                                audio_chin;
  uint8_t                                audio_eptin;
  uint16_t                               audio_in_maxpacket;
  uint8_t                                audio_chout;
  uint8_t                                audio_eptout;
  uint16_t                               audio_out_maxpacket;


  usb_hid_desc_type                      hid_desc;
  usb_hid_ctrl_state_type                ctrl_state;
  usb_hid_state_type                     state;
  uint16_t                               poll_timer;
  uint32_t buffer[16];
}usbh_hid_type;

extern usbh_class_handler_type uhost_hid_class_handler;
extern controller_data_t g_controller_data;
uint8_t usbh_controller_report_seen(void);
uint8_t usbh_controller_report_recent(usbh_core_type *uhost,
                                      uint32_t maximum_age_ms);
uint8_t usbh_get_latest_report(controller_data_t *data);
uint16_t usbh_encode_latest_report(const controller_data_t *data,
                                   uint8_t *report, uint16_t capacity);
uint16_t usbh_encode_controller_report(const controller_data_t *data,
                                       uint8_t *report, uint16_t capacity);
uint8_t usbh_audio_output_send(usbh_core_type *uhost, const uint8_t *data,
                               uint16_t length);
uint8_t usbh_audio_output_faults(void);
void usbh_audio_output_faults_clear(void);
uint8_t usbh_audio_input_take(uint8_t *data, uint16_t capacity,
                              uint16_t *length);
void usbh_audio_irq(usbh_core_type *uhost);


/**
  * @}
  */

/**
  * @}
  */

/**
  * @}
  */
#ifdef __cplusplus
}
#endif

#endif
