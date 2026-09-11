/**
  **************************************************************************
  * @file     custom_hid_class.c
  * @brief    usb custom hid class type
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
#include "usbd_core.h"
#include "custom_hid_class.h"
#include "custom_hid_desc.h"
#include "diagnostic_log.h"
#include <string.h>

/** @addtogroup AT32F435_437_middlewares_usbd_class
  * @{
  */

/** @defgroup USB_custom_hid_class
  * @brief usb device custom hid demo
  * @{
  */

/** @defgroup USB_custom_hid_class_private_functions
  * @{
  */

static usb_sts_type class_init_handler(void *udev);
static usb_sts_type class_clear_handler(void *udev);
static usb_sts_type class_setup_handler(void *udev, usb_setup_type *setup);
static usb_sts_type class_ept0_tx_handler(void *udev);
static usb_sts_type class_ept0_rx_handler(void *udev);
static usb_sts_type class_in_handler(void *udev, uint8_t ept_num);
static usb_sts_type class_out_handler(void *udev, uint8_t ept_num);
static usb_sts_type class_sof_handler(void *udev);
static usb_sts_type class_event_handler(void *udev, usbd_event_type event);

static void usb_hid_buf_process(void *udev, uint8_t *report, uint16_t len);
static usb_sts_type queue_control_request(void *udev, usb_setup_type *setup);
custom_hid_type custom_hid_struct;

typedef enum
{
  XBOX_CONTROL_IDLE,
  XBOX_CONTROL_RECEIVING,
  XBOX_CONTROL_READY,
  XBOX_CONTROL_FORWARDING
} xbox_control_state_type;

static volatile xbox_control_state_type xbox_control_state;
static usb_setup_type xbox_control_setup;
static uint8_t xbox_control_buffer[USBD_XBOX_CONTROL_MAX_SIZE];
static volatile uint16_t xbox_output_length;
static uint8_t xbox_device_qualifier[10];
static uint8_t xbox_device_qualifier_length;
static uint8_t xbox_device_qualifier_known;
static uint8_t xbox_os_string[18];
static uint8_t xbox_os_string_length;
static uint8_t xbox_os_string_known;
static uint8_t xbox_other_speed_configuration[USBD_CUSHID_CONFIG_DESC_SIZE];
static volatile uint8_t xbox_upstream_reset_seen;
static volatile uint8_t xbox_upstream_session_active;
static volatile uint8_t xbox_upstream_wakeup_pending;
#define XBOX_AUDIO_QUEUE_SIZE 128
static uint8_t xbox_audio_queue[XBOX_AUDIO_QUEUE_SIZE]
                               [USBD_XBOX_AUDIO_OUT_MAXPACKET_SIZE];
static uint16_t xbox_audio_lengths[XBOX_AUDIO_QUEUE_SIZE];
static volatile uint8_t xbox_audio_head;
static volatile uint8_t xbox_audio_tail;
static volatile uint8_t xbox_audio_output_faults;
#define XBOX_MICROPHONE_QUEUE_SIZE 8
static uint8_t xbox_microphone_queue[XBOX_MICROPHONE_QUEUE_SIZE]
                                    [USBD_XBOX_AUDIO_IN_MAXPACKET_SIZE];
static uint16_t xbox_microphone_lengths[XBOX_MICROPHONE_QUEUE_SIZE];
static volatile uint8_t xbox_microphone_head;
static volatile uint8_t xbox_microphone_tail;

/* usb device class handler */
usbd_class_handler custom_hid_class_handler =
{
  class_init_handler,
  class_clear_handler,
  class_setup_handler,
  class_ept0_tx_handler,
  class_ept0_rx_handler,
  class_in_handler,
  class_out_handler,
  class_sof_handler,
  class_event_handler,
  &custom_hid_struct
};

/**
  * @brief  initialize usb custom hid endpoint
  * @param  udev: to the structure of usbd_core_type
  * @retval status of usb_sts_type
  */
static usb_sts_type class_init_handler(void *udev)
{
  usb_sts_type status = USB_OK;
  usbd_core_type *pudev = (usbd_core_type *)udev;
  custom_hid_type *pcshid = (custom_hid_type *)pudev->class_handler->pdata;
  /* open custom hid in endpoint */
  usbd_ept_open(pudev, USBD_CUSTOM_HID_IN_EPT, EPT_INT_TYPE, USBD_CUSTOM_IN_MAXPACKET_SIZE);

  /* open custom hid out endpoint */
  usbd_ept_open(pudev, USBD_CUSTOM_HID_OUT_EPT, EPT_INT_TYPE, USBD_CUSTOM_OUT_MAXPACKET_SIZE);

  /* set out endpoint to receive status */
  usbd_ept_recv(pudev, USBD_CUSTOM_HID_OUT_EPT, pcshid->g_rxhid_buff, USBD_CUSTOM_OUT_MAXPACKET_SIZE);

  pcshid->send_state = 0;
  pcshid->audio_send_state = 0;
  xbox_upstream_session_active = 0;
  xbox_output_length = 0;
  xbox_audio_head = 0;
  xbox_audio_tail = 0;
  xbox_audio_output_faults = 0;
  xbox_microphone_head = 0;
  xbox_microphone_tail = 0;
  pcshid->alt_setting[0] = 0;
  pcshid->alt_setting[1] = 0;
  pcshid->alt_setting[2] = 0;
  
  return status;
}

/**
  * @brief  clear endpoint or other state
  * @param  udev: to the structure of usbd_core_type
  * @retval status of usb_sts_type
  */
static usb_sts_type class_clear_handler(void *udev)
{
  usb_sts_type status = USB_OK;
  usbd_core_type *pudev = (usbd_core_type *)udev;

  /* close custom hid in endpoint */
  usbd_ept_close(pudev, USBD_CUSTOM_HID_IN_EPT);

  /* close custom hid out endpoint */
  usbd_ept_close(pudev, USBD_CUSTOM_HID_OUT_EPT);

  usbd_ept_close(pudev, USBD_XBOX_AUDIO_IN_EPT);
  usbd_ept_close(pudev, USBD_XBOX_AUDIO_OUT_EPT);
  usbd_ept_close(pudev, USBD_XBOX_CHAT_IN_EPT);
  usbd_ept_close(pudev, USBD_XBOX_CHAT_OUT_EPT);

  return status;
}

/**
  * @brief  usb device class setup request handler
  * @param  udev: to the structure of usbd_core_type
  * @param  setup: setup packet
  * @retval status of usb_sts_type
  */
static usb_sts_type class_setup_handler(void *udev, usb_setup_type *setup)
{
  usb_sts_type status = USB_OK;
  usbd_core_type *pudev = (usbd_core_type *)udev;
  custom_hid_type *pcshid = (custom_hid_type *)pudev->class_handler->pdata;
  usbd_desc_t *config_descriptor;
  uint16_t len;
  uint8_t *buf;

  switch(setup->bmRequestType & USB_REQ_TYPE_RESERVED)
  {
    case USB_REQ_TYPE_CLASS:
    case USB_REQ_TYPE_VENDOR:
#ifdef USB_UPSTREAM_ATTACH_NO_PROXY_TEST
      usbd_ctrl_unsupport(pudev);
      return USB_FAIL;
#else
      return queue_control_request(udev, setup);
#endif
    /* standard request */
    case USB_REQ_TYPE_STANDARD:
      switch(setup->bRequest)
      {
        case USB_STD_REQ_GET_DESCRIPTOR:
          if((setup->wValue >> 8) == USB_DESCIPTOR_TYPE_STRING &&
             (uint8_t)setup->wValue == USB_WINUSB_OS_STRING)
          {
            if(!xbox_os_string_known || xbox_os_string_length == 0)
            {
              usbd_ctrl_unsupport(pudev);
              return USB_FAIL;
            }
            len = MIN(xbox_os_string_length, setup->wLength);
            usbd_ctrl_send(pudev, xbox_os_string, len);
            return USB_OK;
          }
          else if((setup->wValue >> 8) == USB_DESCIPTOR_TYPE_DEVICE_QUALIFIER)
          {
            if(!xbox_device_qualifier_known)
            {
              diagnostic_log_event("QUAL_UNKNOWN", setup->wValue,
                                   xbox_control_state, setup->wLength);
              diagnostic_log_event("QUAL_IN_STALL", setup->bRequest,
                                   setup->wValue, setup->wIndex);
              usbd_set_stall(pudev, 0x80);
              return USB_FAIL;
            }
            if(xbox_device_qualifier_length == 0)
            {
              diagnostic_log_event("QUAL_IN_STALL", setup->bRequest,
                                   setup->wValue, setup->wIndex);
              usbd_set_stall(pudev, 0x80);
              return USB_FAIL;
            }
            len = MIN(xbox_device_qualifier_length, setup->wLength);
            usbd_ctrl_send(pudev, xbox_device_qualifier, len);
            return USB_OK;
          }
          else if((setup->wValue >> 8) == USB_DESCIPTOR_TYPE_OTHER_SPEED)
          {
            if(!xbox_device_qualifier_known ||
               xbox_device_qualifier_length == 0)
            {
              diagnostic_log_event("OTHER_IN_STALL", setup->bRequest,
                                   setup->wValue, setup->wIndex);
              usbd_set_stall(pudev, 0x80);
              return USB_FAIL;
            }
            config_descriptor =
              custom_hid_desc_handler.get_device_configuration();
            if(config_descriptor == NULL ||
               config_descriptor->descriptor == NULL ||
               config_descriptor->length !=
                 sizeof(xbox_other_speed_configuration))
            {
              usbd_ctrl_unsupport(pudev);
              return USB_FAIL;
            }
            memcpy(xbox_other_speed_configuration,
                   config_descriptor->descriptor,
                   sizeof(xbox_other_speed_configuration));
            xbox_other_speed_configuration[1] =
              USB_DESCIPTOR_TYPE_OTHER_SPEED;
            len = MIN(sizeof(xbox_other_speed_configuration),
                      setup->wLength);
            usbd_ctrl_send(pudev, xbox_other_speed_configuration, len);
            return USB_OK;
          }
          else if(setup->wValue >> 8 == HID_REPORT_DESC)
          {
            len = MIN(USBD_CUSHID_SIZ_REPORT_DESC, setup->wLength);
            buf = (uint8_t *)g_usbd_custom_hid_report;
            usbd_ctrl_send(pudev, (uint8_t *)buf, len);
          }
          else if(setup->wValue >> 8 == HID_DESCRIPTOR_TYPE)
          {
            len = MIN(9, setup->wLength);
            buf = (uint8_t *)g_custom_hid_usb_desc;
            usbd_ctrl_send(pudev, (uint8_t *)buf, len);
          }
          else
          {
            usbd_ctrl_unsupport(pudev);
          }
          break;
        case USB_STD_REQ_GET_INTERFACE:
          if(setup->wIndex < 3)
          {
            usbd_ctrl_send(pudev, &pcshid->alt_setting[setup->wIndex], 1);
          }
          else
          {
            usbd_ctrl_unsupport(pudev);
          }
          break;
        case USB_STD_REQ_SET_INTERFACE:
          if(setup->wIndex >= 3 || setup->wValue > 1)
          {
            usbd_ctrl_unsupport(pudev);
            break;
          }

          pcshid->alt_setting[setup->wIndex] = (uint8_t)setup->wValue;
          if(setup->wIndex == 1)
          {
            usbd_ept_close(pudev, USBD_XBOX_AUDIO_IN_EPT);
            usbd_ept_close(pudev, USBD_XBOX_AUDIO_OUT_EPT);
            pcshid->audio_send_state = 0;
            xbox_microphone_head = 0;
            xbox_microphone_tail = 0;
            if(setup->wValue == 1)
            {
              usbd_ept_open(pudev, USBD_XBOX_AUDIO_IN_EPT, EPT_ISO_TYPE,
                            USBD_XBOX_AUDIO_IN_MAXPACKET_SIZE);
              usbd_ept_open(pudev, USBD_XBOX_AUDIO_OUT_EPT, EPT_ISO_TYPE,
                            USBD_XBOX_AUDIO_OUT_MAXPACKET_SIZE);
              usbd_ept_recv(pudev, USBD_XBOX_AUDIO_OUT_EPT,
                            pcshid->audio_rx_buffer,
                            USBD_XBOX_AUDIO_OUT_MAXPACKET_SIZE);
            }
          }
          else if(setup->wIndex == 2)
          {
            usbd_ept_close(pudev, USBD_XBOX_CHAT_IN_EPT);
            usbd_ept_close(pudev, USBD_XBOX_CHAT_OUT_EPT);
            if(setup->wValue == 1)
            {
              usbd_ept_open(pudev, USBD_XBOX_CHAT_IN_EPT, EPT_BULK_TYPE,
                            USBD_XBOX_CHAT_MAXPACKET_SIZE);
              usbd_ept_open(pudev, USBD_XBOX_CHAT_OUT_EPT, EPT_BULK_TYPE,
                            USBD_XBOX_CHAT_MAXPACKET_SIZE);
              usbd_ept_recv(pudev, USBD_XBOX_CHAT_OUT_EPT,
                            pcshid->chat_rx_buffer,
                            USBD_XBOX_CHAT_MAXPACKET_SIZE);
            }
          }
          break;
        case USB_STD_REQ_CLEAR_FEATURE:
          break;
        case USB_STD_REQ_SET_FEATURE:
          break;
        default:
          usbd_ctrl_unsupport(pudev);
          break;
      }
      break;
    default:
      usbd_ctrl_unsupport(pudev);
      break;
  }
  return status;
}

/**
  * @brief  usb device endpoint 0 in status stage complete
  * @param  udev: to the structure of usbd_core_type
  * @retval status of usb_sts_type
  */
static usb_sts_type class_ept0_tx_handler(void *udev)
{
  usb_sts_type status = USB_OK;

  (void)udev;

  /* ...user code... */

  return status;
}

/**
  * @brief  usb device endpoint 0 out status stage complete
  * @param  udev: to the structure of usbd_core_type
  * @retval status of usb_sts_type
  */
static usb_sts_type class_ept0_rx_handler(void *udev)
{
  usb_sts_type status = USB_OK;
  usbd_core_type *pudev = (usbd_core_type *)udev;
  custom_hid_type *pcshid = (custom_hid_type *)pudev->class_handler->pdata;
  uint32_t recv_len = usbd_get_recv_len(pudev, 0);

  if(xbox_control_state == XBOX_CONTROL_RECEIVING)
  {
    xbox_control_state = XBOX_CONTROL_READY;
    return USB_WAIT;
  }

  /* ...user code... */
  if( pcshid->hid_state == HID_REQ_SET_REPORT)
  {
    /* hid buffer process */
    usb_hid_buf_process(udev, pcshid->hid_set_report, recv_len);
    pcshid->hid_state = 0;
  }

  return status;
}

/**
  * @brief  usb device transmision complete handler
  * @param  udev: to the structure of usbd_core_type
  * @param  ept_num: endpoint number
  * @retval status of usb_sts_type
  */
static usb_sts_type class_in_handler(void *udev, uint8_t ept_num)
{
  usb_sts_type status = USB_OK;
  usbd_core_type *pudev = (usbd_core_type *)udev;
  custom_hid_type *pcshid = (custom_hid_type *)pudev->class_handler->pdata;
  
  if(ept_num == (USBD_CUSTOM_HID_IN_EPT & 0x7F))
  {
    pcshid->send_state = 0;
    xbox_upstream_session_active = 1;
  }
  else if(ept_num == (USBD_XBOX_AUDIO_IN_EPT & 0x7F))
  {
    pcshid->audio_send_state = 0;
  }
  
  /* ...user code...
    trans next packet data
  */

  return status;
}

/**
  * @brief  usb device endpoint receive data
  * @param  udev: to the structure of usbd_core_type
  * @param  ept_num: endpoint number
  * @retval status of usb_sts_type
  */
static usb_sts_type class_out_handler(void *udev, uint8_t ept_num)
{
  usb_sts_type status = USB_OK;
  usbd_core_type *pudev = (usbd_core_type *)udev;
  custom_hid_type *pcshid = (custom_hid_type *)pudev->class_handler->pdata;

  /* get endpoint receive data length  */
  uint32_t recv_len = usbd_get_recv_len(pudev, ept_num);

  if(ept_num == (USBD_CUSTOM_HID_OUT_EPT & 0x7F))
  {
    xbox_output_length = (uint16_t)MIN(recv_len,
                                       USBD_CUSTOM_OUT_MAXPACKET_SIZE);
  }
  else if(ept_num == (USBD_XBOX_AUDIO_OUT_EPT & 0x7F))
  {
    uint8_t next = (uint8_t)((xbox_audio_head + 1) %
                             XBOX_AUDIO_QUEUE_SIZE);
    if(recv_len != 0 && next != xbox_audio_tail)
    {
      recv_len = MIN(recv_len, USBD_XBOX_AUDIO_OUT_MAXPACKET_SIZE);
      memcpy(xbox_audio_queue[xbox_audio_head], pcshid->audio_rx_buffer,
             recv_len);
      xbox_audio_lengths[xbox_audio_head] = (uint16_t)recv_len;
      xbox_audio_head = next;
    }
    else if(recv_len != 0)
    {
      xbox_audio_output_faults = 1;
    }
    usbd_ept_recv(pudev, USBD_XBOX_AUDIO_OUT_EPT, pcshid->audio_rx_buffer,
                  USBD_XBOX_AUDIO_OUT_MAXPACKET_SIZE);
  }
  else if(ept_num == (USBD_XBOX_CHAT_OUT_EPT & 0x7F))
  {
    usbd_ept_recv(pudev, USBD_XBOX_CHAT_OUT_EPT, pcshid->chat_rx_buffer,
                  USBD_XBOX_CHAT_MAXPACKET_SIZE);
  }

  return status;
}

/**
  * @brief  usb device sof handler
  * @param  udev: to the structure of usbd_core_type
  * @retval status of usb_sts_type
  */
static usb_sts_type class_sof_handler(void *udev)
{
  usb_sts_type status = USB_OK;
  usbd_core_type *pudev = (usbd_core_type *)udev;
  custom_hid_type *pcshid = (custom_hid_type *)pudev->class_handler->pdata;

  if(pcshid->alt_setting[1] == 1 && pcshid->audio_send_state == 0 &&
     xbox_microphone_tail != xbox_microphone_head)
  {
    uint8_t tail = xbox_microphone_tail;
    uint16_t packet_length = xbox_microphone_lengths[tail];
    memcpy(pcshid->audio_tx_buffer, xbox_microphone_queue[tail],
           packet_length);
    xbox_microphone_tail = (uint8_t)((tail + 1) %
                                     XBOX_MICROPHONE_QUEUE_SIZE);
    pcshid->audio_send_state = 1;
    usbd_ept_send(pudev, USBD_XBOX_AUDIO_IN_EPT,
                  pcshid->audio_tx_buffer, packet_length);
  }

  return status;
}

/**
  * @brief  usb device event handler
  * @param  udev: to the structure of usbd_core_type
  * @param  event: usb device event
  * @retval status of usb_sts_type
  */
static usb_sts_type class_event_handler(void *udev, usbd_event_type event)
{
  usb_sts_type status = USB_OK;
  usbd_core_type *pudev = (usbd_core_type *)udev;
  custom_hid_type *pcshid = (custom_hid_type *)pudev->class_handler->pdata;

  if(event != USBD_INISOINCOM_EVENT && event != USBD_OUTISOINCOM_EVENT)
  {
    diagnostic_trace_event("UP_EVENT", event, pudev->conn_state,
                           pudev->dev_config);
  }

  switch(event)
  {
    case USBD_RESET_EVENT:

      xbox_upstream_reset_seen = 1;
      xbox_upstream_session_active = 0;
      xbox_control_state = XBOX_CONTROL_IDLE;
      xbox_output_length = 0;
      xbox_audio_head = 0;
      xbox_audio_tail = 0;
      xbox_microphone_head = 0;
      xbox_microphone_tail = 0;

      /* ...user code... */

      break;
    case USBD_SUSPEND_EVENT:

      xbox_upstream_session_active = 0;

      /* ...user code... */

      break;
    case USBD_WAKEUP_EVENT:
      xbox_upstream_wakeup_pending = 1;
      /* ...user code... */

      break;
    case USBD_OUTISOINCOM_EVENT:
      if(pcshid->alt_setting[1] == 1)
      {
        usbd_ept_recv(pudev, USBD_XBOX_AUDIO_OUT_EPT,
                      pcshid->audio_rx_buffer,
                      USBD_XBOX_AUDIO_OUT_MAXPACKET_SIZE);
      }
      break;
    case USBD_INISOINCOM_EVENT:
      if(pcshid->alt_setting[1] == 1)
      {
        otg_eptin_type *audio_in = USB_INEPT(
          pudev->usb_reg, (USBD_XBOX_AUDIO_IN_EPT & 0x7F));
        uint32_t frame = OTG_DEVICE(pudev->usb_reg)->dsts_bit.soffn;

        if((frame & 0x1U) == audio_in->diepctl_bit.dpid)
        {
          audio_in->diepctl_bit.eptdis = TRUE;
          audio_in->diepctl_bit.snak = TRUE;
          usb_flush_tx_fifo(pudev->usb_reg,
                            USBD_XBOX_AUDIO_IN_EPT & 0x7F);
          pcshid->audio_send_state = 0;
        }
      }
      break;
    default:
      break;
  }
  return status;
}

static usb_sts_type queue_control_request(void *udev, usb_setup_type *setup)
{
  usbd_core_type *pudev = (usbd_core_type *)udev;

  if(setup == NULL || setup->wLength > sizeof(xbox_control_buffer) ||
     xbox_control_state != XBOX_CONTROL_IDLE)
  {
    diagnostic_log_event("CTRL_REJECT", xbox_control_state,
                         setup != NULL ? setup->bRequest : 0xFF,
                         setup != NULL ? setup->wValue : 0);
    usbd_ctrl_unsupport(pudev);
    return USB_FAIL;
  }

  xbox_control_setup = *setup;
  if((setup->bmRequestType & 0x80) == 0 && setup->wLength != 0)
  {
    xbox_control_state = XBOX_CONTROL_RECEIVING;
    usbd_ctrl_recv(pudev, xbox_control_buffer, setup->wLength);
  }
  else
  {
    xbox_control_state = XBOX_CONTROL_READY;
  }
  return USB_WAIT;
}

uint8_t custom_hid_control_request_take(usb_setup_type *setup,
                                        uint8_t *data, uint16_t capacity)
{
  if(setup == NULL || data == NULL ||
     xbox_control_state != XBOX_CONTROL_READY ||
     xbox_control_setup.wLength > capacity)
  {
    return 0;
  }

  *setup = xbox_control_setup;
  if((xbox_control_setup.bmRequestType & 0x80) == 0 &&
     xbox_control_setup.wLength != 0)
  {
    memcpy(data, xbox_control_buffer, xbox_control_setup.wLength);
  }
  xbox_control_state = XBOX_CONTROL_FORWARDING;
  return 1;
}

void custom_hid_device_qualifier_set(const uint8_t *data, uint16_t length,
                                     uint8_t supported)
{
  xbox_device_qualifier_known = 1;
  xbox_device_qualifier_length = 0;
  if(supported && data != NULL && length == sizeof(xbox_device_qualifier) &&
     data[0] == sizeof(xbox_device_qualifier) &&
     data[1] == USB_DESCIPTOR_TYPE_DEVICE_QUALIFIER)
  {
    memcpy(xbox_device_qualifier, data, length);
    xbox_device_qualifier_length = (uint8_t)length;
  }
  else
  {
    diagnostic_log_event("QUAL_STALL", supported, length, 0);
  }
}

void custom_hid_os_string_set(const uint8_t *data, uint16_t length,
                              uint8_t supported)
{
  xbox_os_string_known = 1;
  xbox_os_string_length = 0;
  if(supported && data != NULL && length >= 2 &&
     length <= sizeof(xbox_os_string) && data[0] == length &&
     data[1] == USB_DESCIPTOR_TYPE_STRING)
  {
    memcpy(xbox_os_string, data, length);
    xbox_os_string_length = (uint8_t)length;
  }
  diagnostic_log_event("OS_STRING", supported,
                       xbox_os_string_length,
                       xbox_os_string_length >= 4 ?
                         ((uint32_t)xbox_os_string[0] << 24) |
                         ((uint32_t)xbox_os_string[1] << 16) |
                         ((uint32_t)xbox_os_string[2] << 8) |
                         xbox_os_string[3] : 0);
}

void custom_hid_device_qualifier_clear(void)
{
  xbox_device_qualifier_known = 0;
  xbox_device_qualifier_length = 0;
  xbox_os_string_known = 0;
  xbox_os_string_length = 0;
}

void custom_hid_upstream_reset_clear(void)
{
  xbox_upstream_reset_seen = 0;
}

uint8_t custom_hid_upstream_reset_seen(void)
{
  return xbox_upstream_reset_seen;
}

uint8_t custom_hid_upstream_session_active(void)
{
  return xbox_upstream_session_active;
}

uint8_t custom_hid_upstream_wakeup_take(void)
{
  uint32_t interrupt_state = __get_PRIMASK();
  uint8_t pending;

  __disable_irq();
  pending = xbox_upstream_wakeup_pending;
  xbox_upstream_wakeup_pending = 0;
  if(interrupt_state == 0)
  {
    __enable_irq();
  }
  return pending;
}

void custom_hid_control_complete(void *udev, const uint8_t *data,
                                 uint16_t length, uint8_t success)
{
  usbd_core_type *pudev = (usbd_core_type *)udev;

  if(xbox_control_state != XBOX_CONTROL_FORWARDING)
  {
    return;
  }

  xbox_control_state = XBOX_CONTROL_IDLE;
  diagnostic_trace_event("CTRL_DONE", xbox_control_setup.bRequest,
                       length, success);
  if(!success)
  {
    usbd_ctrl_unsupport(pudev);
  }
  else if((xbox_control_setup.bmRequestType & 0x80) != 0 &&
          xbox_control_setup.wLength != 0)
  {
    xbox_upstream_session_active = 1;
    length = MIN(length, xbox_control_setup.wLength);
    usbd_ctrl_send(pudev, (uint8_t *)data, length);
  }
  else
  {
    xbox_upstream_session_active = 1;
    usbd_ctrl_send_status(pudev);
  }
}

uint8_t custom_hid_output_packet_take(void *udev, uint8_t *data,
                                      uint16_t capacity, uint16_t *length)
{
  usbd_core_type *pudev = (usbd_core_type *)udev;
  custom_hid_type *pcshid = (custom_hid_type *)pudev->class_handler->pdata;
  uint16_t packet_length = xbox_output_length;

  if(data == NULL || length == NULL || packet_length == 0 ||
     packet_length > capacity)
  {
    return 0;
  }

  memcpy(data, pcshid->g_rxhid_buff, packet_length);
  xbox_output_length = 0;
  *length = packet_length;
  usbd_ept_recv(pudev, USBD_CUSTOM_HID_OUT_EPT, pcshid->g_rxhid_buff,
                USBD_CUSTOM_OUT_MAXPACKET_SIZE);
  return 1;
}

uint8_t custom_hid_audio_output_take(void *udev, uint8_t *data,
                                     uint16_t capacity, uint16_t *length)
{
  uint32_t interrupt_state;
  uint8_t tail;
  uint16_t packet_length;

  (void)udev;
  if(data == NULL || length == NULL)
  {
    return 0;
  }

  interrupt_state = __get_PRIMASK();
  __disable_irq();
  if(xbox_audio_tail == xbox_audio_head)
  {
    if(interrupt_state == 0)
    {
      __enable_irq();
    }
    return 0;
  }

  tail = xbox_audio_tail;
  packet_length = xbox_audio_lengths[tail];
  if(packet_length > capacity)
  {
    if(interrupt_state == 0)
    {
      __enable_irq();
    }
    return 0;
  }
  memcpy(data, xbox_audio_queue[tail], packet_length);
  xbox_audio_tail = (uint8_t)((tail + 1) % XBOX_AUDIO_QUEUE_SIZE);
  if(interrupt_state == 0)
  {
    __enable_irq();
  }
  *length = packet_length;
  return 1;
}

uint8_t custom_hid_audio_output_faults(void)
{
  return xbox_audio_output_faults;
}

void custom_hid_audio_output_faults_clear(void)
{
  xbox_audio_output_faults = 0;
}

usb_sts_type custom_hid_audio_input_send(void *udev, const uint8_t *data,
                                         uint16_t length)
{
  uint32_t interrupt_state;
  uint8_t next;
  usbd_core_type *pudev = (usbd_core_type *)udev;
  custom_hid_type *pcshid = (custom_hid_type *)pudev->class_handler->pdata;

  if(usbd_connect_state_get(pudev) != USB_CONN_STATE_CONFIGURED ||
     pcshid->alt_setting[1] != 1 ||
     data == NULL || length > sizeof(pcshid->audio_tx_buffer))
  {
    return USB_FAIL;
  }

  interrupt_state = __get_PRIMASK();
  __disable_irq();
  next = (uint8_t)((xbox_microphone_head + 1) %
                   XBOX_MICROPHONE_QUEUE_SIZE);
  if(next == xbox_microphone_tail)
  {
    if(interrupt_state == 0)
    {
      __enable_irq();
    }
    return USB_FAIL;
  }
  memcpy(xbox_microphone_queue[xbox_microphone_head], data, length);
  xbox_microphone_lengths[xbox_microphone_head] = length;
  xbox_microphone_head = next;
  if(interrupt_state == 0)
  {
    __enable_irq();
  }
  return USB_OK;
}

/**
  * @brief  usb device class send report
  * @param  udev: to the structure of usbd_core_type
  * @param  report: report buffer
  * @param  len: report length
  * @retval status of usb_sts_type
  */
usb_sts_type custom_hid_class_send_report(void *udev, uint8_t *report, uint16_t len)
{
  usb_sts_type status = USB_FAIL;
  usbd_core_type *pudev = (usbd_core_type *)udev;
  custom_hid_type *pcshid = (custom_hid_type *)pudev->class_handler->pdata;

  if(usbd_connect_state_get(pudev) == USB_CONN_STATE_CONFIGURED &&
     pcshid->send_state == 0 && report != NULL &&
     len <= sizeof(pcshid->g_txhid_buff))
  {
    memcpy(pcshid->g_txhid_buff, report, len);
    pcshid->send_state = 1;
    usbd_ept_send(pudev, USBD_CUSTOM_HID_IN_EPT, pcshid->g_txhid_buff, len);
    status = USB_OK;
  }
  return status;
}

/**
  * @brief  usb device report function
  * @param  udev: to the structure of usbd_core_type
  * @param  report: report buffer
  * @param  len: report length
  * @retval none
  */
static void usb_hid_buf_process(void *udev, uint8_t *report, uint16_t len)
{
  uint32_t i_index;
  usbd_core_type *pudev = (usbd_core_type *)udev;
  custom_hid_type *pcshid = (custom_hid_type *)pudev->class_handler->pdata;

  switch(report[0])
  {
    case HID_REPORT_ID_2:
      if(pcshid->g_rxhid_buff[1] == 0)
      {
        
      }
      else
      {
        
      }
      break;
    case HID_REPORT_ID_3:
      if(pcshid->g_rxhid_buff[1] == 0)
      {
        
      }
      else
      {
        
      }
      break;
    case HID_REPORT_ID_4:
      if(pcshid->g_rxhid_buff[1] == 0)
      {
        
      }
      else
      {
        
      }
      break;
    case HID_REPORT_ID_6:
      for(i_index = 0; i_index < len; i_index ++)
      {
        pcshid->g_txhid_buff[i_index] = report[i_index];
      }
      custom_hid_class_send_report(pudev, pcshid->g_txhid_buff, len);
      break;
    default:
      break;
  }

}

/**
  * @}
  */

/**
  * @}
  */

/**
  * @}
  */

