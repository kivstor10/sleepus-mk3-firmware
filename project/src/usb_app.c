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
#include "usbh_msc_class.h"
#include "usbh_user.h"
#include "diagnostic_log.h"
#include "ff.h"
#include "lua_storage.h"
#include "hardware.h"

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

#if !defined(USB_UPSTREAM_ISOLATION_TEST) && \
  !defined(USB_UPSTREAM_NO_ATTACH_TEST) && \
  !defined(USB_UPSTREAM_ATTACH_NO_PROXY_TEST)
static usb_setup_type control_proxy_setup;
static uint8_t control_proxy_buffer[USBD_XBOX_CONTROL_MAX_SIZE];
static uint8_t control_proxy_busy;
static uint8_t output_proxy_buffer[USBD_CUSTOM_OUT_MAXPACKET_SIZE];
static uint16_t output_proxy_length;
static uint8_t output_proxy_busy;
static uint16_t output_proxy_timer;
static uint8_t output_trace_buffer[8];
static uint16_t output_trace_length;
static uint8_t output_trace_valid;
#ifdef RUMBLE_OLED_TRACE
static uint16_t rumble_trace_sequence;

static char rumble_trace_hex(uint8_t value)
{
  return "0123456789ABCDEF"[value & 0x0FU];
}

static void rumble_trace_display(const uint8_t *packet, uint16_t length)
{
  char line[14] = "00 00 00 00";
  char status[13] = "OUT 00 #0000";
  uint8_t index;

  for(index = 0; index < 4U && index < length; index++)
  {
    line[index * 3U] = rumble_trace_hex(packet[index] >> 4);
    line[index * 3U + 1U] = rumble_trace_hex(packet[index]);
  }
  status[4] = rumble_trace_hex((uint8_t)(length >> 4));
  status[5] = rumble_trace_hex((uint8_t)length);
  status[8] = rumble_trace_hex((uint8_t)(rumble_trace_sequence >> 12));
  status[9] = rumble_trace_hex((uint8_t)(rumble_trace_sequence >> 8));
  status[10] = rumble_trace_hex((uint8_t)(rumble_trace_sequence >> 4));
  status[11] = rumble_trace_hex((uint8_t)rumble_trace_sequence++);
  oled_clear();
  oled_draw_text_at(0, 0, "XBOX OUTPUT TRACE");
  oled_draw_text_at(0, 8, status);
  oled_draw_text_at(0, 16, line);
  oled_draw_text_at(0, 24, "FILM PRIMARY/SECOND");
}
#endif
static uint8_t speaker_proxy_buffer[USBD_XBOX_AUDIO_OUT_MAXPACKET_SIZE];
static uint16_t speaker_proxy_length;
static uint8_t speaker_proxy_pending;
static uint8_t microphone_proxy_buffer[USBD_XBOX_AUDIO_IN_MAXPACKET_SIZE];
static uint16_t microphone_proxy_length;
static uint8_t microphone_proxy_pending;
#endif
static uint8_t console_device_connected;
static uint8_t console_session_ready;
static uint8_t upstream_retry_disconnected;
static uint8_t upstream_enumeration_active;
static uint32_t upstream_retry_time;
static uint8_t qualifier_probe_state;
static uint8_t startup_screen_drawn;
static usb_config_status_type config_status;
static usb_config_operation_type config_operation;
static usb_config_error_type config_error;
static FATFS config_filesystem;
ALIGNED_HEAD static uint8_t config_buffer[4096] ALIGNED_TAIL;

#define UPSTREAM_RETRY_INTERVAL_MS 2000U
#define UPSTREAM_DISCONNECT_MS      250U
#define UPSTREAM_ENUM_TIMEOUT_MS    5000U

static const uint8_t startup_logo[15][13] =
{
  {0x0F, 0xF9, 0xF0, 0x1F, 0xFC, 0xFF, 0xE7, 0xFF, 0x9F, 0x0F, 0x0F, 0xF8, 0x00},
  {0x0F, 0xF9, 0xF0, 0x1F, 0xFC, 0xFF, 0xE7, 0xFF, 0x9F, 0x0F, 0x0F, 0xF8, 0x00},
  {0x3F, 0xF1, 0xE0, 0x1F, 0xFC, 0xFF, 0xE3, 0xFF, 0xCF, 0x0F, 0x1F, 0xF8, 0x00},
  {0x78, 0x01, 0xE0, 0x1E, 0x00, 0xF8, 0x03, 0xC7, 0xCF, 0x0F, 0x38, 0x00, 0x00},
  {0x7C, 0x01, 0xE0, 0x1E, 0x00, 0xF0, 0x07, 0xC7, 0x9F, 0x0F, 0x3E, 0x00, 0x00},
  {0x7F, 0x01, 0xE0, 0x1E, 0x00, 0xF0, 0x07, 0x87, 0x9E, 0x1F, 0x3F, 0x80, 0x00},
  {0x7F, 0x01, 0xE0, 0x1E, 0x00, 0xF0, 0x07, 0x87, 0x9E, 0x1F, 0x3F, 0x80, 0x00},
  {0x3F, 0xC1, 0xE0, 0x1F, 0xE0, 0xFF, 0x87, 0x87, 0x9E, 0x1E, 0x3F, 0xE0, 0x00},
  {0x1F, 0xF3, 0xC0, 0x3F, 0xE0, 0xFF, 0x07, 0xFF, 0x1E, 0x1E, 0x0F, 0xF0, 0x00},
  {0x07, 0xF3, 0xC0, 0x3C, 0x01, 0xF0, 0x07, 0xFE, 0x1E, 0x1E, 0x03, 0xF8, 0x00},
  {0x01, 0xFB, 0xC0, 0x3C, 0x01, 0xE0, 0x07, 0x80, 0x1E, 0x1E, 0x00, 0xF8, 0x00},
  {0x01, 0xFB, 0xC0, 0x3C, 0x01, 0xE0, 0x07, 0x80, 0x1E, 0x1E, 0x00, 0xF8, 0x00},
  {0x00, 0x73, 0xC0, 0x3C, 0x01, 0xE0, 0x0F, 0x00, 0x3E, 0x3E, 0x00, 0x78, 0x00},
  {0x7F, 0xE3, 0xFF, 0xBF, 0xF9, 0xFF, 0xCF, 0x00, 0x1F, 0xFC, 0x7F, 0xF0, 0x00},
  {0xFF, 0xC7, 0xFF, 0x3F, 0xF1, 0xFF, 0x8F, 0x00, 0x0F, 0xFC, 0x7F, 0xE0, 0x00}
};

void usb_show_startup_screen(void)
{
  uint8_t x;
  uint8_t y;

  oled_clear();
  for(y = 0; y < 15U; y++)
  {
    for(x = 0; x < 104U; x++)
    {
      if((startup_logo[y][x / 8U] & (uint8_t)(0x80U >> (x & 7U))) != 0U)
      {
        oled_draw_pixel((uint8_t)(18U + x), (uint8_t)(8U + y), 1);
      }
    }
  }
}

static void usb_config_select_class(usbh_class_handler_type *handler)
{
  if(otg_core_struct_fs1.host.class_handler != 0 &&
     otg_core_struct_fs1.host.class_handler->reset_handler != 0)
  {
    otg_core_struct_fs1.host.class_handler->reset_handler(
      &otg_core_struct_fs1.host);
  }
  usbh_init(&otg_core_struct_fs1, USB_FULL_SPEED_CORE_ID, USB_OTG1_ID,
            handler, &usbh_user_handle);
}

static void usb_config_restore_controller(void)
{
  f_mount(0, "", 0);
  usb_config_select_class(&uhost_hid_class_handler);
  qualifier_probe_state = 0;
  custom_hid_device_qualifier_clear();
  diagnostic_log_event("CFG_RESTORE", config_operation, config_status,
                       config_error);
}

static void usb_controller_reenumerate(void)
{
  usbh_core_type *host = &otg_core_struct_fs1.host;

  if(host->class_handler != NULL && host->class_handler->reset_handler != NULL)
  {
    host->class_handler->reset_handler(host);
  }
  usbh_cfg_default_init(host);
  host->conn_sts = 1;
  host->port_enable = 0;
#if !defined(USB_UPSTREAM_ISOLATION_TEST) && \
  !defined(USB_UPSTREAM_NO_ATTACH_TEST) && \
  !defined(USB_UPSTREAM_ATTACH_NO_PROXY_TEST)
  control_proxy_busy = 0;
  output_proxy_busy = 0;
  speaker_proxy_pending = 0;
  microphone_proxy_pending = 0;
#endif
}

static FRESULT usb_config_open_import(FIL *file)
{
  static const char *filenames[] =
  {
    "SLEEPUS.CFG", "SLEEPUS.NEW", "SLEEPUS.BAK"
  };
  uint8_t index;
  FRESULT result = FR_NO_FILE;
  for(index = 0; index < sizeof(filenames) / sizeof(filenames[0]); index++)
  {
    result = f_open(file, filenames[index], FA_READ);
    if(result == FR_OK)
    {
      break;
    }
  }
  return result;
}

static uint8_t usb_config_export_file(void)
{
  FIL file;
  UINT written = 0;
  size_t length = 0;
  FRESULT result;
  FRESULT close_result;
  if(!lua_storage_export(config_buffer, sizeof(config_buffer), &length))
  {
    config_error = USB_CONFIG_ERROR_PAYLOAD;
    return 0;
  }
  f_unlink("SLEEPUS.NEW");
  result = f_open(&file, "SLEEPUS.NEW", FA_WRITE | FA_CREATE_ALWAYS);
  if(result != FR_OK)
  {
    config_error = USB_CONFIG_ERROR_OPEN;
    return 0;
  }
  result = f_write(&file, config_buffer, (UINT)length, &written);
  if(result != FR_OK || written != length)
  {
    config_error = USB_CONFIG_ERROR_WRITE;
  }
  else
  {
    result = f_sync(&file);
    if(result != FR_OK)
    {
      config_error = USB_CONFIG_ERROR_SYNC;
    }
  }
  close_result = f_close(&file);
  if(close_result != FR_OK && config_error == USB_CONFIG_ERROR_NONE)
  {
    config_error = USB_CONFIG_ERROR_CLOSE;
  }
  if(config_error != USB_CONFIG_ERROR_NONE)
  {
    return 0;
  }
  f_unlink("SLEEPUS.BAK");
  f_rename("SLEEPUS.CFG", "SLEEPUS.BAK");
  if(f_rename("SLEEPUS.NEW", "SLEEPUS.CFG") != FR_OK)
  {
    config_error = USB_CONFIG_ERROR_RENAME;
    return 0;
  }
  return 1;
}

static uint8_t usb_config_import_file(void)
{
  FIL file;
  UINT received = 0;
  FRESULT result = usb_config_open_import(&file);
  if(result != FR_OK || f_size(&file) != sizeof(config_buffer))
  {
    if(result == FR_OK)
    {
      f_close(&file);
    }
    config_error = result == FR_OK ? USB_CONFIG_ERROR_INVALID_FILE :
      USB_CONFIG_ERROR_OPEN;
    return 0;
  }
  result = f_read(&file, config_buffer, sizeof(config_buffer), &received);
  if(f_close(&file) != FR_OK || result != FR_OK ||
     received != sizeof(config_buffer))
  {
    config_error = USB_CONFIG_ERROR_READ;
    return 0;
  }
  if(!lua_storage_import(config_buffer, sizeof(config_buffer)))
  {
    config_error = USB_CONFIG_ERROR_INVALID_FILE;
    return 0;
  }
  return 1;
}

static usb_config_error_type usb_config_mount_error(FRESULT result)
{
  if(result == FR_NOT_READY)
  {
    return USB_CONFIG_ERROR_NOT_READY;
  }
  if(result == FR_DISK_ERR)
  {
    return USB_CONFIG_ERROR_DISK;
  }
  if(result == FR_NO_FILESYSTEM)
  {
    return USB_CONFIG_ERROR_FILESYSTEM;
  }
  return USB_CONFIG_ERROR_MOUNT;
}

static void usb_config_task(void)
{
  uint8_t success;
  FRESULT mount_result;
  if(config_status == USB_CONFIG_WORKING)
  {
    if(config_operation == USB_CONFIG_IMPORT &&
       !lua_storage_commit_pending())
    {
      config_status = USB_CONFIG_SUCCESS;
    }
    return;
  }
  if(config_status == USB_CONFIG_WAIT_STORAGE)
  {
    if(otg_core_struct_fs1.host.global_state != USBH_CLASS ||
       usbh_msc.state != USBH_MSC_IDLE ||
       usbh_msc.l_unit_n[0].state != USBH_MSC_IDLE ||
       usbh_msc_is_ready(&otg_core_struct_fs1.host, 0) != MSC_OK ||
       usbh_msc.l_unit_n[0].capacity.blk_size == 0U)
    {
      return;
    }
    diagnostic_log_event("CFG_MEDIA",
                         usbh_msc.l_unit_n[0].capacity.blk_size,
                         usbh_msc.l_unit_n[0].capacity.blk_nbr,
                         usbh_msc.max_lun);
    config_status = USB_CONFIG_WORKING;
    mount_result = f_mount(&config_filesystem, "", 1);
    diagnostic_log_event("CFG_MOUNT", mount_result,
                         usbh_msc.l_unit_n[0].capacity.blk_size,
                         usbh_msc.l_unit_n[0].capacity.blk_nbr);
    if(mount_result != FR_OK)
    {
      config_error = usb_config_mount_error(mount_result);
      success = 0;
    }
    else if(config_operation == USB_CONFIG_EXPORT)
    {
      success = usb_config_export_file();
    }
    else
    {
      success = usb_config_import_file();
    }
    usb_config_restore_controller();
    if(!success)
    {
      config_status = USB_CONFIG_ERROR;
    }
    else if(config_operation == USB_CONFIG_IMPORT &&
            lua_storage_commit_pending())
    {
      config_status = USB_CONFIG_WORKING;
    }
    else
    {
      config_status = USB_CONFIG_SUCCESS;
    }
    diagnostic_log_event("CFG_DONE", config_operation, config_status,
                         config_error);
  }
}

uint8_t usb_config_request(usb_config_operation_type operation)
{
  if((operation != USB_CONFIG_EXPORT && operation != USB_CONFIG_IMPORT) ||
     config_status == USB_CONFIG_WAIT_STORAGE ||
     config_status == USB_CONFIG_WORKING)
  {
    return 0;
  }
  usbd_disconnect(&otg_core_struct_fs2.dev);
  console_device_connected = 0;
  config_operation = operation;
  config_error = USB_CONFIG_ERROR_NONE;
  config_status = USB_CONFIG_WAIT_STORAGE;
  diagnostic_log_event("CFG_START", operation,
                       otg_core_struct_fs1.host.global_state,
                       otg_core_struct_fs1.host.conn_sts);
  usb_config_select_class(&uhost_msc_class_handler);
  return 1;
}

usb_config_status_type usb_config_status(void)
{
  return config_status;
}

usb_config_error_type usb_config_error(void)
{
  return config_error;
}

uint8_t usb_config_reset(void)
{
  if(config_status == USB_CONFIG_WORKING)
  {
    return 0;
  }
  if(config_status == USB_CONFIG_WAIT_STORAGE)
  {
    usb_config_restore_controller();
  }
  config_status = USB_CONFIG_IDLE;
  config_error = USB_CONFIG_ERROR_NONE;
  return 1;
}

#if !defined(USB_UPSTREAM_ISOLATION_TEST) && \
  !defined(USB_UPSTREAM_NO_ATTACH_TEST) && \
  !defined(USB_UPSTREAM_ATTACH_NO_PROXY_TEST)
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
    diagnostic_trace_event("CTRL_FWD",
      ((uint32_t)control_proxy_setup.bmRequestType << 24) |
      ((uint32_t)control_proxy_setup.bRequest << 16) |
      control_proxy_setup.wValue,
      ((uint32_t)control_proxy_setup.wIndex << 16) |
      control_proxy_setup.wLength,
      control_proxy_setup.wLength >= 4 &&
      (control_proxy_setup.bmRequestType & 0x80) == 0 ?
        ((uint32_t)control_proxy_buffer[0] << 24) |
        ((uint32_t)control_proxy_buffer[1] << 16) |
        ((uint32_t)control_proxy_buffer[2] << 8) |
        control_proxy_buffer[3] : 0);
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
    uint16_t trace_length = MIN(output_proxy_length,
                                sizeof(output_trace_buffer));
    if(!output_trace_valid || output_trace_length != output_proxy_length ||
       memcmp(output_trace_buffer, output_proxy_buffer, trace_length) != 0)
    {
      uint32_t prefix_a = 0;
      uint32_t prefix_b = 0;
      uint8_t index;

      memcpy(output_trace_buffer, output_proxy_buffer, trace_length);
      output_trace_length = output_proxy_length;
      output_trace_valid = 1;
      for(index = 0; index < trace_length && index < 4; index++)
      {
        prefix_a = (prefix_a << 8) | output_proxy_buffer[index];
      }
      for(; index < trace_length; index++)
      {
        prefix_b = (prefix_b << 8) | output_proxy_buffer[index];
      }
      diagnostic_trace_event("OUT_FWD", output_proxy_length,
                             prefix_a, prefix_b);
    #ifdef RUMBLE_OLED_TRACE
      rumble_trace_display(output_proxy_buffer, output_proxy_length);
    #endif
    }
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
#endif


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

  startup_screen_drawn = 0;
}

uint8_t wk_usb_device_init(void)
{
#ifdef USB_UPSTREAM_ISOLATION_TEST
  console_device_connected = 0;
  qualifier_probe_state = 0;
  custom_hid_device_qualifier_clear();
  return 1;
#else
  usbd_desc_t *device_descriptor;
  usbd_desc_t *config_descriptor;

  /* FS2 is the console-facing Xbox device on PB14/PB15. */
  usbd_init(&otg_core_struct_fs2,
            USB_FULL_SPEED_CORE_ID,
            USB_OTG2_ID,
            &custom_hid_class_handler,
            &custom_hid_desc_handler);
  usbd_disconnect(&otg_core_struct_fs2.dev);

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

  console_device_connected = 0;
  console_session_ready = 0;
  upstream_retry_disconnected = 0;
  upstream_enumeration_active = 0;
  upstream_retry_time = 0;
  custom_hid_upstream_reset_clear();
  return 1;
#endif
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
#ifdef USB_DIAGNOSTICS
  static uint8_t previous_host_state = 0xFF;
  static uint8_t previous_enum_state = 0xFF;
  static uint8_t previous_device_state = 0xFF;
#endif
  /* add user code begin usb_app_task 0 */

  /* add user code end usb_app_task 0 */

  /* add user code begin usb_app_task 1 */

  /* add user code end usb_app_task 1 */

  if(!startup_screen_drawn)
  {
    usb_show_startup_screen();
    startup_screen_drawn = 1;
  }

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

  /* Enumerate the physical controller before exposing Sleepus to the Xbox. */
#ifdef USB_DOWNSTREAM_ISOLATION_TEST
  return;
#else
  usbh_loop_handler(&otg_core_struct_fs1.host);
  if(config_status == USB_CONFIG_WAIT_STORAGE ||
     config_status == USB_CONFIG_WORKING)
  {
    usb_config_task();
    return;
  }
#ifdef USB_DIAGNOSTICS
  if(previous_host_state != otg_core_struct_fs1.host.global_state)
  {
    previous_host_state = otg_core_struct_fs1.host.global_state;
    diagnostic_trace_event("HOST_STATE", previous_host_state,
                         otg_core_struct_fs1.host.enum_state,
                         otg_core_struct_fs1.host.ctrl.state);
  }
  if(otg_core_struct_fs1.host.global_state == USBH_ENUMERATION &&
     previous_enum_state != otg_core_struct_fs1.host.enum_state)
  {
    previous_enum_state = otg_core_struct_fs1.host.enum_state;
    diagnostic_trace_event("HOST_ENUM", previous_enum_state,
                         otg_core_struct_fs1.host.ctrl.state,
                         otg_core_struct_fs1.host.req_state);
  }
  if(console_device_connected &&
     previous_device_state != otg_core_struct_fs2.dev.conn_state)
  {
    previous_device_state = otg_core_struct_fs2.dev.conn_state;
    diagnostic_trace_event("UP_STATE", previous_device_state,
                         otg_core_struct_fs2.dev.dev_config,
                         otg_core_struct_fs2.dev.device_addr);
  }
#endif
#if defined(USB_UPSTREAM_ISOLATION_TEST) || defined(USB_UPSTREAM_NO_ATTACH_TEST)
  return;
#else
  if(!console_device_connected)
  {
    usbh_core_type *host = &otg_core_struct_fs1.host;

    if(host->global_state != USBH_CLASS || !usbh_controller_report_seen())
    {
      qualifier_probe_state = 0;
      custom_hid_device_qualifier_clear();
      return;
    }
    if(qualifier_probe_state == 0)
    {
      custom_hid_desc_set_device(&host->dev.dev_desc);
      if(!custom_hid_desc_set_configuration(
           host->dev.raw_configuration,
           host->dev.raw_configuration_length))
      {
        diagnostic_log_event("CFG_MIRROR_FAIL",
                             host->dev.raw_configuration_length,
                             host->dev.raw_configuration[0],
                             host->dev.raw_configuration[1]);
        return;
      }
      diagnostic_trace_event("DEV_MIRROR",
        ((uint32_t)host->dev.dev_desc.bcdUSB << 16) |
        host->dev.dev_desc.bcdDevice,
        ((uint32_t)host->dev.dev_desc.idVendor << 16) |
        host->dev.dev_desc.idProduct,
        ((uint32_t)host->dev.dev_desc.bDeviceClass << 24) |
        ((uint32_t)host->dev.dev_desc.bDeviceSubClass << 16) |
        ((uint32_t)host->dev.dev_desc.bDeviceProtocol << 8) |
        host->dev.dev_desc.bMaxPacketSize0);
      custom_hid_device_qualifier_set(host->dev.qualifier,
                                      host->dev.qualifier_length,
                                      host->dev.qualifier_supported);
      custom_hid_os_string_set(host->dev.os_string,
               host->dev.os_string_length,
               host->dev.os_string_supported);
      diagnostic_trace_event("QUAL_ENUM",
                           host->dev.qualifier_supported,
                           host->dev.qualifier_length,
                           host->dev.qualifier_length >= 4 ?
                             ((uint32_t)host->dev.qualifier[0] << 24) |
                             ((uint32_t)host->dev.qualifier[1] << 16) |
                             ((uint32_t)host->dev.qualifier[2] << 8) |
                             host->dev.qualifier[3] : 0);
      qualifier_probe_state = 2;
      return;
    }
    if(host->ctrl.state != CONTROL_IDLE || host->req_state != CMD_SEND)
    {
      return;
    }
    diagnostic_log_event("UP_ATTACH", host->global_state,
               host->ctrl.state, host->req_state);
    usbd_connect(&otg_core_struct_fs2.dev);
    console_device_connected = 1;
    upstream_retry_disconnected = 0;
    upstream_enumeration_active = 0;
    upstream_retry_time = get_system_tick();
  }
  if(!console_device_connected)
  {
    return;
  }
  if(custom_hid_upstream_wakeup_take() &&
     otg_core_struct_fs1.host.global_state == USBH_CLASS &&
     !usbh_controller_report_recent(&otg_core_struct_fs1.host, 1000U))
  {
    diagnostic_log_event("CTRL_WAKE", get_system_tick(),
                         otg_core_struct_fs1.host.timer, 0);
    usb_controller_reenumerate();
    return;
  }
  if(usbd_connect_state_get(&otg_core_struct_fs2.dev) !=
       USB_CONN_STATE_CONFIGURED)
  {
    uint32_t current_time = get_system_tick();

    if(custom_hid_upstream_reset_seen() && !upstream_enumeration_active)
    {
      upstream_enumeration_active = 1;
      upstream_retry_time = current_time;
      diagnostic_log_event("UP_ENUM_START", current_time, 0, 0);
    }
    if(upstream_enumeration_active &&
       (uint32_t)(current_time - upstream_retry_time) >=
         UPSTREAM_ENUM_TIMEOUT_MS)
    {
      usbd_disconnect(&otg_core_struct_fs2.dev);
      custom_hid_upstream_reset_clear();
      upstream_retry_disconnected = 1;
      upstream_enumeration_active = 0;
      upstream_retry_time = current_time;
      diagnostic_log_event("UP_ENUM_TIMEOUT", current_time,
                           otg_core_struct_fs2.dev.conn_state,
                           otg_core_struct_fs2.dev.dev_config);
    }
    else if(!upstream_enumeration_active &&
            !upstream_retry_disconnected &&
       (uint32_t)(current_time - upstream_retry_time) >=
         UPSTREAM_RETRY_INTERVAL_MS)
    {
      usbd_disconnect(&otg_core_struct_fs2.dev);
      upstream_retry_disconnected = 1;
      upstream_retry_time = current_time;
      diagnostic_log_event("UP_RETRY_OFF", current_time, 0, 0);
    }
    else if(upstream_retry_disconnected &&
            (uint32_t)(current_time - upstream_retry_time) >=
              UPSTREAM_DISCONNECT_MS)
    {
      usbd_connect(&otg_core_struct_fs2.dev);
      upstream_retry_disconnected = 0;
      upstream_retry_time = current_time;
      diagnostic_log_event("UP_RETRY_ON", current_time, 0, 0);
    }
    if(upstream_retry_disconnected)
    {
      return;
    }
  }
#ifndef USB_UPSTREAM_ATTACH_NO_PROXY_TEST
  xbox_speaker_proxy_task();
  xbox_control_proxy_task();
  xbox_output_proxy_task();
#endif
#endif
#endif

  /* add user code begin usb_app_task 2 */

  /* add user code end usb_app_task 2 */
}

uint8_t usb_device_configured(void)
{
  uint8_t upstream_session_active = custom_hid_upstream_session_active();
  uint8_t upstream_suspended =
    usb_suspend_status_get(otg_core_struct_fs2.dev.usb_reg);
  uint8_t upstream_session_valid = console_device_connected &&
    !upstream_retry_disconnected && upstream_session_active &&
    usbd_connect_state_get(&otg_core_struct_fs2.dev) ==
      USB_CONN_STATE_CONFIGURED &&
    upstream_suspended == 0;
#ifdef USB_DIAGNOSTICS
  static uint8_t previous_session_ready = 0xFF;
#endif

  if(!upstream_session_valid)
  {
    console_session_ready = 0;
  }
  else if(usbh_controller_report_recent(&otg_core_struct_fs1.host, 1000U))
  {
    console_session_ready = 1;
  }
#ifdef USB_DIAGNOSTICS
  if(console_session_ready != previous_session_ready)
  {
    diagnostic_log_event("UP_READY", console_session_ready,
      upstream_session_valid,
      (uint32_t)usbd_connect_state_get(&otg_core_struct_fs2.dev) |
      ((uint32_t)upstream_suspended << 8) |
      ((uint32_t)console_device_connected << 16) |
      ((uint32_t)upstream_session_active << 17));
    previous_session_ready = console_session_ready;
  }
#endif
  return console_session_ready;
}

uint8_t usb_device_send_report(const uint8_t *report, uint16_t length)
{
#if defined(USB_UPSTREAM_ISOLATION_TEST) || \
  defined(USB_UPSTREAM_NO_ATTACH_TEST) || \
  defined(USB_UPSTREAM_ATTACH_NO_PROXY_TEST)
  (void)report;
  (void)length;
  return 0;
#else
  if(upstream_retry_disconnected)
  {
    return 0;
  }
  return custom_hid_class_send_report(&otg_core_struct_fs2.dev,
                                      (uint8_t *)report, length) == USB_OK;
#endif
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

#ifndef USB_DOWNSTREAM_ISOLATION_TEST
  usbh_irq_handler(&otg_core_struct_fs1);
  usbh_audio_irq(&otg_core_struct_fs1.host);
#if !defined(USB_UPSTREAM_ISOLATION_TEST) && \
    !defined(USB_UPSTREAM_NO_ATTACH_TEST) && \
    !defined(USB_UPSTREAM_ATTACH_NO_PROXY_TEST)
  xbox_microphone_proxy_task();
#endif
#endif

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

#ifndef USB_UPSTREAM_ISOLATION_TEST
  usbd_irq_handler(&otg_core_struct_fs2);
#endif

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
