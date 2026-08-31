/**
  **************************************************************************
  * @file     custom_hid_class.h
  * @brief    usb hid header file
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

 /* define to prevent recursive inclusion -------------------------------------*/
#ifndef __CUSTOM_HID_CLASS_H
#define __CUSTOM_HID_CLASS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "usb_std.h"
#include "usbd_core.h"

/** @addtogroup AT32F435_437_middlewares_usbd_class
  * @{
  */

/** @addtogroup USB_custom_hid_class
  * @{
  */

/** @defgroup USB_custom_hid_class_endpoint_definition
  * @{
  */

/**
  * @brief usb custom hid use endpoint define
  */
#define USBD_CUSTOM_HID_IN_EPT                  0x82
#define USBD_CUSTOM_HID_OUT_EPT                 0x02
#define USBD_XBOX_AUDIO_IN_EPT                  0x83
#define USBD_XBOX_AUDIO_OUT_EPT                 0x03
#define USBD_XBOX_CHAT_IN_EPT                   0x84
#define USBD_XBOX_CHAT_OUT_EPT                  0x04
#define USBD_XBOX_CONTROL_MAX_SIZE             64

/**
  * @brief usb custom hid in and out max packet size define
  */
#define USBD_CUSTOM_IN_MAXPACKET_SIZE           0x40
#define USBD_CUSTOM_OUT_MAXPACKET_SIZE          0x40
#define USBD_XBOX_AUDIO_IN_MAXPACKET_SIZE       0x40
#define USBD_XBOX_AUDIO_OUT_MAXPACKET_SIZE      0xE4
#define USBD_XBOX_CHAT_MAXPACKET_SIZE           0x40

/**
  * @}
  */

/** @defgroup USB_custom_hid_class_request_code_definition
  * @{
  */

typedef struct
{
  uint8_t g_rxhid_buff[USBD_CUSTOM_OUT_MAXPACKET_SIZE];
  uint8_t g_txhid_buff[USBD_CUSTOM_IN_MAXPACKET_SIZE];
  uint8_t audio_rx_buffer[USBD_XBOX_AUDIO_OUT_MAXPACKET_SIZE];
  uint8_t audio_tx_buffer[USBD_XBOX_AUDIO_IN_MAXPACKET_SIZE];
  uint8_t chat_rx_buffer[USBD_XBOX_CHAT_MAXPACKET_SIZE];

  uint32_t hid_protocol;
  uint32_t hid_set_idle;
  uint8_t alt_setting[3];
  
  uint8_t hid_set_report[64];
  uint8_t hid_get_report[64];
  uint8_t hid_state;
  uint8_t send_state;
  uint8_t audio_send_state;
}custom_hid_type;

/**
  * @}
  */

/** @defgroup USB_custom_hid_class_exported_functions
  * @{
  */
extern usbd_class_handler custom_hid_class_handler;
usb_sts_type custom_hid_class_send_report(void *udev, uint8_t *report, uint16_t len);
uint8_t custom_hid_control_request_take(usb_setup_type *setup,
                                        uint8_t *data, uint16_t capacity);
void custom_hid_control_complete(void *udev, const uint8_t *data,
                                 uint16_t length, uint8_t success);
uint8_t custom_hid_output_packet_take(void *udev, uint8_t *data,
                                      uint16_t capacity, uint16_t *length);
uint8_t custom_hid_audio_output_take(void *udev, uint8_t *data,
                                     uint16_t capacity, uint16_t *length);
uint8_t custom_hid_audio_output_faults(void);
void custom_hid_audio_output_faults_clear(void);
usb_sts_type custom_hid_audio_input_send(void *udev, const uint8_t *data,
                                         uint16_t length);
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
