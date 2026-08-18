#ifndef _INTERVAL_OVERRIDE_H_
#define _INTERVAL_OVERRIDE_H_

#include <stdint.h>

// Referenced by the OpenStickCommunity Pico-PIO-USB fork (pio_usb_host.c) to
// override the polling interval of host endpoints. Defined in
// src/interval_override.c.
extern volatile uint8_t interval_override;

#endif
