/* SPDX-License-Identifier: MIT */
#include <stdint.h>
#include <stdio.h>
#include "via.h"
#define REG(n) ((unsigned int)(n) << 9)
static int irq_level;
static void irq_set(int level) { irq_level = level; }
static int fail(const char *m) { fprintf(stderr, "FAIL: %s\n", m); return 1; }
int main(void)
{
	struct via_cb cb = { .irq_set = irq_set };
	via_init(&cb); via_tick(0U); via_write(REG(14), 0xc0U);
	via_write(REG(4), 9U); via_write(REG(5), 0U); via_tick(10U);
	if (irq_level) return fail("T1 early");
	via_tick(20U); if (!irq_level) return fail("T1 one-shot");
	(void)via_read(REG(4)); if (irq_level) return fail("T1 acknowledge");
	via_write(REG(11), 0x40U); via_write(REG(4), 4U); via_write(REG(5), 0U);
	via_tick(40U); if (!irq_level) return fail("T1 free-run");
	(void)via_read(REG(4)); via_tick(50U); if (!irq_level) return fail("T1 reload");
	(void)via_read(REG(4)); via_write(REG(14), 0x40U); via_write(REG(14), 0xa0U);
	via_write(REG(11), 0U); via_write(REG(8), 3U); via_write(REG(9), 0U);
	via_tick(60U); if (!irq_level) return fail("T2 interval"); (void)via_read(REG(8));
	via_write(REG(11), 0x20U); via_write(REG(8), 1U); via_write(REG(9), 0U);
	via_pb6_pulse(); if (irq_level) return fail("T2 pulse early");
	via_pb6_pulse(); if (!irq_level) return fail("T2 pulse");
	puts("uMac VIA timer tests: PASS"); return 0;
}
