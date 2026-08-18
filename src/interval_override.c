// Definition of the `interval_override` symbol referenced by the OpenStickCommunity
// Pico-PIO-USB fork. 0 = use each endpoint's descriptor bInterval unchanged.
#include "interval_override.h"

volatile uint8_t interval_override = 0;
