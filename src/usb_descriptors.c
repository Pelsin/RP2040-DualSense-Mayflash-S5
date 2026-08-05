#include "tusb.h"
#include "device/usbd_pvt.h"
#include "shared_state.h"
#include <string.h>

//--------------------------------------------------------------------
// Device descriptor — Sony DualSense (VID 054C / PID 0CE6)
//--------------------------------------------------------------------
static const uint8_t desc_device[] = {
    0x12,        // bLength
    0x01,        // bDescriptorType (Device)
    0x00, 0x02,  // bcdUSB 2.00
    0x00,        // bDeviceClass
    0x00,        // bDeviceSubClass
    0x00,        // bDeviceProtocol
    0x40,        // bMaxPacketSize0 64
    0x4C, 0x05,  // idVendor 0x054C
    0xE6, 0x0C,  // idProduct 0x0CE6
    0x00, 0x01,  // bcdDevice 1.00
    0x01,        // iManufacturer
    0x02,        // iProduct
    0x00,        // iSerialNumber
    0x01,        // bNumConfigurations
};

//--------------------------------------------------------------------
// HID report descriptor (DualSense hybrid)
//--------------------------------------------------------------------
static const uint8_t desc_hid_report[] = {
    0x05, 0x01, 0x09, 0x05, 0xA1, 0x01, 0x85, 0x01, 0x09, 0x30, 0x09, 0x31,
    0x09, 0x32, 0x09, 0x35, 0x09, 0x33, 0x09, 0x34, 0x15, 0x00, 0x26, 0xFF,
    0x00, 0x75, 0x08, 0x95, 0x06, 0x81, 0x02, 0x06, 0x00, 0xFF, 0x09, 0x20,
    0x95, 0x01, 0x81, 0x02, 0x05, 0x01, 0x09, 0x39, 0x15, 0x00, 0x25, 0x07,
    0x35, 0x00, 0x46, 0x3B, 0x01, 0x65, 0x14, 0x75, 0x04, 0x95, 0x01, 0x81,
    0x42, 0x65, 0x00, 0x05, 0x09, 0x19, 0x01, 0x29, 0x0F, 0x15, 0x00, 0x25,
    0x01, 0x75, 0x01, 0x95, 0x0F, 0x81, 0x02, 0x06, 0x00, 0xFF, 0x09, 0x21,
    0x95, 0x0D, 0x81, 0x02, 0x06, 0x00, 0xFF, 0x09, 0x22, 0x15, 0x00, 0x26,
    0xFF, 0x00, 0x75, 0x08, 0x95, 0x34, 0x81, 0x02, 0x85, 0x02, 0x09, 0x23,
    0x95, 0x2F, 0x91, 0x02, 0x85, 0x05, 0x09, 0x33, 0x95, 0x28, 0xB1, 0x02,
    0x85, 0x08, 0x09, 0x34, 0x95, 0x2F, 0xB1, 0x02, 0x85, 0x09, 0x09, 0x24,
    0x95, 0x13, 0xB1, 0x02, 0x85, 0x0A, 0x09, 0x25, 0x95, 0x1A, 0xB1, 0x02,
    0x85, 0x20, 0x09, 0x26, 0x95, 0x3F, 0xB1, 0x02, 0x85, 0x21, 0x09, 0x27,
    0x95, 0x04, 0xB1, 0x02, 0x85, 0x22, 0x09, 0x40, 0x95, 0x3F, 0xB1, 0x02,
    0x85, 0x80, 0x09, 0x28, 0x95, 0x3F, 0xB1, 0x02, 0x85, 0x81, 0x09, 0x29,
    0x95, 0x3F, 0xB1, 0x02, 0x85, 0x82, 0x09, 0x2A, 0x95, 0x09, 0xB1, 0x02,
    0x85, 0x83, 0x09, 0x2B, 0x95, 0x3F, 0xB1, 0x02, 0x85, 0x84, 0x09, 0x2C,
    0x95, 0x3F, 0xB1, 0x02, 0x85, 0x85, 0x09, 0x2D, 0x95, 0x02, 0xB1, 0x02,
    0x85, 0xA0, 0x09, 0x2E, 0x95, 0x01, 0xB1, 0x02, 0x85, 0xE0, 0x09, 0x2F,
    0x95, 0x3F, 0xB1, 0x02, 0x85, 0xF0, 0x09, 0x30, 0x95, 0x3F, 0xB1, 0x02,
    0x85, 0xF1, 0x09, 0x31, 0x95, 0x3F, 0xB1, 0x02, 0x85, 0xF2, 0x09, 0x32,
    0x95, 0x0F, 0xB1, 0x02, 0x85, 0xF4, 0x09, 0x35, 0x95, 0x3F, 0xB1, 0x02,
    0x85, 0xF5, 0x09, 0x36, 0x95, 0x03, 0xB1, 0x02, 0xC0,
};

//--------------------------------------------------------------------
// Configuration descriptor
//--------------------------------------------------------------------
// Full DualSense composite (227 B): audio-control + 2 audio-streaming + HID.
// The audio interfaces are a no-op (see the audio-noop driver below) but MUST be
// present -- the PS5 fingerprints this wired and rejects a HID-only device.
static const uint8_t desc_configuration[] = {
    0x09, 0x02, 0xE3, 0x00, 0x04, 0x01, 0x00, 0xC0, 0xFA, // config: 4 itf, 227 B
    // -- Interface 0: Audio Control --
    0x09, 0x04, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00,
    0x0A, 0x24, 0x01, 0x00, 0x01, 0x49, 0x00, 0x02, 0x01, 0x02,
    0x0C, 0x24, 0x02, 0x01, 0x01, 0x01, 0x06, 0x04, 0x33, 0x00, 0x00, 0x00,
    0x0C, 0x24, 0x06, 0x02, 0x01, 0x01, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x09, 0x24, 0x03, 0x03, 0x01, 0x03, 0x04, 0x02, 0x00,
    0x0C, 0x24, 0x02, 0x04, 0x02, 0x04, 0x03, 0x02, 0x03, 0x00, 0x00, 0x00,
    0x09, 0x24, 0x06, 0x05, 0x04, 0x01, 0x03, 0x00, 0x00,
    0x09, 0x24, 0x03, 0x06, 0x01, 0x01, 0x01, 0x05, 0x00,
    // -- Interface 1: Audio Streaming OUT (alt0 zero-bw, alt1 iso ep 0x01) --
    0x09, 0x04, 0x01, 0x00, 0x00, 0x01, 0x02, 0x00, 0x00,
    0x09, 0x04, 0x01, 0x01, 0x01, 0x01, 0x02, 0x00, 0x00,
    0x07, 0x24, 0x01, 0x01, 0x01, 0x01, 0x00,
    0x0B, 0x24, 0x02, 0x01, 0x04, 0x02, 0x10, 0x01, 0x80, 0xBB, 0x00,
    0x09, 0x05, 0x01, 0x09, 0xC4, 0x00, 0x04, 0x00, 0x00,
    0x07, 0x25, 0x01, 0x00, 0x00, 0x00, 0x00,
    // -- Interface 2: Audio Streaming IN (alt0 zero-bw, alt1 iso ep 0x82) --
    0x09, 0x04, 0x02, 0x00, 0x00, 0x01, 0x02, 0x00, 0x00,
    0x09, 0x04, 0x02, 0x01, 0x01, 0x01, 0x02, 0x00, 0x00,
    0x07, 0x24, 0x01, 0x06, 0x01, 0x01, 0x00,
    0x0B, 0x24, 0x02, 0x01, 0x01, 0x02, 0x10, 0x01, 0x80, 0xBB, 0x00,
    0x09, 0x05, 0x82, 0x05, 0x62, 0x00, 0x04, 0x00, 0x00,
    0x07, 0x25, 0x01, 0x00, 0x00, 0x00, 0x00,
    // -- Interface 3: HID gamepad -- IN ep 0x84, OUT ep 0x05 --
    0x09, 0x04, 0x03, 0x00, 0x02, 0x03, 0x00, 0x00, 0x00,
    0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22,
    (uint8_t)(sizeof(desc_hid_report) & 0xFF),
    (uint8_t)((sizeof(desc_hid_report) >> 8) & 0xFF),
    0x07, 0x05, 0x84, 0x03, 0x40, 0x00, 0x06,
    0x07, 0x05, 0x05, 0x03, 0x40, 0x00, 0x06,
};

//--------------------------------------------------------------------
// String descriptors
//--------------------------------------------------------------------
static const char *string_desc_arr[] = {
    (const char[]){0x09, 0x04},          // 0: language (English)
    "Sony Interactive Entertainment",    // 1: Manufacturer
    "DualSense Wireless Controller",     // 2: Product
};

//--------------------------------------------------------------------
// TinyUSB callbacks
//--------------------------------------------------------------------
const uint8_t *tud_descriptor_device_cb(void) {
    return desc_device;
}

const uint8_t *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return desc_configuration;
}

const uint8_t *tud_hid_descriptor_report_cb(uint8_t instance) {
    (void)instance;
    return desc_hid_report;
}

static uint16_t _desc_str[32];
const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    uint8_t chr_count;

    if (index == 0) {
        memcpy(&_desc_str[1], string_desc_arr[0], 2);
        chr_count = 1;
    } else {
        if (index >= (sizeof(string_desc_arr) / sizeof(string_desc_arr[0])))
            return NULL;
        const char *str = string_desc_arr[index];
        chr_count = (uint8_t)strlen(str);
        if (chr_count > 31) chr_count = 31;
        for (uint8_t i = 0; i < chr_count; i++) _desc_str[1 + i] = str[i];
    }

    _desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
    return _desc_str;
}

// Device-info FEATURE reports the PS5 reads to accept us. 0x03 is static;
// 0x05/0x09/0x20 come from the dongle once fetched (static fallback until then).
// 0x03 Feature definition (static)
static const uint8_t output_0x03[] = {
    0x21, 0x28, 0x03, 0xC3, 0x00, 0x2C, 0x56,
    0x01, 0x00, 0xD0, 0x07, 0x00, 0x80, 0x04, 0x00,
    0x00, 0x80, 0x0D, 0x0D, 0x84, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
// 0x05 Sensor calibration (static fallback)
static const uint8_t output_0x05[] = {
    0xff, 0xff, 0xf4, 0xff, 0xfb, 0xff, 0x92, 0x22,
    0x6a, 0xdd, 0x8d, 0x22, 0x5d, 0xdd, 0x9b, 0x22,
    0x65, 0xdd, 0x1c, 0x02, 0x1c, 0x02, 0xd2, 0x1f,
    0xf2, 0xdf, 0xd0, 0x1f, 0xb7, 0xdf, 0x04, 0x20,
    0xfc, 0xdf, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00,
};
// 0x09 Pair info / MAC (static fallback)
static const uint8_t output_0x09[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
// 0x20 Firmware version (static fallback)
static const uint8_t output_0x20[] = {
    0x4a, 0x75, 0x6e, 0x20, 0x32, 0x34, 0x20, 0x32,
    0x30, 0x32, 0x34, 0x31, 0x31, 0x3a, 0x31, 0x36,
    0x3a, 0x32, 0x31, 0x03, 0x00, 0x04, 0x00, 0x13,
    0x03, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x01, 0x41,
    0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x20, 0x05, 0x00, 0x00, 0x2a,
    0x00, 0x01, 0x00, 0x0a, 0x00, 0x02, 0x00, 0x06,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

// A current DualSense firmware build for 0x20 -- the dongle's own build is old
// enough that the PS5 offers a firmware update instead of authing, so we swap it.
static const uint8_t dualsense_current_0x20[63] = {
    0x4A, 0x75, 0x6C, 0x20, 0x20, 0x34, 0x20, 0x32, 0x30, 0x32, 0x35,
    0x31, 0x30, 0x3A, 0x31, 0x30, 0x3A, 0x33, 0x32,
    0x03, 0x00, 0x04, 0x00, 0x14, 0x05, 0x00, 0x00,
    0x2A, 0x00, 0x10, 0x01, 0xC1, 0x48, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x30, 0x06, 0x00, 0x00, 0x38, 0x00, 0x01,
    0x00, 0x0A, 0x00, 0x02, 0x00, 0x06,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type,
                               uint8_t *buffer, uint16_t reqlen) {
    (void)instance; (void)reqlen;
    if (report_type != HID_REPORT_TYPE_FEATURE) return 0;

    // ---- PS5 native auth reads, served from the dongle caches (see dongle.cpp) ----
    if (report_id == 0xF2) {            // signing state
        if (g_state.auth_f1_ready)            memcpy(buffer, g_state.auth_f2, 15);
        else if (g_state.auth_f2_have_notready) memcpy(buffer, g_state.auth_f2_notready, 15);
        else return 0;                 // nothing cached yet -> STALL, the PS5 retries
        return 15;
    }
    if (report_id == 0xF1) {           // signature nonce (4 chunks, in order)
        if (!g_state.auth_f1_ready) return 0;   // not ready -> STALL
        uint8_t idx = g_state.auth_f1_read_idx; if (idx > 3) idx = 3;
        memcpy(buffer, g_state.auth_f1[idx], 63);
        if (g_state.auth_f1_read_idx < 3) g_state.auth_f1_read_idx++;
        return 63;
    }

    switch (report_id) {
        case 0x03:
            memcpy(buffer, output_0x03, sizeof(output_0x03));
            return sizeof(output_0x03);
        case 0x05:
            memcpy(buffer, g_state.reports_ready ? g_state.calibration_report
                                                 : output_0x05, 40);
            return 40;
        case 0x09:
            memcpy(buffer, g_state.reports_ready ? g_state.mac_pair_report
                                                 : output_0x09, 15);
            return 15;
        case 0x20:
            // Substitute a current firmware build so the PS5 doesn't demand an update.
            memcpy(buffer, dualsense_current_0x20, sizeof(dualsense_current_0x20));
            return 63;
        default:
            return 0;   // includes the deferred F0/F1/F2 auth reports
    }
}

// DualSense OUTPUT report 0x02 field offsets, indexed on the FULL report
// (byte[0] = report id). valid_flag1 (byte[2]) gates which fields to apply.
#define DS_OUT_REPORT_ID     0x02
#define DS_OUT_VALIDFLAG1    2     // bit2 = LED color, bit4 = player indicators
#define DS_OUT_VALIDFLAG2    39    // bit1 = lightbar setup / brightness
#define DS_OUT_LIGHTBAR_SETUP 42   // 0x02 = fade lightbar off
#define DS_OUT_LED_BRIGHTNESS 43
#define DS_OUT_PLAYER_LEDS   44
#define DS_OUT_LIGHTBAR_R    45
#define DS_OUT_LIGHTBAR_G    46
#define DS_OUT_LIGHTBAR_B    47
#define DS_FLAG1_LED_COLOR   0x04
#define DS_FLAG1_PLAYER_LEDS 0x10
// led_brightness enable bit in valid_flag2. Sources disagree: the PS5 / Linux
// kernel use bit1 (LIGHTBAR_SETUP_CONTROL_ENABLE); ds.daidr.me's WebHID tester
// uses bit0. Accept either so brightness works from both.
#define DS_FLAG2_BRIGHTNESS_EN 0x03    // bit0 (ds.daidr.me) | bit1 (PS5/kernel)

// Map the PS5's brightness field to a 0..255 RGB scale. Never returns 0 -- a
// brightness update must not black the LEDs out.
static uint8_t ds_brightness_scale(uint8_t brightness) {
    switch (brightness) {
        case 0: return 255;
        case 1: return 160;
        case 2: return 70;
        default: return brightness;   // >2 => already a nonzero magnitude
    }
}

// Decode the DualSense player-indicator bitmask (lower 5 bits) into a slot 1..4;
// 0 = unrecognized pattern.
static uint8_t ds_decode_player(uint8_t player_leds) {
    switch (player_leds & 0x1F) {
        case 0x04: return 1;   // 0b00100 center
        case 0x0A: return 2;   // 0b01010
        case 0x15: return 3;   // 0b10101
        case 0x1B: return 4;   // 0b11011
        default:   return 0;
    }
}

// HID SET_REPORT from the console. FEATURE writes (pairing 0x08/0x0A, auth 0xF0)
// get relayed to the dongle; the OUTPUT report (0x02) drives our LEDs/OLED.
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t bufsize) {
    (void)instance;

    if (report_type == HID_REPORT_TYPE_FEATURE) {
        // Queue the console's pairing/auth writes for core1 to relay verbatim --
        // without this the dongle's BT link key is wrong ("incorrect passkey").
        if (report_id == 0x08 || report_id == 0x0A || report_id == 0xF0) {
            uint8_t head = g_state.set_q_head;
            uint8_t next = (uint8_t)((head + 1) % PS5_SET_QUEUE_DEPTH);
            if (next != g_state.set_q_tail) {            // drop if the ring is full
                ps5_pending_set_t *s = &g_state.set_queue[head];
                uint16_t n = bufsize; if (n > 63) n = 63;
                s->report_id = report_id;
                s->data[0] = report_id;                  // echo id at [0] (0x80 pattern)
                for (uint16_t i = 0; i < n; i++) s->data[1 + i] = buffer[i];
                s->len = (uint16_t)(n + 1);
                g_state.set_q_head = next;               // publish to core1
            }
        }
        // F0 bookkeeping ([0]=type, [2]=index): type 0x01 is 4 chunks, else one.
        // Reset caches on the first chunk; on the last, let core1 start polling F2.
        if (report_id == 0xF0 && bufsize >= 3) {
            uint8_t atype = buffer[0], aindex = buffer[2];
            bool first    = (atype == 0x01) ? (aindex == 0) : true;
            bool terminal = (atype == 0x01) ? (aindex == 3) : true;
            if (first) {
                g_state.auth_type = atype;
                g_state.auth_f1_ready = false;
                g_state.auth_f1_read_idx = 0;
                g_state.auth_f2_have_notready = false;
            }
            if (terminal) g_state.auth_ready_to_poll = true;
        }
        return;
    }

    if (report_type != HID_REPORT_TYPE_OUTPUT) return;

    static uint8_t full[64];
    const uint8_t *rep;
    uint16_t replen;
    if (report_id == 0) {                 // interrupt-OUT: id is buffer[0]
        rep = buffer;
        replen = bufsize;
    } else {                              // control: id stripped -> re-prepend it
        if (bufsize + 1 > (uint16_t)sizeof(full)) return;
        full[0] = report_id;
        memcpy(&full[1], buffer, bufsize);
        rep = full;
        replen = bufsize + 1;
    }

    if (replen == 0 || rep[0] != DS_OUT_REPORT_ID) return;

    // connected_bt isn't derived here -- it mirrors our own boot-time mode choice.
    g_state.have_output = true;

    if (replen <= DS_OUT_LIGHTBAR_B) return;   // too short to hold the LED fields
    uint8_t flag1 = rep[DS_OUT_VALIDFLAG1];

    if (flag1 & DS_FLAG1_LED_COLOR) {
        g_state.led_r = rep[DS_OUT_LIGHTBAR_R];
        g_state.led_g = rep[DS_OUT_LIGHTBAR_G];
        g_state.led_b = rep[DS_OUT_LIGHTBAR_B];
    }
    if (flag1 & DS_FLAG1_PLAYER_LEDS) {
        g_state.player_leds = rep[DS_OUT_PLAYER_LEDS];
        g_state.player_num = ds_decode_player(rep[DS_OUT_PLAYER_LEDS]);
    }
    // Brightness is gated by valid_flag2 (the lightbar-setup block), not the player flag.
    if (replen > DS_OUT_VALIDFLAG2 && (rep[DS_OUT_VALIDFLAG2] & DS_FLAG2_BRIGHTNESS_EN)) {
        g_state.led_scale = ds_brightness_scale(rep[DS_OUT_LED_BRIGHTNESS]);
    }
}

//--------------------------------------------------------------------
// No-op AUDIO class driver: claims audio interfaces 0/1/2 so the composite
// fingerprint holds up wired, and just ACKs their control requests -- no real
// audio, no iso endpoints. (TinyUSB auto-acks SET_INTERFACE when we return false.)
//--------------------------------------------------------------------
static void audio_noop_init(void) {}
static bool audio_noop_deinit(void) { return true; }
static void audio_noop_reset(uint8_t rhport) { (void)rhport; }

static uint16_t audio_noop_open(uint8_t rhport, tusb_desc_interface_t const *itf,
                                uint16_t max_len) {
    (void)rhport;
    if (itf->bInterfaceClass != TUSB_CLASS_AUDIO) return 0;
    // Consume every consecutive AUDIO interface/altsetting + functional/endpoint
    // descriptor up to the next non-audio interface. Endpoints are NOT opened.
    uint8_t const *p = (uint8_t const *)itf;
    uint16_t used = 0;
    while (used < max_len) {
        tusb_desc_interface_t const *d = (tusb_desc_interface_t const *)p;
        if (d->bDescriptorType == TUSB_DESC_INTERFACE &&
            d->bInterfaceClass != TUSB_CLASS_AUDIO) break;
        uint8_t len = p[0];
        if (len == 0) break;
        used = (uint16_t)(used + len);
        p += len;
    }
    return used;
}

static bool audio_noop_control_xfer(uint8_t rhport, uint8_t stage,
                                    tusb_control_request_t const *req) {
    // Standard requests (SET_INTERFACE / GET_INTERFACE): let usbd auto-ack them.
    if (req->bmRequestType_bit.type == TUSB_REQ_TYPE_STANDARD) return false;
    if (stage != CONTROL_STAGE_SETUP) return true;
    // Audio class requests: ack with a zeroed buffer (or swallow OUT data).
    static uint8_t scratch[64];
    uint16_t n = req->wLength; if (n > sizeof(scratch)) n = sizeof(scratch);
    if (req->bmRequestType_bit.direction == TUSB_DIR_IN) memset(scratch, 0, n);
    return tud_control_xfer(rhport, (tusb_control_request_t *)req, scratch, n);
}

static bool audio_noop_xfer(uint8_t rhport, uint8_t ep, xfer_result_t r, uint32_t n) {
    (void)rhport; (void)ep; (void)r; (void)n; return true;
}

static const usbd_class_driver_t s_audio_noop_driver = {
    .name            = "audio-noop",
    .init            = audio_noop_init,
    .deinit          = audio_noop_deinit,
    .reset           = audio_noop_reset,
    .open            = audio_noop_open,
    .control_xfer_cb = audio_noop_control_xfer,
    .xfer_cb         = audio_noop_xfer,
    .sof             = NULL,
};

usbd_class_driver_t const *usbd_app_driver_get_cb(uint8_t *count) {
    *count = 1;
    return &s_audio_noop_driver;
}
