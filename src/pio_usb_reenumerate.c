#include "pio_usb_ll.h"

void pio_usb_host_force_reenumerate(uint8_t root_idx) {
  root_port_t *root = PIO_USB_ROOT_PORT(root_idx);
  root->connected = false;
  root->suspended = true;
  root->ints |= PIO_USB_INTS_DISCONNECT_BITS;
}
