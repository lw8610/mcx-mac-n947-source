/* SPDX-License-Identifier: MIT */
#include "umac_serial_input.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <umac.h>

#include "video/vga.h"

#define INPUT_LINE_CAPACITY 64U
#define KEY_QUEUE_CAPACITY 64U
#define KEY_EVENT_SPACING_MS 50U

struct key_event {
	uint8_t mac_code;
	bool down;
};

static const struct device *const input_uart =
	DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
static char input_line[INPUT_LINE_CAPACITY];
static unsigned int input_length;
static bool discard_line;
static struct key_event key_queue[KEY_QUEUE_CAPACITY];
static unsigned int key_head;
static unsigned int key_tail;
static uint32_t next_key_ms;
static bool guest_paused;
static bool test_pattern_enabled;
static uint32_t rx_bytes;
static uint32_t rx_lines;
static uint32_t valid_commands;
static uint32_t rx_errors;
static uint8_t last_rx_byte;

static void skip_spaces(const char **cursor)
{
	while (**cursor == ' ' || **cursor == '\t') {
		(*cursor)++;
	}
}

static bool parse_number(const char **cursor, int base, long min, long max,
			 long *result)
{
	char *end;
	long value;

	skip_spaces(cursor);
	if (**cursor == '\0') {
		return false;
	}
	value = strtol(*cursor, &end, base);
	if (end == *cursor || value < min || value > max) {
		return false;
	}
	*cursor = end;
	*result = value;
	return true;
}

static bool at_end(const char *cursor)
{
	skip_spaces(&cursor);
	return *cursor == '\0';
}

static void enqueue_key(uint8_t mac_code, bool down)
{
	unsigned int next = (key_head + 1U) % KEY_QUEUE_CAPACITY;

	if (next == key_tail) {
		/* The producer can run ahead of the guest's keyboard polling. */
		return;
	}
	key_queue[key_head] = (struct key_event){ .mac_code = mac_code,
					      .down = down };
	key_head = next;
}

static void process_line(void)
{
	const char *cursor = input_line;
	long a, b, c;

	if (*cursor == 'K') {
		cursor++;
		if (parse_number(&cursor, 16, 0, 0x7f, &a) &&
		    parse_number(&cursor, 10, 0, 1, &b) && at_end(cursor)) {
			enqueue_key((uint8_t)a, b != 0);
			valid_commands++;
		}
	} else if (*cursor == 'M') {
		cursor++;
		if (parse_number(&cursor, 10, -127, 127, &a) &&
		    parse_number(&cursor, 10, -127, 127, &b) &&
		    parse_number(&cursor, 10, 0, 1, &c) && at_end(cursor)) {
			/* The serial protocol uses screen coordinates (Y down);
			 * uMac expects positive Y upwards. */
			umac_mouse((int)a, -(int)b, (int)c);
			valid_commands++;
		}
	} else if (*cursor == 'F') {
		cursor++;
		if (parse_number(&cursor, 10, 0, 1, &a) && at_end(cursor)) {
			vga_raster_freeze(a != 0);
			valid_commands++;
		}
	} else if (*cursor == 'P') {
		cursor++;
		if (parse_number(&cursor, 10, 0, 1, &a) && at_end(cursor)) {
			guest_paused = a != 0;
			valid_commands++;
			printk("DIAG uMac guest pause=%u (VGA continues)\n",
			       guest_paused ? 1U : 0U);
		}
	} else if (*cursor == 'B') {
		cursor++;
		if (parse_number(&cursor, 10, 0, 1, &a) && at_end(cursor)) {
			test_pattern_enabled = a != 0;
			valid_commands++;
			if (test_pattern_enabled) {
				vga_raster_freeze(false);
				vga_framebuffer_test_pattern();
			}
			printk("DIAG VGA test pattern=%u\n",
			       test_pattern_enabled ? 1U : 0U);
		}
	}
}

void umac_serial_input_init(void)
{
	if (device_is_ready(input_uart)) {
		printk("DIAG uMac input UART ready: K <Mac-code-hex> <0|1>, "
		       "M <dx> <dy> <button>, F <0|1>, P <0|1>, B <0|1>\n");
	} else {
		printk("DIAG uMac input UART unavailable\n");
	}
	next_key_ms = k_uptime_get_32();
}

bool umac_serial_guest_paused(void)
{
	return guest_paused;
}

bool umac_serial_test_pattern_enabled(void)
{
	return test_pattern_enabled;
}

void umac_serial_input_report(void)
{
	struct umac_kbd_diagnostics kbd;

	printk("DIAG UART RX bytes=%u lines=%u valid=%u errors=%u last=%02x\n",
	       rx_bytes, rx_lines, valid_commands, rx_errors, last_rx_byte);
	umac_kbd_get_diagnostics(&kbd);
	printk("DIAG uMac keyboard cmd=%u inquiry=%u submitted=%u consumed=%u overwritten=%u via_rejected=%u via_reads=%u pending=%d last_cmd=%02x last_evt=%02x\n",
	       kbd.commands, kbd.inquiries, kbd.events_submitted,
	       kbd.events_consumed, kbd.events_overwritten,
	       kbd.via_rejected, kbd.via_event_reads,
	       kbd.pending_event, kbd.last_command,
	       kbd.last_event);
}

void umac_serial_input_poll(void)
{
	unsigned char byte;
	uint32_t now;
	int error;

	if (!device_is_ready(input_uart)) {
		return;
	}
	/* Flexcomm reports RX overrun separately. Clear it before polling so a
	 * burst from the host cannot leave the receiver stuck until reset. A
	 * partial command is unsafe after a dropped byte. */
	error = uart_err_check(input_uart);
	if (error > 0) {
		rx_errors++;
		input_length = 0U;
		discard_line = true;
	}
	/* Bound RX work so serial traffic cannot starve the video service. */
	for (unsigned int i = 0; i < 48U; i++) {
		if (uart_poll_in(input_uart, &byte) != 0) {
			break;
		}
		rx_bytes++;
		last_rx_byte = byte;
		if (byte == '\n' || byte == '\r') {
			rx_lines++;
			if (!discard_line) {
				input_line[input_length] = '\0';
				process_line();
			}
			input_length = 0U;
			discard_line = false;
		} else if (byte >= 0x20U && byte <= 0x7eU) {
			if (discard_line) {
				continue;
			}
			if (input_length + 1U < sizeof(input_line)) {
				input_line[input_length++] = (char)byte;
			} else {
				/* Discard an overlong command in full. */
				input_length = 0U;
				discard_line = true;
			}
		}
	}

	now = k_uptime_get_32();
	if (key_tail != key_head && (int32_t)(now - next_key_ms) >= 0) {
		struct key_event event = key_queue[key_tail];

		key_tail = (key_tail + 1U) % KEY_QUEUE_CAPACITY;
		/* Match upstream's wire encoding, not its MKC_* number. */
		umac_kbd_event((uint8_t)((event.mac_code << 1U) | 1U),
			       event.down ? 1 : 0);
		next_key_ms = now + KEY_EVENT_SPACING_MS;
	}
}
