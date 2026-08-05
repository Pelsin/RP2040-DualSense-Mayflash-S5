#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C"
{
#endif

#define BOARD_TUD_RHPORT 0
#define BOARD_TUH_RHPORT 1
#define BOARD_TUD_MAX_SPEED OPT_MODE_FULL_SPEED
#define BOARD_TUH_MAX_SPEED OPT_MODE_FULL_SPEED

#ifndef CFG_TUSB_MCU
#error CFG_TUSB_MCU must be defined
#endif

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS OPT_OS_PICO
#endif

#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG 0
#endif

#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif
#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN __attribute__((aligned(4)))
#endif

#define CFG_TUD_ENABLED 1
#define CFG_TUD_MAX_SPEED BOARD_TUD_MAX_SPEED

#ifndef CFG_TUD_ENDPOINT0_SIZE
#define CFG_TUD_ENDPOINT0_SIZE 64
#endif

#define CFG_TUD_HID 1
#define CFG_TUD_CDC 0
#define CFG_TUD_MSC 0
#define CFG_TUD_MIDI 0
#define CFG_TUD_VENDOR 0

#define CFG_TUD_HID_EP_BUFSIZE 64

#define CFG_TUH_ENABLED 1
#define CFG_TUH_MAX_SPEED BOARD_TUH_MAX_SPEED
#define CFG_TUH_RPI_PIO_USB 1
#define TUH_OPT_RHPORT 1

#define CFG_TUH_ENUMERATION_BUFSIZE 512

#define CFG_TUH_HUB 0
#define CFG_TUH_DEVICE_MAX 1

#define CFG_TUH_HID 2 // dongle may expose >1 HID interface
#define CFG_TUH_HID_EPIN_BUFSIZE 64
#define CFG_TUH_HID_EPOUT_BUFSIZE 64

#ifdef __cplusplus
}
#endif

#endif /* _TUSB_CONFIG_H_ */
