/* SPDX-License-Identifier: MIT */
/* Macintosh Device Manager ABI implementation for a single emulated floppy.
 * Interface fields are specified by Apple's Inside Macintosh: Devices and
 * Inside Macintosh: Files. See PROVENANCE_REVIEW.md before publication.
 */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "disc.h"
#include "m68k.h"
#include "machw.h"
#include "emulator/umac_block_backend.h"
#if defined(CONFIG_MCX_MAC_UMAC_VIDEO_DIRTY_TRACK)
#include "emulator/umac_video.h"
#define FLOPPY_GUEST_WRITE(address, length) \
	umac_video_mark_guest_write((address), (length))
#else
#define FLOPPY_GUEST_WRITE(address, length) do { } while (0)
#endif

enum {
	PB_TRAP = 6,
	PB_RESULT = 16,
	PB_DRIVE = 22,
	PB_CONTROL_CODE = 26,
	PB_CONTROL_DATA = 28,
	PB_BUFFER = 32,
	PB_REQUESTED = 36,
	PB_ACTUAL = 40,
	DCE_POSITION = 16,
	DRIVE_STATUS_BYTES = 32,
	DRIVE_QUEUE_OFFSET = 4,
	DRIVE_NUMBER = 1,
	HDD_DRIVE_NUMBER = 8,
	DRIVER_REFNUM = -5,
	ERR_BAD_UNIT = -21,
	ERR_CONTROL = -17,
	ERR_STATUS = -18,
	ERR_PARAM = -50,
	ERR_OFFLINE = -65,
	ERR_WRITE_PROTECT = -44,
};

static uint32_t status_addr;
static uint32_t media_size;
static bool media_present;
static bool media_writable;
static uint32_t hdd_status_addr;
static uint32_t hdd_media_size;
static bool hdd_media_present;
static bool hdd_media_writable;
static bool hdd_insert_event_pending;

static bool guest_range(uint32_t address, uint32_t length)
{
	address = ADR24(address);
	return !overlay && address <= RAM_SIZE && length <= RAM_SIZE - address;
}

static uint16_t guest_u16(uint32_t address)
{
	return RAM_RD16(ADR24(address));
}

static uint32_t guest_u32(uint32_t address)
{
	return RAM_RD32(ADR24(address));
}

static void guest_w16(uint32_t address, uint16_t value)
{
	RAM_WR16(ADR24(address), value);
	FLOPPY_GUEST_WRITE(ADR24(address), 2U);
}

static void guest_w32(uint32_t address, uint32_t value)
{
	RAM_WR32(ADR24(address), value);
	FLOPPY_GUEST_WRITE(ADR24(address), 4U);
}

static int16_t floppy_open(uint32_t pb, uint32_t dce, uint32_t status)
{
#if defined(CONFIG_MCX_MAC_UMAC_FIXED_DISK_ROM)
	uint32_t queue = status + 6U;
#else
	uint32_t queue = status + DRIVE_QUEUE_OFFSET;
#endif

	if (!media_present || !guest_range(pb, 50U) ||
	    !guest_range(dce, 20U) || !guest_range(status, DRIVE_STATUS_BYTES)) {
		return ERR_OFFLINE;
	}
	status_addr = status;
	memset(ram_get_base() + status, 0, DRIVE_STATUS_BYTES);
	FLOPPY_GUEST_WRITE(status, DRIVE_STATUS_BYTES);
#if defined(CONFIG_MCX_MAC_UMAC_FIXED_DISK_ROM)
	RAM_WR8(status + 2U, media_writable ? 0U : 0x80U);
	RAM_WR8(status + 3U, 1U); /* disk in place */
	RAM_WR8(status + 4U, 1U); /* drive installed */
	RAM_WR8(status + 5U, media_size == 819200U ? 0xffU : 0U);
#else
	RAM_WR8(status, media_writable ? 0U : 0x80U);
	RAM_WR8(status + 1U, 1U);
	RAM_WR8(status + 3U, media_size == 819200U ? 0x80U : 0U);
#endif
	guest_w16(queue + 4U, 0U); /* qType: size fits in one word */
	guest_w16(queue + 6U, DRIVE_NUMBER);
	guest_w16(queue + 8U, (uint16_t)DRIVER_REFNUM);
	guest_w16(queue + 10U, 0U); /* File Manager filesystem */
#if !defined(CONFIG_MCX_MAC_UMAC_FIXED_DISK_ROM)
	guest_w16(queue + 12U, (uint16_t)(media_size / 512U));
	guest_w16(queue + 14U, 0U);
#endif
	guest_w32(dce + DCE_POSITION, 0U);
	return 0;
}

/* Drive 8 is a fixed block device. It shares the MCX ROM entry stub but is
 * never presented as a second floppy.
 */
static int16_t hdd_open(uint32_t pb, uint32_t dce, uint32_t status)
{
	/* A full DrvSts starts with a 2-byte track number and four 1-byte
	 * flags; its drive queue element therefore starts at byte 6. */
	uint32_t queue = status + 6U;

	if (!hdd_media_present || !guest_range(pb, 50U) ||
	    !guest_range(dce, 20U) || !guest_range(status, DRIVE_STATUS_BYTES)) {
		return ERR_OFFLINE;
	}
	hdd_status_addr = status;
	hdd_insert_event_pending = true;
	memset(ram_get_base() + status, 0, DRIVE_STATUS_BYTES);
	FLOPPY_GUEST_WRITE(status, DRIVE_STATUS_BYTES);
	RAM_WR8(status + 2U, hdd_media_writable ? 0U : 0x80U);
	RAM_WR8(status + 3U, 8U); /* nonejectable fixed disk */
	RAM_WR8(status + 4U, 1U); /* installed */
	guest_w16(queue + 4U, 0U); /* qType: sector count fits in one word */
	guest_w16(queue + 6U, HDD_DRIVE_NUMBER);
	guest_w16(queue + 8U, (uint16_t)DRIVER_REFNUM);
	guest_w16(queue + 10U, 0U);
	guest_w16(queue + 12U, (uint16_t)(hdd_media_size / 512U));
	guest_w16(queue + 14U, 0U);
	return 0;
}

static int16_t floppy_prime(uint32_t pb, uint32_t dce)
{
	uint32_t length;
	uint32_t address;
	uint32_t position;
	uint16_t trap;
	bool write;
	bool hdd;
	uint32_t drive_size;

	if (!guest_range(pb, 50U) || !guest_range(dce, 20U)) {
		return ERR_PARAM;
	}
	guest_w32(pb + PB_ACTUAL, 0U);
	hdd = (int16_t)guest_u16(pb + PB_DRIVE) == HDD_DRIVE_NUMBER;
	if (hdd ? (!hdd_media_present || hdd_status_addr == 0U) :
	    ((int16_t)guest_u16(pb + PB_DRIVE) != DRIVE_NUMBER ||
	     !media_present || status_addr == 0U)) {
		return ERR_BAD_UNIT;
	}
	drive_size = hdd ? hdd_media_size : media_size;
	trap = guest_u16(pb + PB_TRAP) & 0xffU;
	if (trap != 2U && trap != 3U) {
		return ERR_PARAM;
	}
	write = trap == 3U;
	if (write && !(hdd ? hdd_media_writable : media_writable)) {
		return ERR_WRITE_PROTECT;
	}
	length = guest_u32(pb + PB_REQUESTED);
	address = ADR24(guest_u32(pb + PB_BUFFER));
	position = guest_u32(dce + DCE_POSITION);
	if ((length & 511U) != 0U || (position & 511U) != 0U ||
	    !guest_range(address, length) || position > drive_size ||
	    length > drive_size - position) {
		return ERR_PARAM;
	}
	if (umac_block_backend_transfer(hdd ? 1U : 0U, write, position,
					 ram_get_base() + address, length) != 0) {
		return ERR_OFFLINE;
	}
	if (!write) {
		FLOPPY_GUEST_WRITE(address, length);
	}
	guest_w32(pb + PB_ACTUAL, length);
	guest_w32(dce + DCE_POSITION, position + length);
	return 0;
}

static int16_t floppy_control(uint32_t pb)
{
	uint16_t code;
	bool hdd;

	if (!guest_range(pb, 50U)) {
		return ERR_PARAM;
	}
	code = guest_u16(pb + PB_CONTROL_CODE);
	if (code == 1U || code == 9U) { /* KillIO / cache control */
		return 0;
	}
	if (code == 65U) { /* periodic action */
		/* Private ROM convention: +1 asks its 68000 stub to call Apple's
		 * PostEvent(diskEvt, drive 8) once. The callback must not invoke a
		 * Toolbox trap from host C while the guest is inside a PV hook. */
		if (hdd_insert_event_pending && hdd_status_addr != 0U) {
			hdd_insert_event_pending = false;
			return 1;
		}
		return 0;
	}
	hdd = (int16_t)guest_u16(pb + PB_DRIVE) == HDD_DRIVE_NUMBER;
	if (hdd ? hdd_status_addr == 0U :
	    ((int16_t)guest_u16(pb + PB_DRIVE) != DRIVE_NUMBER ||
	     status_addr == 0U)) {
		return ERR_BAD_UNIT;
	}
	switch (code) {
	case 5U: /* verify */
		return (hdd ? hdd_media_present : media_present) ? 0 : ERR_OFFLINE;
	case 6U: /* low-level format: fixed block media is already addressable */
		if (!hdd || !hdd_media_present) {
			return ERR_CONTROL;
		}
		return hdd_media_writable ? 0 : ERR_WRITE_PROTECT;
	case 7U: /* eject: current image remains attached until reset */
		return ERR_CONTROL;
	case 23U: /* internal floppy drive info */
		guest_w32(pb + PB_CONTROL_DATA, hdd ? 0U : 4U);
		return 0;
	default:
		return ERR_CONTROL;
	}
}

static int16_t floppy_status(uint32_t pb)
{
	bool hdd;
	uint32_t source;

	if (!guest_range(pb, 50U)) {
		return ERR_PARAM;
	}
	hdd = (int16_t)guest_u16(pb + PB_DRIVE) == HDD_DRIVE_NUMBER;
	if (hdd ? hdd_status_addr == 0U :
	    ((int16_t)guest_u16(pb + PB_DRIVE) != DRIVE_NUMBER ||
	     status_addr == 0U)) {
		return ERR_BAD_UNIT;
	}
	source = hdd ? hdd_status_addr : status_addr;
	switch (guest_u16(pb + PB_CONTROL_CODE)) {
	case 8U: /* drive status record */
		memcpy(ram_get_base() + pb + PB_CONTROL_DATA,
		       ram_get_base() + source, 22U);
		FLOPPY_GUEST_WRITE(pb + PB_CONTROL_DATA, 22U);
		return 0;
	case 10U: /* disk type information */
		guest_w32(pb + PB_CONTROL_DATA, 0xfeU);
		return 0;
	default:
		return ERR_STATUS;
	}
}

void disc_init(disc_descr_t discs[DISC_NUM_DRIVES])
{
	umac_block_backend_init(discs);
	media_size = discs != NULL ? discs[0].size : 0U;
	media_present = media_size == 409600U || media_size == 819200U;
	media_writable = media_present && !discs[0].read_only;
	status_addr = 0U;
	hdd_media_size = discs != NULL ? discs[1].size : 0U;
	hdd_media_present = hdd_media_size >= 1024U &&
		(hdd_media_size & 511U) == 0U && hdd_media_size <= 0xffffU * 512U;
	hdd_media_writable = hdd_media_present && !discs[1].read_only;
	hdd_status_addr = 0U;
	hdd_insert_event_pending = false;
}

int disc_pv_hook(uint8_t opcode)
{
	uint32_t pb = ADR24(m68k_get_reg(NULL, M68K_REG_A0));
	uint32_t dce = ADR24(m68k_get_reg(NULL, M68K_REG_A1));
	int16_t result;

	switch (opcode) {
	case 0U:
		result = floppy_open(pb, dce,
			ADR24(m68k_get_reg(NULL, M68K_REG_A2)));
		break;
	case 1U:
		result = floppy_prime(pb, dce);
		break;
	case 2U:
		result = floppy_control(pb);
		break;
	case 3U:
		result = floppy_status(pb);
		break;
	case 4U:
		result = hdd_open(pb, dce,
			ADR24(m68k_get_reg(NULL, M68K_REG_A2)));
		break;
	default:
		return -1;
	}
	m68k_set_reg(M68K_REG_D0, (uint32_t)(int32_t)result);
	return 0;
}
