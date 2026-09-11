/**
  **************************************************************************
  * @file     usbh_hid_class.c
  * @brief    usb host hid class type
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
 #include "usbh_hid_class.h"
 #include "usb_conf.h"
 #include "usbh_core.h"
 #include "usbh_ctrl.h"
 #include "usbh_hid_mouse.h"
 #include "usbh_hid_keyboard.h"
 #include <string.h>
 #include "diagnostic_log.h"

 /** @addtogroup AT32F435_437_middlewares_usbh_class
  * @{
  */

/** @defgroup USBH_hid_class
  * @brief usb host class hid demo
  * @{
  */

/** @defgroup USBH_hid_class_private_functions
  * @{
  */

 static usb_sts_type uhost_init_handler(void *uhost);
 static usb_sts_type uhost_reset_handler(void *uhost);
 static usb_sts_type uhost_request_handler(void *uhost);
 static usb_sts_type uhost_process_handler(void *uhost);
 static uint8_t xbox_input_report_decode(const uint8_t *report, uint16_t length);
 static void xbox_audio_process(usbh_core_type *uhost, usbh_hid_type *phid);

 usbh_hid_type usbh_hid;
 controller_data_t g_controller_data;
#define XBOX_REPORT_QUEUE_SIZE 16
 static uint8_t report_queue[XBOX_REPORT_QUEUE_SIZE][64];
 static uint16_t report_queue_lengths[XBOX_REPORT_QUEUE_SIZE];
 static volatile uint8_t report_queue_head;
 static volatile uint8_t report_queue_tail;
 static uint8_t latest_input_report[64];
 static uint16_t latest_input_report_length;
static volatile uint8_t controller_report_seen;
static volatile uint32_t controller_last_report_frame;
 static uint8_t controller_input_report[64];
 static uint16_t controller_input_report_length;
#define XBOX_AUDIO_INPUT_QUEUE_SIZE 8
 static uint8_t audio_input_buffer[USB_XBOX_AUDIO_INPUT_SIZE];
 static uint8_t audio_input_queue[XBOX_AUDIO_INPUT_QUEUE_SIZE]
                                 [USB_XBOX_AUDIO_INPUT_SIZE];
 static uint16_t audio_input_lengths[XBOX_AUDIO_INPUT_QUEUE_SIZE];
 static volatile uint8_t audio_input_head;
 static volatile uint8_t audio_input_tail;
 static uint8_t audio_input_busy;
 static uint8_t audio_output_buffer[USB_XBOX_AUDIO_OUTPUT_SIZE];
 static uint8_t audio_output_busy;
#define XBOX_AUDIO_OUTPUT_QUEUE_SIZE 64
 static uint8_t audio_output_queue[XBOX_AUDIO_OUTPUT_QUEUE_SIZE]
                                  [USB_XBOX_AUDIO_OUTPUT_SIZE];
 static uint16_t audio_output_lengths[XBOX_AUDIO_OUTPUT_QUEUE_SIZE];
 static volatile uint8_t audio_output_head;
 static volatile uint8_t audio_output_tail;
 static uint8_t audio_output_error_count;
 static volatile uint8_t audio_output_fault_flags;
 usbh_class_handler_type uhost_hid_class_handler =
 {
   uhost_init_handler,
   uhost_reset_handler,
   uhost_request_handler,
   uhost_process_handler,
   &usbh_hid
 };

/**
  * @brief  usb host class init handler
  * @param  uhost: to the structure of usbh_core_type
  * @retval status: usb_sts_type status
  */
static usb_sts_type uhost_init_handler(void *uhost)
{
  usbh_core_type *puhost = (usbh_core_type *)uhost;
  usb_sts_type status = USB_OK;
  uint8_t hidx, eptidx = 0;
  usbh_hid_type *phid =  &usbh_hid;
  usb_interface_desc_type *interface;

  puhost->class_handler->pdata = &usbh_hid;

  if(puhost->dev.dev_desc.idVendor != USB_XBOX_VENDOR_ID ||
     puhost->dev.dev_desc.idProduct != USB_XBOX_PRODUCT_ID)
  {
    return USB_NOT_SUPPORT;
  }

  for(hidx = 0; hidx < USBH_MAX_INTERFACE; hidx++)
  {
    interface = &puhost->dev.cfg_desc.interface[hidx].interface;
    if(interface->bInterfaceNumber == 0 && interface->bAlternateSetting == 0 &&
       interface->bInterfaceClass == USB_XBOX_CLASS_CODE &&
       interface->bInterfaceSubClass == USB_XBOX_SUBCLASS_CODE &&
       interface->bInterfaceProtocol == USB_XBOX_PROTOCOL_CODE)
    {
      break;
    }
  }
  if(hidx == USBH_MAX_INTERFACE)
  {
    USBH_DEBUG("Unsupported Xbox controller");
    return USB_NOT_SUPPORT;
  }

  for(eptidx = 0; eptidx < puhost->dev.cfg_desc.interface[hidx].interface.bNumEndpoints; eptidx ++)
  {
    if(puhost->dev.cfg_desc.interface[hidx].endpoint[eptidx].bEndpointAddress & 0x80)
    {
      /* find interface out endpoint information */
      phid->eptin = puhost->dev.cfg_desc.interface[hidx].endpoint[eptidx].bEndpointAddress;
      phid->in_maxpacket = puhost->dev.cfg_desc.interface[hidx].endpoint[eptidx].wMaxPacketSize;
      phid->in_poll = puhost->dev.cfg_desc.interface[hidx].endpoint[eptidx].bInterval;

      if(phid->eptin != 0x82 || phid->in_maxpacket > sizeof(phid->buffer))
      {
        return USB_NOT_SUPPORT;
      }

      phid->chin = usbh_alloc_channel(puhost, phid->eptin);
      /* enable channel */
      usbh_hc_open(puhost, phid->chin,phid->eptin,
                    puhost->dev.address, EPT_INT_TYPE,
                    phid->in_maxpacket,
                    puhost->dev.speed);
      usbh_set_toggle(puhost, phid->chin, 0);
    }
    else
    {
      /* get interface out endpoint information */
      phid->eptout = puhost->dev.cfg_desc.interface[hidx].endpoint[eptidx].bEndpointAddress;
      phid->out_maxpacket = puhost->dev.cfg_desc.interface[hidx].endpoint[eptidx].wMaxPacketSize;
      phid->out_poll = puhost->dev.cfg_desc.interface[hidx].endpoint[eptidx].bInterval;

      if(phid->eptout != 0x02)
      {
        return USB_NOT_SUPPORT;
      }

      phid->chout = usbh_alloc_channel(puhost, usbh_hid.eptout);
      /* enable channel */
      usbh_hc_open(puhost, phid->chout, phid->eptout,
                    puhost->dev.address, EPT_INT_TYPE,
                    phid->out_maxpacket,
                    puhost->dev.speed);
      usbh_set_toggle(puhost, phid->chout, 0);
    }
  }
  if(phid->eptin == 0 || phid->eptout == 0)
  {
    return USB_NOT_SUPPORT;
  }

  for(hidx = 0; hidx < USBH_MAX_INTERFACE; hidx++)
  {
    interface = &puhost->dev.cfg_desc.interface[hidx].interface;
    if(interface->bInterfaceNumber != 1 ||
       interface->bAlternateSetting != 1)
    {
      continue;
    }

    for(eptidx = 0; eptidx < interface->bNumEndpoints; eptidx++)
    {
      usb_endpoint_desc_type *endpoint =
        &puhost->dev.cfg_desc.interface[hidx].endpoint[eptidx];
      if((endpoint->bmAttributes & 0x03) != EPT_ISO_TYPE ||
         (endpoint->bEndpointAddress & 0x0F) != 3)
      {
        continue;
      }
      if((endpoint->bEndpointAddress & 0x80) != 0)
      {
        phid->audio_eptin = endpoint->bEndpointAddress;
        phid->audio_in_maxpacket = endpoint->wMaxPacketSize;
      }
      else
      {
        phid->audio_eptout = endpoint->bEndpointAddress;
        phid->audio_out_maxpacket = endpoint->wMaxPacketSize;
      }
    }
    break;
  }
  if(phid->audio_eptin == 0 || phid->audio_eptout == 0 ||
     phid->audio_in_maxpacket > sizeof(audio_input_buffer) ||
     phid->audio_out_maxpacket > sizeof(audio_output_buffer))
  {
    return USB_NOT_SUPPORT;
  }

  phid->audio_chin = usbh_alloc_channel(puhost, phid->audio_eptin);
  usbh_hc_open(puhost, phid->audio_chin, phid->audio_eptin,
               puhost->dev.address, EPT_ISO_TYPE,
               phid->audio_in_maxpacket, puhost->dev.speed);
  phid->audio_chout = usbh_alloc_channel(puhost, phid->audio_eptout);
  usbh_hc_open(puhost, phid->audio_chout, phid->audio_eptout,
               puhost->dev.address, EPT_ISO_TYPE,
               phid->audio_out_maxpacket, puhost->dev.speed);

  phid->ctrl_state = USB_HID_STATE_IDLE;
  phid->state = USB_HID_INIT;
  report_queue_head = 0;
  report_queue_tail = 0;
  latest_input_report_length = 0;
  controller_report_seen = 0;
  controller_last_report_frame = 0;
  controller_input_report_length = 0;
  audio_input_head = 0;
  audio_input_tail = 0;
  audio_input_busy = 0;
  audio_output_busy = 0;
  audio_output_head = 0;
  audio_output_tail = 0;
  audio_output_error_count = 0;
  audio_output_fault_flags = 0;
  memset(&g_controller_data, 0, sizeof(g_controller_data));
  diagnostic_trace_event("HOST_CLASS",
                       ((uint32_t)puhost->dev.dev_desc.idVendor << 16) |
                       puhost->dev.dev_desc.idProduct,
                       ((uint32_t)phid->eptin << 16) | phid->eptout,
                       ((uint32_t)phid->in_maxpacket << 16) |
                       phid->out_maxpacket);
  return status;
}

/**
  * @brief  usb host class reset handler
  * @param  uhost: to the structure of usbh_core_type
  * @retval status: usb_sts_type status
  */
static usb_sts_type uhost_reset_handler(void *uhost)
{
  usbh_core_type *puhost = (usbh_core_type *)uhost;
  usbh_hid_type *phid =  (usbh_hid_type *)puhost->class_handler->pdata;
  usb_sts_type status = USB_OK;
  if(puhost->class_handler->pdata == NULL)
  {
    return status;
  }

  if(phid->chin != 0)
  {
    /* free in channel */
    usbh_free_channel(puhost, phid->chin);
    usbh_ch_disable(puhost, phid->chin);
    phid->chin = 0;
  }

  if(phid->chout != 0)
  {
    /* free out channel */
    usbh_free_channel(puhost, phid->chout);
    usbh_ch_disable(puhost, phid->chout);
    phid->chout = 0;
  }

  if(phid->audio_chin != 0)
  {
    usbh_free_channel(puhost, phid->audio_chin);
    usbh_ch_disable(puhost, phid->audio_chin);
    phid->audio_chin = 0;
  }

  if(phid->audio_chout != 0)
  {
    usbh_free_channel(puhost, phid->audio_chout);
    usbh_ch_disable(puhost, phid->audio_chout);
    phid->audio_chout = 0;
  }

  phid->eptin = 0;
  phid->eptout = 0;
  phid->audio_eptin = 0;
  phid->audio_eptout = 0;
  report_queue_head = 0;
  report_queue_tail = 0;
  latest_input_report_length = 0;
  controller_report_seen = 0;
  controller_last_report_frame = 0;
  controller_input_report_length = 0;
  audio_input_head = 0;
  audio_input_tail = 0;
  audio_input_busy = 0;
  audio_output_busy = 0;
  audio_output_head = 0;
  audio_output_tail = 0;
  audio_output_error_count = 0;
  memset(&g_controller_data, 0, sizeof(g_controller_data));
  diagnostic_trace_event("HOST_RESET", puhost->global_state, 0, 0);

  return status;
}

/**
  * @brief  usb host hid class get descriptor
  * @param  uhost: to the structure of usbh_core_type
  * @param  length: descriptor length
  * @retval status: usb_sts_type status
  */
usb_sts_type usbh_hid_get_desc(void *uhost, uint16_t length)
{
  usbh_core_type *puhost = (usbh_core_type *)uhost;
  usbh_hid_type *phid =  (usbh_hid_type *)puhost->class_handler->pdata;
  usb_sts_type status = USB_WAIT;
  uint8_t bm_req;
  uint16_t wvalue;
  if(puhost->ctrl.state == CONTROL_IDLE)
  {
    bm_req = USB_REQ_RECIPIENT_INTERFACE | USB_REQ_TYPE_STANDARD;
    wvalue = (0x21 << 8) & 0xFF00;

    usbh_get_descriptor(puhost, length, bm_req,
                                 wvalue, puhost->rx_buffer);
  }
  else
  {
    if(usbh_ctrl_result_check(puhost, CONTROL_IDLE, ENUM_IDLE) == USB_OK)
    {
      phid->hid_desc.bLength = puhost->rx_buffer[0];
      phid->hid_desc.bDescriptorType = puhost->rx_buffer[1];
      phid->hid_desc.bcdHID = SWAPBYTE(puhost->rx_buffer+2);
      phid->hid_desc.bCountryCode = puhost->rx_buffer[4];
      phid->hid_desc.bNumDescriptors = puhost->rx_buffer[5];
      phid->hid_desc.bReportDescriptorType = puhost->rx_buffer[6];
      phid->hid_desc.wItemLength = SWAPBYTE(puhost->rx_buffer+7);
      status = USB_OK;
    }
  }
  return status;
}


/**
  * @brief  usb host hid class get report
  * @param  uhost: to the structure of usbh_core_type
  * @param  length: reprot length
  * @retval status: usb_sts_type status
  */
usb_sts_type usbh_hid_get_report(void *uhost, uint16_t length)
{
  usbh_core_type *puhost = (usbh_core_type *)uhost;
  usb_sts_type status = USB_WAIT;
  uint8_t bm_req;
  uint16_t wvalue;
  if(puhost->ctrl.state == CONTROL_IDLE)
  {
    bm_req = USB_REQ_RECIPIENT_INTERFACE | USB_REQ_TYPE_STANDARD;
    wvalue = (0x22 << 8) & 0xFF00;

    usbh_get_descriptor(puhost, length, bm_req,
                                 wvalue, puhost->rx_buffer);
  }
  else
  {
    if(usbh_ctrl_result_check(puhost, CONTROL_IDLE, ENUM_IDLE) == USB_OK)
    {
      status = USB_OK;
    }
  }
  return status;
}

/**
  * @brief  usb host hid class set idle
  * @param  uhost: to the structure of usbh_core_type
  * @param  id: id
  * @param  dr: dr
  * @retval status: usb_sts_type status
  */
usb_sts_type usbh_hid_set_idle(void *uhost, uint8_t id, uint8_t dr)
{
  usbh_core_type *puhost = (usbh_core_type *)uhost;
  usb_sts_type status = USB_WAIT;
  if(puhost->ctrl.state == CONTROL_IDLE)
  {
    puhost->ctrl.setup.bmRequestType = USB_DIR_H2D | USB_REQ_RECIPIENT_INTERFACE | USB_REQ_TYPE_CLASS;
    puhost->ctrl.setup.bRequest = USB_HID_SET_IDLE;
    puhost->ctrl.setup.wValue = (dr << 8) | id;
    puhost->ctrl.setup.wIndex = 0;
    puhost->ctrl.setup.wLength = 0;
    usbh_ctrl_request(puhost, 0, 0);
  }
  else
  {
    status = usbh_ctrl_result_check(puhost, CONTROL_IDLE, ENUM_IDLE);
    if(status == USB_OK || status == USB_NOT_SUPPORT)
    {
      status = USB_OK;
    }
  }
  return status;
}

/**
  * @brief  usb host hid class set protocol
  * @param  uhost: to the structure of usbh_core_type
  * @param  protocol: portocol number
  * @retval status: usb_sts_type status
  */
usb_sts_type usbh_hid_set_protocol(void *uhost, uint8_t protocol)
{
  usbh_core_type *puhost = (usbh_core_type *)uhost;
  usb_sts_type status = USB_WAIT;
  if(puhost->ctrl.state == CONTROL_IDLE)
  {
    puhost->ctrl.setup.bmRequestType = USB_DIR_H2D | USB_REQ_RECIPIENT_INTERFACE | USB_REQ_TYPE_CLASS;
    puhost->ctrl.setup.bRequest = USB_HID_SET_PROTOCOL;
    puhost->ctrl.setup.wValue = protocol;
    puhost->ctrl.setup.wIndex = 0;
    puhost->ctrl.setup.wLength = 0;
    usbh_ctrl_request(puhost, 0, 0);
  }
  else
  {
    status = usbh_ctrl_result_check(puhost, CONTROL_IDLE, ENUM_IDLE);
    if(status == USB_OK || status == USB_NOT_SUPPORT)
    {
      status = USB_OK;
    }
  }
  return status;
}

/**
  * @brief  usb host clear feature
  * @param  uhost: to the structure of usbh_core_type
  * @param  ept_num: endpoint number
  * @param  hc_num: channel number
  * @retval status: usb_sts_type status
  */
usb_sts_type usbh_clear_endpoint_feature(usbh_core_type *uhost, uint8_t ept_num, uint8_t hc_num)
{
  usb_sts_type status = USB_WAIT;
  usbh_core_type *puhost = (usbh_core_type *)uhost;
  uint8_t bm_req;
  bm_req = USB_REQ_RECIPIENT_ENDPOINT | USB_REQ_TYPE_STANDARD;
  if(puhost->ctrl.state == CONTROL_IDLE)
  {
    puhost->ctrl.setup.bmRequestType = USB_DIR_H2D | bm_req;
    puhost->ctrl.setup.bRequest = USB_STD_REQ_CLEAR_FEATURE;
    puhost->ctrl.setup.wValue = 0x00;
    puhost->ctrl.setup.wLength = 0;
    puhost->ctrl.setup.wIndex = ept_num;
    if((ept_num & 0x80) == USB_DIR_D2H)
    {
      puhost->hch[hc_num].toggle_in = 0;
    }
    else
    {
      puhost->hch[hc_num].toggle_out = 0;
    }
    status = usbh_ctrl_request(puhost, 0, 0);
  }
  if(usbh_ctrl_result_check(puhost, CONTROL_IDLE, ENUM_IDLE) == USB_OK)
  {
    status = USB_OK;
  }
  return status;
}

/**
  * @brief  usb host hid class request handler
  * @param  uhost: to the structure of usbh_core_type
  * @retval status: usb_sts_type status
  */
static usb_sts_type uhost_request_handler(void *uhost)
{
  usb_sts_type status = USB_WAIT;
  usbh_core_type *puhost = (usbh_core_type *)uhost;
  usbh_hid_type *phid =  (usbh_hid_type *)puhost->class_handler->pdata;

  switch(phid->ctrl_state)
  {
    case USB_HID_STATE_IDLE:
      phid->ctrl_state = USB_XBOX_STATE_SET_INPUT_INTERFACE;
      break;
    case USB_XBOX_STATE_SET_INPUT_INTERFACE:
      if(puhost->ctrl.state == CONTROL_IDLE)
      {
        usbh_set_interface(puhost, 0, 1);
      }
      if(usbh_ctrl_result_check(puhost, CONTROL_IDLE, ENUM_IDLE) == USB_OK)
      {
        diagnostic_trace_event("SET_IF", 0, 1, 0);
        phid->ctrl_state = USB_XBOX_STATE_DISABLE_CHAT_INTERFACE;
      }
      break;
    case USB_XBOX_STATE_DISABLE_CHAT_INTERFACE:
      if(puhost->ctrl.state == CONTROL_IDLE)
      {
        usbh_set_interface(puhost, 2, 0);
      }
      if(usbh_ctrl_result_check(puhost, CONTROL_IDLE, ENUM_IDLE) == USB_OK)
      {
        diagnostic_trace_event("SET_IF", 2, 0, 0);
        phid->ctrl_state = USB_XBOX_STATE_SET_AUDIO_INTERFACE;
      }
      break;
    case USB_XBOX_STATE_SET_AUDIO_INTERFACE:
      if(puhost->ctrl.state == CONTROL_IDLE)
      {
        usbh_set_interface(puhost, 1, 1);
      }
      if(usbh_ctrl_result_check(puhost, CONTROL_IDLE, ENUM_IDLE) == USB_OK)
      {
        diagnostic_trace_event("SET_IF", 1, 1, 0);
        phid->ctrl_state = USB_HID_STATE_COMPLETE;
      }
      break;
    case USB_HID_STATE_COMPLETE:
      phid->state = USB_HID_INIT;
      status = USB_OK;
      break;
    default:
      break;
  }

  return status;
}

/**
  * @brief  usb host hid class process handler
  * @param  uhost: to the structure of usbh_core_type
  * @retval status: usb_sts_type status
  */
static usb_sts_type uhost_process_handler(void *uhost)
{
  usbh_core_type *puhost = (usbh_core_type *)uhost;
  usbh_hid_type *phid =  (usbh_hid_type *)puhost->class_handler->pdata;
  urb_sts_type urb_status;
  switch(phid->state)
  {
    case USB_HID_INIT:
      phid->state = USB_HID_GET;
      break;

    case USB_HID_GET:
      usbh_interrupt_recv(puhost, phid->chin, (uint8_t *)phid->buffer, phid->in_maxpacket);
      phid->state = USB_HID_POLL;
      phid->poll_timer = usbh_get_frame(puhost->usb_reg);
      break;

    case USB_HID_POLL:
      if((usbh_get_frame(puhost->usb_reg) - phid->poll_timer) >= phid->in_poll )
      {
        phid->state = USB_HID_GET;
      }
      else
      {
        urb_status = usbh_get_urb_status(puhost, phid->chin);
        if(urb_status == URB_DONE)
        {
          puhost->urb_state[phid->chin] = URB_IDLE;
          if(puhost->hch[phid->chin].trans_count != 0)
          {
            controller_last_report_frame = puhost->timer;
          }
          xbox_input_report_decode((uint8_t *)phid->buffer,
                                   (uint16_t)puhost->hch[phid->chin].trans_count);

        }
        else if(urb_status == URB_STALL)
        {
          if(usbh_clear_endpoint_feature(puhost, phid->eptin, phid->chin) ==  USB_OK)
          {
            phid->state = USB_HID_GET;
          }
        }
      }
      break;

    default:
      break;
  }
  return USB_OK;
}

static void xbox_audio_process(usbh_core_type *uhost, usbh_hid_type *phid)
{
  urb_sts_type status;

  if(phid->audio_chin != 0)
  {
    if(audio_input_busy)
    {
      status = usbh_get_urb_status(uhost, phid->audio_chin);
      if(status == URB_DONE)
      {
        uint16_t packet_length =
          (uint16_t)uhost->hch[phid->audio_chin].trans_count;
        uint8_t next = (uint8_t)((audio_input_head + 1) %
                                 XBOX_AUDIO_INPUT_QUEUE_SIZE);
        if(packet_length != 0 && packet_length <= sizeof(audio_input_buffer) &&
           next != audio_input_tail)
        {
          memcpy(audio_input_queue[audio_input_head], audio_input_buffer,
                 packet_length);
          audio_input_lengths[audio_input_head] = packet_length;
          audio_input_head = next;
        }
        uhost->urb_state[phid->audio_chin] = URB_IDLE;
        audio_input_busy = 0;
      }
            else if(status == URB_NOTREADY || status == URB_ERROR ||
              status == URB_STALL)
      {
        uhost->urb_state[phid->audio_chin] = URB_IDLE;
        audio_input_busy = 0;
      }
    }
    if(!audio_input_busy)
    {
      usbh_isoc_recv(uhost, phid->audio_chin, audio_input_buffer,
                     phid->audio_in_maxpacket);
      audio_input_busy = 1;
    }
  }

  if(phid->audio_chout != 0 && audio_output_busy)
  {
    status = usbh_get_urb_status(uhost, phid->audio_chout);
     if(status == URB_DONE || status == URB_NOTREADY ||
       status == URB_ERROR || status == URB_STALL)
    {
      if(status == URB_ERROR || status == URB_STALL)
      {
        if(audio_output_error_count < 3)
        {
          audio_output_error_count++;
        }
        if(audio_output_error_count >= 3)
        {
          audio_output_fault_flags |= 0x04;
        }
      }
      else if(status == URB_DONE)
      {
        audio_output_error_count = 0;
        audio_output_tail = (uint8_t)((audio_output_tail + 1) %
                                      XBOX_AUDIO_OUTPUT_QUEUE_SIZE);
      }
      uhost->urb_state[phid->audio_chout] = URB_IDLE;
      audio_output_busy = 0;
    }
  }
  if(phid->audio_chout != 0 && !audio_output_busy &&
     audio_output_tail != audio_output_head)
  {
    uint8_t tail = audio_output_tail;
    uint16_t packet_length = audio_output_lengths[tail];

    memcpy(audio_output_buffer, audio_output_queue[tail], packet_length);

    usbh_isoc_send(uhost, phid->audio_chout, audio_output_buffer,
                   packet_length);
    audio_output_busy = 1;
  }
}

void usbh_audio_irq(usbh_core_type *uhost)
{
  usbh_hid_type *phid;

  if(uhost == NULL || uhost->global_state != USBH_CLASS ||
     uhost->class_handler == NULL || uhost->class_handler->pdata == NULL)
  {
    return;
  }
  phid = (usbh_hid_type *)uhost->class_handler->pdata;
  xbox_audio_process(uhost, phid);
}

uint8_t usbh_audio_output_send(usbh_core_type *uhost, const uint8_t *data,
                               uint16_t length)
{
  uint32_t interrupt_state;
  uint8_t next;
  usbh_hid_type *phid;

  if(uhost == NULL || uhost->class_handler == NULL ||
     uhost->class_handler->pdata == NULL || data == NULL)
  {
    return 0;
  }
  phid = (usbh_hid_type *)uhost->class_handler->pdata;
  if(phid->audio_chout == 0 || length == 0 ||
     length > phid->audio_out_maxpacket ||
     length > sizeof(audio_output_buffer))
  {
    return 0;
  }

  interrupt_state = __get_PRIMASK();
  __disable_irq();
  next = (uint8_t)((audio_output_head + 1) %
                   XBOX_AUDIO_OUTPUT_QUEUE_SIZE);
  if(next == audio_output_tail)
  {
    if(interrupt_state == 0)
    {
      __enable_irq();
    }
    return 0;
  }
  memcpy(audio_output_queue[audio_output_head], data, length);
  audio_output_lengths[audio_output_head] = length;
  audio_output_head = next;
  if(interrupt_state == 0)
  {
    __enable_irq();
  }
  return 1;
}

uint8_t usbh_audio_output_faults(void)
{
  return audio_output_fault_flags;
}

void usbh_audio_output_faults_clear(void)
{
  audio_output_fault_flags = 0;
}

uint8_t usbh_audio_input_take(uint8_t *data, uint16_t capacity,
                              uint16_t *length)
{
  uint32_t interrupt_state;
  uint8_t tail;
  uint16_t packet_length;

  if(data == NULL || length == NULL)
  {
    return 0;
  }
  interrupt_state = __get_PRIMASK();
  __disable_irq();
  if(audio_input_tail == audio_input_head)
  {
    if(interrupt_state == 0)
    {
      __enable_irq();
    }
    return 0;
  }
  tail = audio_input_tail;
  packet_length = audio_input_lengths[tail];
  if(packet_length > capacity)
  {
    if(interrupt_state == 0)
    {
      __enable_irq();
    }
    return 0;
  }
  memcpy(data, audio_input_queue[tail], packet_length);
  audio_input_tail = (uint8_t)((tail + 1) % XBOX_AUDIO_INPUT_QUEUE_SIZE);
  if(interrupt_state == 0)
  {
    __enable_irq();
  }
  *length = packet_length;
  return 1;
}

static uint8_t xbox_input_report_decode(const uint8_t *report, uint16_t length)
{
  uint8_t next;

  if(report == NULL || length == 0)
  {
    return 0;
  }

  length = MIN(length, sizeof(report_queue[0]));
  next = (uint8_t)((report_queue_head + 1) % XBOX_REPORT_QUEUE_SIZE);
  if(next == report_queue_tail)
  {
    return 0;
  }

  memcpy(report_queue[report_queue_head], report, length);
  report_queue_lengths[report_queue_head] = length;
  report_queue_head = next;
  if(!controller_report_seen)
  {
    uint32_t prefix = 0;
    uint16_t index;
    for(index = 0; index < length && index < 4U; index++)
    {
      prefix = (prefix << 8) | report[index];
    }
    diagnostic_trace_event("FIRST_IN", length, prefix, usbh_hid.in_poll);
  }
  controller_report_seen = 1;
  return 1;
}

uint8_t usbh_controller_report_seen(void)
{
  return controller_report_seen;
}

uint8_t usbh_controller_report_recent(usbh_core_type *uhost,
                                      uint32_t maximum_age_ms)
{
  return uhost != NULL && controller_report_seen &&
    (uint32_t)(uhost->timer - controller_last_report_frame) < maximum_age_ms;
}

uint8_t usbh_get_latest_report(controller_data_t *data)
{
  uint32_t interrupt_state;

  if(data == NULL)
  {
    return 0;
  }

  interrupt_state = __get_PRIMASK();
  __disable_irq();
  if(report_queue_tail == report_queue_head)
  {
    if(interrupt_state == 0)
    {
      __enable_irq();
    }
    return 0;
  }

  latest_input_report_length = report_queue_lengths[report_queue_tail];
  memcpy(latest_input_report, report_queue[report_queue_tail],
         latest_input_report_length);
  report_queue_tail = (uint8_t)((report_queue_tail + 1) %
                                XBOX_REPORT_QUEUE_SIZE);

  if(latest_input_report_length >= USB_XBOX_INPUT_REPORT_SIZE &&
     latest_input_report[0] == 0x20 && latest_input_report[1] == 0x00)
  {
     controller_input_report_length = latest_input_report_length;
     memcpy(controller_input_report, latest_input_report,
          controller_input_report_length);
    g_controller_data.buttons = (uint16_t)latest_input_report[4] |
                                ((uint16_t)latest_input_report[5] << 8);
    g_controller_data.lt = (uint16_t)latest_input_report[6] |
                           ((uint16_t)latest_input_report[7] << 8);
    g_controller_data.rt = (uint16_t)latest_input_report[8] |
                           ((uint16_t)latest_input_report[9] << 8);
    g_controller_data.lx = (int16_t)((uint16_t)latest_input_report[10] |
                                     ((uint16_t)latest_input_report[11] << 8));
    g_controller_data.ly = (int16_t)((uint16_t)latest_input_report[12] |
                                     ((uint16_t)latest_input_report[13] << 8));
    g_controller_data.rx = (int16_t)((uint16_t)latest_input_report[14] |
                                     ((uint16_t)latest_input_report[15] << 8));
    g_controller_data.ry = (int16_t)((uint16_t)latest_input_report[16] |
                                     ((uint16_t)latest_input_report[17] << 8));
  }
  *data = g_controller_data;
  if(interrupt_state == 0)
  {
    __enable_irq();
  }
  return 1;
}

static uint16_t xbox_encode_report(const controller_data_t *data,
                                   const uint8_t *source, uint16_t length,
                                   uint8_t *report, uint16_t capacity)
{
  if(data == NULL || source == NULL || report == NULL || length == 0 ||
     capacity < length)
  {
    return 0;
  }

  memcpy(report, source, length);
  if(length < USB_XBOX_INPUT_REPORT_SIZE ||
     report[0] != 0x20 || report[1] != 0x00)
  {
    return length;
  }

  report[4] = (uint8_t)data->buttons;
  report[5] = (uint8_t)(data->buttons >> 8);
  report[6] = (uint8_t)data->lt;
  report[7] = (uint8_t)(data->lt >> 8);
  report[8] = (uint8_t)data->rt;
  report[9] = (uint8_t)(data->rt >> 8);
  report[10] = (uint8_t)data->lx;
  report[11] = (uint8_t)((uint16_t)data->lx >> 8);
  report[12] = (uint8_t)data->ly;
  report[13] = (uint8_t)((uint16_t)data->ly >> 8);
  report[14] = (uint8_t)data->rx;
  report[15] = (uint8_t)((uint16_t)data->rx >> 8);
  report[16] = (uint8_t)data->ry;
  report[17] = (uint8_t)((uint16_t)data->ry >> 8);
  return length;
}

uint16_t usbh_encode_latest_report(const controller_data_t *data,
                                   uint8_t *report, uint16_t capacity)
{
  return xbox_encode_report(data, latest_input_report,
                            latest_input_report_length, report, capacity);
}

uint16_t usbh_encode_controller_report(const controller_data_t *data,
                                       uint8_t *report, uint16_t capacity)
{
  return xbox_encode_report(data, controller_input_report,
                            controller_input_report_length, report, capacity);
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
