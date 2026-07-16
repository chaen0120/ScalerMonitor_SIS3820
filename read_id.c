#include <libxxusb.h>
#include <stdio.h>

int main(void)
{
  xxusb_device_type devices[100];
  struct usb_device *dev;
  usb_dev_handle *udev;
  long base = 0x88000000;
  short AM = 0x09;          /* A32 user data — same as your scan */
  long id = 0, zero = 0, acq;
  long counts[32];
  short ret;
  int i;

  xxusb_devices_find(devices);
  udev = xxusb_device_open(devices[0].usbdev);
  if (!udev) {
    printf("Failed to open VM-USB\n");
    return 1;
  }
  printf("Device open\n");

  /* --- identify --- */
  xxusb_reset_toggle(udev);
  ret = VME_read_32(udev, AM, base + 0x04, &id); /* trust this one */
  if (ret < 0) {
      xxusb_reset_toggle(udev);
      ret = VME_read_32(udev, AM, base + 0x04, &id);
  }
  printf("ID ret=%d  ID=0x%08lx\n", ret, id);
  if ((id & 0xffff0000) != 0x38200000) {
    printf("Not SIS3820\n");
    xxusb_device_close(udev);
    return 1;
  }
  printf("Looks like an SIS3820\n");

  /* --- init (once) --- */
  acq = 0x00000000   /* 32-bit */
      | 0x00000000   /* LNE from VME */
      | 0x00000000   /* arm with FP */
      | 0x00001000   /* SRAM */
      | 0x00040000   /* input mode: inhibit banks of 4 */
      | 0x00100000   /* 50 MHz out */
      | 0x00000000;  /* latch mode, clear-on-latch */

  VME_write_32(udev, AM, base + 0x400, zero);  /* reset */
  VME_write_32(udev, AM, base + 0x100, acq);   /* acq mode */
  VME_write_32(udev, AM, base + 0x404, zero);  /* fifo reset */
  VME_write_32(udev, AM, base + 0x40C, zero);  /* clear counters */
  VME_write_32(udev, AM, base + 0x414, zero);  /* arm */
  VME_write_32(udev, AM, base + 0x418, zero);  /* enable — S LED may come on */

  /* --- readout --- */
  VME_write_32(udev, AM, base + 0x410, zero);  /* LNE / latch */

  for (i = 0; i < 32; i++) {
    ret = VME_read_32(udev, AM, base + 0x800 + 4 * i, &counts[i]);
    printf("ch%02d = %lu  (ret=%d)\n", i + 1,
           (unsigned long)counts[i], ret);
  }

  xxusb_device_close(udev);
  return 0;
}
