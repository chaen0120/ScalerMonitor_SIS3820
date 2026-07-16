#include <libxxusb.h>
#include <stdio.h>

int main(void)
{
  xxusb_device_type devices[100];
  struct usb_device *dev;
  usb_dev_handle *udev;
  short AM = 0x09;
  long id, base;
  short ret;

  xxusb_devices_find(devices);
  dev = devices[0].usbdev;
  udev = xxusb_device_open(dev);
  if (!udev) {
    printf("Failed to open VM-USB\n");
    return 1;
  }
  printf("Device open\n");

  for (base = 0; base <= 0xF0000000L; base += 0x01000000L) {
    id = 0;
    ret = VME_read_32(udev, AM, base + 0x04, &id);
    printf("base=0x%08lx ret=%d id=0x%08lx\n", base, ret, id);
    /* watch SIS3820 Access (A) LED while this runs */
  }

  xxusb_device_close(udev);
  return 0;
}
