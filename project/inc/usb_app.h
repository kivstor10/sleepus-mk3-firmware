/* add user code begin Header */
/**
  **************************************************************************
  * @file     usb_app.h
  * @brief    usb application config header file
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

/* define to prevent recursive inclusion -------------------------------------*/
#ifndef __USB_APP_H
#define __USB_APP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* private includes -------------------------------------------------------------*/
/* add user code begin private includes */

/* add user code end private includes */

/* private define ------------------------------------------------------------*/
/* add user code begin private define */

/* add user code end private define */

/* exported types -------------------------------------------------------------*/
/* add user code begin exported types */

/* add user code end exported types */

/* exported constants --------------------------------------------------------*/
/* add user code begin exported constants */

/* add user code end exported constants */

/* exported macro ------------------------------------------------------------*/
/* add user code begin exported macro */

/* add user code end exported macro */

void wk_usb_app_init(void);

uint8_t wk_usb_device_init(void);

void wk_usb_host_init(void);

void wk_usb_app_task(void);

uint8_t usb_device_configured(void);
void usb_show_startup_screen(void);

void wk_otgfs1_irq_handler(void);

void wk_otgfs2_irq_handler(void);

uint8_t usb_device_send_report(const uint8_t *report, uint16_t length);
uint8_t usb_audio_output_faults(void);
void usb_audio_output_faults_clear(void);

typedef enum
{
  USB_CONFIG_IDLE = 0,
  USB_CONFIG_WAIT_STORAGE,
  USB_CONFIG_WORKING,
  USB_CONFIG_SUCCESS,
  USB_CONFIG_ERROR
} usb_config_status_type;

typedef enum
{
  USB_CONFIG_EXPORT = 1,
  USB_CONFIG_IMPORT
} usb_config_operation_type;

typedef enum
{
  USB_CONFIG_ERROR_NONE = 0,
  USB_CONFIG_ERROR_PAYLOAD,
  USB_CONFIG_ERROR_NOT_READY,
  USB_CONFIG_ERROR_DISK,
  USB_CONFIG_ERROR_FILESYSTEM,
  USB_CONFIG_ERROR_MOUNT,
  USB_CONFIG_ERROR_OPEN,
  USB_CONFIG_ERROR_WRITE,
  USB_CONFIG_ERROR_SYNC,
  USB_CONFIG_ERROR_CLOSE,
  USB_CONFIG_ERROR_RENAME,
  USB_CONFIG_ERROR_READ,
  USB_CONFIG_ERROR_INVALID_FILE
} usb_config_error_type;

uint8_t usb_config_request(usb_config_operation_type operation);
usb_config_status_type usb_config_status(void);
usb_config_error_type usb_config_error(void);
uint8_t usb_config_reset(void);

/* add user code begin exported functions */

/* add user code end exported functions */

#ifdef __cplusplus
}
#endif

#endif
