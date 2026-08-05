#include "tusb.h"
#include "host/usbh.h"
#include "class/hid/hid.h"
#include "class/hid/hid_host.h"
#include "pio_usb.h"
#include "mbedtls/des.h"
#include "pico/time.h"

#include "dongle.h"
#include "shared_state.h"

#include <string.h>

// Mayflash S5 dongle: VID/PID, feature codes, DES unlock key, and other stuff
static constexpr uint16_t S5_VID = 0x054C;
static constexpr uint16_t S5_PID = 0x0CE6;
static constexpr uint8_t S5_GET_AUTH = 0x01;				// GET INPUT: DES challenge
static constexpr uint8_t S5_AUTH_COMPLETE = 0x02;		// SET OUTPUT: unlock reply (+ interrupt-OUT id)
static constexpr uint8_t S5_GET_CALIBRATION = 0x05; // device-info features (not auth)
static constexpr uint8_t S5_GET_PAIRINFO = 0x09;
static constexpr uint8_t S5_GET_FIRMWARE = 0x20;
static constexpr uint8_t S5_SET_TESTCMD = 0x80; // turns signing on
static constexpr uint8_t S5_GET_TESTRESP = 0x81;
static constexpr uint8_t S5_SET_AUTHPAYLOAD = 0xF0; // native auth: console payload
static constexpr uint8_t S5_GET_SIGNNONCE = 0xF1;		// native auth: signature nonce
static constexpr uint8_t S5_GET_SIGNSTATE = 0xF2;		// native auth: signing state

// DES secret key for the Mayflash S5 dongle, provided by Mayflash
static constexpr uint8_t S5_DES_SECRET_KEY[16] = {
		0x5C, 0x28, 0xE3, 0x05, 0x97, 0xC5, 0xAD, 0x04,
		0x9E, 0x5D, 0x19, 0xC3, 0x25, 0x40, 0x5B, 0x9D};

static constexpr uint8_t kBattery[3] = {0x28, 0x18, 0x00};
static constexpr uint8_t kTouchpad[9] = {0x80, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00};

// this might be totally wrong, saw in the pcap dump
static constexpr uint8_t kOutTrailer[20] = {
		0x47, 0x85, 0x9f, 0x6a, 0xae, 0x43, 0xdd, 0xf1, 0xe4, 0xad,
		0x9c, 0xbd, 0xc7, 0x67, 0x6e, 0x95, 0x20, 0xee, 0x8e, 0x0f};

static uint8_t s_dev_addr = 0xFF;
static uint8_t s_instance = 0xFF;

enum AuthState : uint8_t
{
	A_IDLE = 0,
	A_POLL_F2,
	A_FETCH_F1,
	A_WAIT
};
static volatile AuthState s_auth = A_IDLE;
static uint64_t s_auth_us = 0;
static uint8_t s_f1_idx = 0;

// After the unlock SET, the F300 goes silent ~810ms, then bus-resets the dongle
// into its signing persona. The silence is the trigger, so we mirror it: idle,
// then force a re-enumeration. Forcing it early just re-reads the non-signing persona.
// This re-enumeration is a bit of a hack, might not even be needed...
// TODO: Remove and check if the S5 still works
enum ReenumStage : uint8_t
{
	RS_NONE = 0, // idle
	RS_WAIT,		 // unlock done: stay silent for the quiet window
	RS_FORCE,		 // window elapsed: force the re-enumeration
	RS_SETTLE,	 // forced: wait for the dongle to come back
};
static volatile ReenumStage s_reenum = RS_NONE;
static uint64_t s_reenum_us = 0;
static constexpr uint64_t kQuietWaitUs = 850000;	 // ~810ms quiet window + margin
static constexpr uint64_t kReenumWaitUs = 1500000; // grace for the re-enumeration
static constexpr uint8_t kPioRootIdx = BOARD_TUH_RHPORT - 1;

// Arm interrupt-IN. No-op while a transfer is pending, so it's safe every loop.
static bool arm_in(uint8_t dev_addr, uint8_t instance)
{
	return tuh_hid_receive_report(dev_addr, instance);
}

// Based on the info provided by mayflash
static void s5_encrypt(const uint8_t *in, uint8_t *out)
{
	mbedtls_des_context ctx;
	mbedtls_des_init(&ctx);
	mbedtls_des_setkey_enc(&ctx, &S5_DES_SECRET_KEY[0]);
	mbedtls_des_crypt_ecb(&ctx, &in[0], &out[0]);
	mbedtls_des_setkey_enc(&ctx, &S5_DES_SECRET_KEY[8]);
	mbedtls_des_crypt_ecb(&ctx, &in[8], &out[8]);
	mbedtls_des_free(&ctx);
}

// Reassemble the dongle's signed 0x01 interrupt-IN reply into the 64-byte report
static void copy_signed_report(const uint8_t *report)
{
	uint8_t *dst = g_state.signed_report;
	memset(dst, 0, 64);
	memcpy(&dst[0x00], &report[0x0D], 16); // 12 input bytes + 4 incount
	memcpy(&dst[0x1C], &report[0x25], 5);	 // sensor timestamp + temperature
	memcpy(&dst[0x21], &kTouchpad[0], 9);	 // faked touchpad
	memcpy(&dst[0x2A], &report[0x2A], 2);	 // trigger statuses
	memcpy(&dst[0x31], &report[0x31], 4);	 // device timestamp
	memcpy(&dst[0x35], &kBattery[0], 3);	 // battery + mic
	memcpy(&dst[0x38], &report[0x1D], 8);	 // 8-byte AES CMAC
	dst[0] = 0x01;
}

static void send_test_command(uint8_t dev_addr, uint8_t instance)
{
	uint8_t *cmd = g_state.feat_buffer;
	memset(cmd, 0, 64);
	cmd[0] = S5_SET_TESTCMD;
	cmd[1] = 0x01;
	cmd[2] = 0x13;
	tuh_hid_set_report(dev_addr, instance, S5_SET_TESTCMD, HID_REPORT_TYPE_FEATURE, cmd, 64);
}

static void start_feature_fetch(uint8_t dev_addr, uint8_t instance)
{
	tuh_hid_get_report(dev_addr, instance, S5_GET_CALIBRATION, HID_REPORT_TYPE_FEATURE, g_state.feat_buffer, 41);
}

struct VendorReq
{
	uint8_t bmRequestType;
	uint8_t bRequest;
	uint16_t wValue;
	uint16_t wIndex;
	uint16_t wLength;
};

static const VendorReq kVendorSeq[] = {
		{0xA1, 0x82, 0x0200, 0x0500, 2}, // GET register
		{0xA1, 0x83, 0x0200, 0x0500, 2},
		{0xA1, 0x84, 0x0200, 0x0500, 2},
		{0xA1, 0x82, 0x0200, 0x0200, 2},
		{0xA1, 0x83, 0x0200, 0x0200, 2},
		{0xA1, 0x84, 0x0200, 0x0200, 2},
		{0x21, 0x01, 0x0200, 0x0200, 2}, // SET, payload 47 e6
		{0x01, 0x0B, 0x0001, 0x0001, 0}, // SET_INTERFACE itf1 alt1 (audio out)
		{0x01, 0x0B, 0x0001, 0x0002, 0}, // SET_INTERFACE itf2 alt1 (audio in)
};
static const uint8_t kVendorSetData[2] = {0x47, 0xe6};
static uint8_t s_vendor_step = 0;
static tusb_control_request_t s_vendor_setup;
static uint8_t s_vendor_buf[8];

static void vendor_xfer_complete(tuh_xfer_t *xfer);

static void issue_vendor_step(uint8_t dev_addr)
{
	const VendorReq &r = kVendorSeq[s_vendor_step];
	s_vendor_setup.bmRequestType = r.bmRequestType;
	s_vendor_setup.bRequest = r.bRequest;
	s_vendor_setup.wValue = r.wValue;
	s_vendor_setup.wIndex = r.wIndex;
	s_vendor_setup.wLength = r.wLength;

	if ((r.bmRequestType & 0x80) == 0 && r.wLength > 0)
		memcpy(s_vendor_buf, kVendorSetData, sizeof(kVendorSetData));

	tuh_xfer_t xfer = {};
	xfer.daddr = dev_addr;
	xfer.ep_addr = 0;
	xfer.setup = &s_vendor_setup;
	xfer.buffer = s_vendor_buf;
	xfer.complete_cb = vendor_xfer_complete;
	xfer.user_data = 0;
	tuh_control_xfer(&xfer);
}

static void vendor_xfer_complete(tuh_xfer_t *xfer)
{
	// Advance regardless of result; a STALL on a read is harmless here.
	s_vendor_step++;
	if (s_vendor_step < (sizeof(kVendorSeq) / sizeof(kVendorSeq[0])))
	{
		issue_vendor_step(xfer->daddr);
	}
	else
	{
		start_feature_fetch(s_dev_addr, s_instance);
	}
}

// Phase 2: vendor block -> features -> 0x80 -> signing.
static void start_phase2(uint8_t dev_addr)
{
	s_vendor_step = 0;
	issue_vendor_step(dev_addr);
}

void dongle_init(void)
{
	s_dev_addr = 0xFF;
	s_instance = 0xFF;
	g_state.unlocked = false;
	s_reenum = RS_NONE;
	s_reenum_us = 0;
}

// Called every core1 loop: run the re-enum state machine, relay PS5 auth, and
// free-run the 0x02 signing request while keeping interrupt-IN armed.
void dongle_task(void)
{
	// this is so hacky, do we actually need to do this?
	switch (s_reenum)
	{
	case RS_WAIT:
		if (time_us_64() - s_reenum_us >= kQuietWaitUs)
		{
			s_reenum = RS_FORCE;
		}
		return;
	case RS_FORCE:
		pio_usb_host_force_reenumerate(kPioRootIdx);
		s_reenum_us = time_us_64();
		s_reenum = RS_SETTLE;
		return;
	case RS_SETTLE:
		if (time_us_64() - s_reenum_us >= kReenumWaitUs)
		{
			// Never came back -- fall back to phase 2 on the same address.
			s_reenum = RS_NONE;
			if (g_state.dongle_mounted && s_dev_addr != 0xFF)
			{
				g_state.dongle_ready = true;
				start_phase2(s_dev_addr);
			}
		}
		return;
	default:
		break;
	}

	if (!g_state.reports_ready || s_dev_addr == 0xFF)
		return;

	// Relay one queued PS5 feature-write (pairing 0x08/0x0A, auth 0xF0) at a time.
	// set_inflight gates this and the F1/F2 GETs so only one control xfer is in flight.
	if (!g_state.set_inflight && g_state.set_q_tail != g_state.set_q_head)
	{
		ps5_pending_set_t *s = &g_state.set_queue[g_state.set_q_tail];
		g_state.set_inflight = true;
		tuh_hid_set_report(s_dev_addr, s_instance, s->report_id, HID_REPORT_TYPE_FEATURE, s->data, s->len);
	}

	// Native auth: once F0 is forwarded, poll F2 then fetch the 4 F1 chunks.
	if (!g_state.set_inflight)
	{
		switch (s_auth)
		{
		case A_IDLE:
			if (g_state.auth_ready_to_poll &&
					g_state.set_q_tail == g_state.set_q_head)
			{
				g_state.auth_ready_to_poll = false;
				s_auth = A_POLL_F2;
				s_auth_us = time_us_64();
			}
			break;
		case A_POLL_F2:
			if (time_us_64() - s_auth_us >= 10000)
			{
				g_state.set_inflight = true;
				s_auth = A_WAIT;
				tuh_hid_get_report(s_dev_addr, s_instance, S5_GET_SIGNSTATE, HID_REPORT_TYPE_FEATURE, g_state.feat_buffer, 16);
			}
			break;
		case A_FETCH_F1:
			g_state.set_inflight = true;
			s_auth = A_WAIT;
			tuh_hid_get_report(s_dev_addr, s_instance, S5_GET_SIGNNONCE, HID_REPORT_TYPE_FEATURE, g_state.feat_buffer, 64);
			break;
		case A_WAIT:
			break;
		}
	}

	arm_in(s_dev_addr, s_instance);

	if (!tuh_hid_send_ready(s_dev_addr, s_instance))
		return;

	uint8_t *buf = g_state.out_buffer;
	memset(buf, 0, 48);
	buf[0] = 0x02; // key-encryption request
	buf[1] = 0x04; // data type
	buf[3] = 0x18; // 24 effective bytes
	memcpy(&buf[4], (const void *)g_state.input12, 12);
	memcpy(&buf[16], kBattery, 3);
	memcpy(&buf[19], kTouchpad, 9);
	memcpy(&buf[28], kOutTrailer, 20);

	tuh_hid_send_report(s_dev_addr, s_instance, 0, buf, 48);
}

extern "C"
{

	void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance,
												uint8_t const *desc_report, uint16_t desc_len)
	{
		(void)desc_report;
		(void)desc_len;

		uint16_t vid = 0, pid = 0;
		tuh_vid_pid_get(dev_addr, &vid, &pid);
		if (vid != S5_VID || pid != S5_PID)
			return;
		if (g_state.dongle_mounted)
			return;

		s_dev_addr = dev_addr;
		s_instance = instance;
		g_state.dongle_mounted = true;

		if (!g_state.unlocked)
		{
			tuh_hid_get_report(dev_addr, instance, S5_GET_AUTH, HID_REPORT_TYPE_INPUT, g_state.unlock_buffer, 64);
		}
		else
		{
			s_reenum = RS_NONE;
			g_state.dongle_ready = true;
			start_phase2(dev_addr);
		}
	}

	void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance)
	{
		if (dev_addr != s_dev_addr)
			return;
		(void)instance;
		s_dev_addr = 0xFF;
		s_instance = 0xFF;
		g_state.dongle_mounted = false;
		g_state.dongle_ready = false;
		g_state.reports_ready = false;
		g_state.hash_pending = false;
		g_state.hash_ready = false;
		// Keep 'unlocked' set: the dongle re-enumerates right after unlock and the next
		// mount must take the phase-2 path.
	}

	// Control GET completed. F1/F2 belong to native auth; otherwise it's the unlock
	// challenge (pre-unlock) or the device-info feature chain (post-unlock).
	void tuh_hid_get_report_complete_cb(uint8_t dev_addr, uint8_t instance, uint8_t report_id, uint8_t report_type, uint16_t len)
	{
		if (dev_addr != s_dev_addr || instance != s_instance)
			return;
		(void)report_type;

		if (report_id == S5_GET_SIGNSTATE)
		{ // F2 signing state
			uint8_t status = g_state.feat_buffer[3];
			if (status == 0x12 || status == 0x52)
			{ // ready -> fetch F1
				memcpy(g_state.auth_f2, &g_state.feat_buffer[1], 15);
				s_f1_idx = 0;
				s_auth = A_FETCH_F1;
			}
			else if (status == 0x10 || status == 0x40)
			{ // done
				memcpy(g_state.auth_f2, &g_state.feat_buffer[1], 15);
				g_state.auth_f1_ready = true;
				s_auth = A_IDLE;
			}
			else
			{ // still signing
				memcpy(g_state.auth_f2_notready, &g_state.feat_buffer[1], 15);
				g_state.auth_f2_have_notready = true;
				s_auth = A_POLL_F2;
				s_auth_us = time_us_64();
			}
			g_state.set_inflight = false;
			return;
		}
		if (report_id == S5_GET_SIGNNONCE)
		{ // F1 chunk
			if (s_f1_idx < 4)
				memcpy(g_state.auth_f1[s_f1_idx], &g_state.feat_buffer[1], 63);
			s_f1_idx++;
			if (s_f1_idx >= 4)
			{
				g_state.auth_f1_read_idx = 0;
				g_state.auth_f1_ready = true; // core0 may now report F2 ready + serve F1
				s_auth = A_IDLE;
			}
			else
			{
				s_auth = A_FETCH_F1;
			}
			g_state.set_inflight = false;
			return;
		}

		// Phase 1: DES challenge -> compute + send the unlock reply.
		if (!g_state.unlocked)
		{
			if (report_id != S5_GET_AUTH)
				return;
			// [7]=0x08 (auth), [12]=0x10 (16 bytes), nonce at [13:29].
			if (len == 64 && g_state.unlock_buffer[7] == 0x08 && g_state.unlock_buffer[12] == 0x10)
			{
				uint8_t enc[16];
				s5_encrypt(&g_state.unlock_buffer[13], enc);
				g_state.unlock_buffer[0] = 0x02;
				g_state.unlock_buffer[1] = 0x08;
				g_state.unlock_buffer[2] = 0x00;
				g_state.unlock_buffer[3] = 0x10;
				memcpy(&g_state.unlock_buffer[4], enc, 16);
				tuh_hid_set_report(dev_addr, instance, S5_AUTH_COMPLETE, HID_REPORT_TYPE_OUTPUT, g_state.unlock_buffer, 48);
				g_state.unlocked = true; // phase 2 begins when this SET completes
			}
			return;
		}

		// Phase 2: feature chain calibration -> firmware -> MAC -> 0x80. Dongle echoes
		// the id in byte 0, so real data starts at byte 1.
		switch (report_id)
		{
		case S5_GET_CALIBRATION:
			memcpy(g_state.calibration_report, &g_state.feat_buffer[1], 40);
			tuh_hid_get_report(dev_addr, instance, S5_GET_FIRMWARE, HID_REPORT_TYPE_FEATURE, g_state.feat_buffer, 64);
			break;
		case S5_GET_FIRMWARE:
			memcpy(g_state.firmware_report, &g_state.feat_buffer[1], 63);
			tuh_hid_get_report(dev_addr, instance, S5_GET_PAIRINFO, HID_REPORT_TYPE_FEATURE, g_state.feat_buffer, 16);
			break;
		case S5_GET_PAIRINFO:
		{
			memcpy(g_state.mac_pair_report, &g_state.feat_buffer[1], 15);
			send_test_command(dev_addr, instance);
			break;
		}
		case S5_GET_TESTRESP:
			break; // nothing to store; signing was already enabled by the 0x80 SET
		default:
			break;
		}
	}

	void tuh_hid_set_report_complete_cb(uint8_t dev_addr, uint8_t instance, uint8_t report_id, uint8_t report_type, uint16_t len)
	{
		if (dev_addr != s_dev_addr || instance != s_instance)
			return;
		(void)report_type;
		(void)len;

		if (report_id == 0x08 || report_id == 0x0A || report_id == 0xF0)
		{
			// A forwarded PS5 pairing/auth write landed -> advance the relay queue.
			g_state.set_q_tail = (uint8_t)((g_state.set_q_tail + 1) % PS5_SET_QUEUE_DEPTH);
			g_state.set_inflight = false;
			return;
		}

		if (report_id == S5_SET_TESTCMD)
		{
			// 0x80 landed: the dongle now signs. Start the stream; the 0x81 read is
			// best-effort (it may STALL).
			g_state.reports_ready = true;
			arm_in(dev_addr, instance);
			tuh_hid_get_report(dev_addr, instance, S5_GET_TESTRESP, HID_REPORT_TYPE_FEATURE, g_state.feat_buffer, 64);
			return;
		}

		if (report_id == S5_AUTH_COMPLETE)
		{
			s_reenum = RS_WAIT;
			s_reenum_us = time_us_64();
			return;
		}
	}

	// Interrupt-IN: the dongle's signed 0x01 reply (byte[7]=0x04).
	void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *report, uint16_t len)
	{
		if (dev_addr == s_dev_addr && instance == s_instance && len >= 0x40)
		{
			copy_signed_report(report);
			g_state.hash_ready = true;
			g_state.hash_pending = false;
		}

		if (dev_addr == s_dev_addr)
		{
			arm_in(dev_addr, instance); // re-arm so the stream keeps flowing
		}
	}

	// Interrupt-OUT completed (the 0x02 sign request went out).
	void tuh_hid_report_sent_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *report, uint16_t len)
	{
		(void)dev_addr;
		(void)instance;
		(void)report;
		(void)len;
	}

} // extern "C"
