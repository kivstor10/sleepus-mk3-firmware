#include "ff.h"
#include "diskio.h"
#include "usb_core.h"
#include "usb_conf.h"
#include "usbh_msc_class.h"

extern otg_core_type otg_core_struct_fs1;

static DSTATUS disk_state = STA_NOINIT;

DSTATUS disk_initialize(BYTE drive)
{
  if(drive != 0U || usbh_msc_is_ready(&otg_core_struct_fs1.host, 0) != MSC_OK ||
     usbh_msc.l_unit_n[0].capacity.blk_size != 512U)
  {
    disk_state = STA_NOINIT;
    return disk_state;
  }
  disk_state = 0;
  return disk_state;
}

DSTATUS disk_status(BYTE drive)
{
  if(drive != 0U || usbh_msc_is_ready(&otg_core_struct_fs1.host, 0) != MSC_OK ||
     usbh_msc.l_unit_n[0].capacity.blk_size != 512U)
  {
    return STA_NOINIT;
  }
  return disk_state;
}

DRESULT disk_read(BYTE drive, BYTE *buffer, LBA_t sector, UINT count)
{
  if(drive != 0U || buffer == 0 || count == 0U || disk_status(drive) != 0U)
  {
    return RES_PARERR;
  }
  return usbh_msc_read(&otg_core_struct_fs1.host, (uint32_t)sector,
                       count, buffer, 0) == USB_OK ? RES_OK : RES_ERROR;
}

#if FF_FS_READONLY == 0
DRESULT disk_write(BYTE drive, const BYTE *buffer, LBA_t sector, UINT count)
{
  if(drive != 0U || buffer == 0 || count == 0U || disk_status(drive) != 0U)
  {
    return RES_PARERR;
  }
  return usbh_msc_write(&otg_core_struct_fs1.host, (uint32_t)sector,
                        count, (BYTE *)buffer, 0) == USB_OK ?
    RES_OK : RES_ERROR;
}
#endif

DRESULT disk_ioctl(BYTE drive, BYTE command, void *buffer)
{
  if(drive != 0U || disk_status(drive) != 0U)
  {
    return RES_NOTRDY;
  }
  switch(command)
  {
    case CTRL_SYNC:
      return RES_OK;
    case GET_SECTOR_COUNT:
      if(buffer == 0)
      {
        return RES_PARERR;
      }
      *(LBA_t *)buffer = (LBA_t)usbh_msc.l_unit_n[0].capacity.blk_nbr + 1U;
      return RES_OK;
    case GET_SECTOR_SIZE:
      if(buffer == 0)
      {
        return RES_PARERR;
      }
      *(WORD *)buffer = (WORD)usbh_msc.l_unit_n[0].capacity.blk_size;
      return RES_OK;
    case GET_BLOCK_SIZE:
      if(buffer == 0)
      {
        return RES_PARERR;
      }
      *(DWORD *)buffer = 1U;
      return RES_OK;
    default:
      return RES_PARERR;
  }
}