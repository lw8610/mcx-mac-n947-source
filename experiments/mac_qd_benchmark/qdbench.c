/* SPDX-License-Identifier: MIT */
/* Small 68000 Macintosh QuickDraw benchmark for uMac targets. */

#include <Events.h>
#include <Fonts.h>
#include <Memory.h>
#include <OSUtils.h>
#include <Quickdraw.h>
#include <TextEdit.h>
#include <Windows.h>

#include <string.h>

enum {
	FRAME_ITERATIONS = 10000,
	PAINT_ITERATIONS = 2000,
	COPY_ITERATIONS = 10000,
	ANIMATION_ITERATIONS = 2000,
	OFFSCREEN_WIDTH = 64,
	OFFSCREEN_HEIGHT = 64,
	OFFSCREEN_ROW_BYTES = OFFSCREEN_WIDTH / 8,
};

struct bench_result {
	const char *name;
	unsigned long iterations;
	unsigned long ticks;
};

static WindowRecord window_record;
enum { RESULT_COUNT = 8 };

static struct bench_result results[RESULT_COUNT];

static void draw_c_string(const char *text)
{
	Str255 pstr;
	size_t length = strlen(text);

	if (length > 255U) {
		length = 255U;
	}
	pstr[0] = (unsigned char)length;
	memcpy(&pstr[1], text, length);
	DrawString(pstr);
}

static void draw_number(unsigned long value)
{
	Str255 number;

	NumToString((long)value, number);
	DrawString(number);
}

static unsigned long elapsed_ticks(unsigned long start)
{
	return TickCount() - start;
}

static void draw_progress(const char *stage)
{
	EraseRect(&window_record.port.portRect);
	MoveTo(12, 20);
	draw_c_string("BENCHMARK RUNNING");
	MoveTo(12, 46);
	draw_c_string(stage);
	MoveTo(12, 72);
	draw_c_string("Please wait for the COMPLETE results screen.");
	SystemTask();
}

static void run_benchmarks(void)
{
	Rect frame_rect;
	Rect paint_rect;
	Rect screen_src;
	Rect screen_dst;
	Rect offscreen_rect;
	Rect moving_rect;
	Rect scaling_rect;
	BitMap first;
	BitMap second;
	Ptr first_pixels;
	Ptr second_pixels;
	unsigned long start;
	unsigned long i;

	SetRect(&frame_rect, 24, 36, 184, 116);
	SetRect(&paint_rect, 204, 36, 268, 100);
	SetRect(&screen_src, 40, 60, 104, 124);
	SetRect(&screen_dst, 120, 60, 184, 124);
	SetRect(&offscreen_rect, 0, 0, OFFSCREEN_WIDTH, OFFSCREEN_HEIGHT);
	SetRect(&moving_rect, 32, 132, 96, 180);
	SetRect(&scaling_rect, 160, 132, 224, 180);

	first_pixels = NewPtrClear(OFFSCREEN_ROW_BYTES * OFFSCREEN_HEIGHT);
	second_pixels = NewPtrClear(OFFSCREEN_ROW_BYTES * OFFSCREEN_HEIGHT);
	if (first_pixels == NULL || second_pixels == NULL) {
		results[0].name = "Not enough RAM for offscreen buffers";
		results[0].iterations = 0;
		results[0].ticks = 0;
		if (first_pixels != NULL) {
			DisposePtr(first_pixels);
		}
		if (second_pixels != NULL) {
			DisposePtr(second_pixels);
		}
		return;
	}

	first.baseAddr = first_pixels;
	first.rowBytes = OFFSCREEN_ROW_BYTES;
	first.bounds = offscreen_rect;
	second.baseAddr = second_pixels;
	second.rowBytes = OFFSCREEN_ROW_BYTES;
	second.bounds = offscreen_rect;

	/* XOR makes every even-numbered test restore its initial pixels. */
	draw_progress("1/8  FrameRect screen");
	PenMode(patXor);
	start = TickCount();
	for (i = 0; i < FRAME_ITERATIONS; ++i) {
		FrameRect(&frame_rect);
	}
	results[0].name = "FrameRect screen";
	results[0].iterations = FRAME_ITERATIONS;
	results[0].ticks = elapsed_ticks(start);

	draw_progress("2/8  PaintRect screen");
	PenMode(patXor);
	start = TickCount();
	for (i = 0; i < PAINT_ITERATIONS; ++i) {
		PaintRect(&paint_rect);
	}
	results[1].name = "PaintRect screen";
	results[1].iterations = PAINT_ITERATIONS;
	results[1].ticks = elapsed_ticks(start);
	PenNormal();

	draw_progress("3/8  CopyBits screen-screen");
	start = TickCount();
	for (i = 0; i < COPY_ITERATIONS; ++i) {
		CopyBits(&qd.screenBits, &qd.screenBits,
			 &screen_src, &screen_dst, srcCopy, NULL);
	}
	results[2].name = "CopyBits screen-screen";
	results[2].iterations = COPY_ITERATIONS;
	results[2].ticks = elapsed_ticks(start);

	draw_progress("4/8  CopyBits screen-offscreen");
	start = TickCount();
	for (i = 0; i < COPY_ITERATIONS; ++i) {
		CopyBits(&qd.screenBits, &first,
			 &screen_src, &offscreen_rect, srcCopy, NULL);
	}
	results[3].name = "CopyBits screen-offscreen";
	results[3].iterations = COPY_ITERATIONS;
	results[3].ticks = elapsed_ticks(start);

	draw_progress("5/8  CopyBits offscreen-offscreen");
	start = TickCount();
	for (i = 0; i < COPY_ITERATIONS; ++i) {
		CopyBits(&first, &second,
			 &offscreen_rect, &offscreen_rect, srcCopy, NULL);
	}
	results[4].name = "CopyBits offscreen-offscreen";
	results[4].iterations = COPY_ITERATIONS;
	results[4].ticks = elapsed_ticks(start);

	draw_progress("6/8  CopyBits offscreen-screen");
	start = TickCount();
	for (i = 0; i < COPY_ITERATIONS; ++i) {
		CopyBits(&second, &qd.screenBits,
			 &offscreen_rect, &screen_dst, srcCopy, NULL);
	}
	results[5].name = "CopyBits offscreen-screen";
	results[5].iterations = COPY_ITERATIONS;
	results[5].ticks = elapsed_ticks(start);

	/* One iteration erases the old outline and draws the changed outline. */
	draw_progress("7/8  Moving rectangle");
	PenMode(patXor);
	FrameRect(&moving_rect);
	start = TickCount();
	for (i = 0; i < ANIMATION_ITERATIONS; ++i) {
		FrameRect(&moving_rect);
		OffsetRect(&moving_rect, (i & 1U) != 0U ? -32 : 32, 0);
		FrameRect(&moving_rect);
	}
	results[6].name = "Move rect (erase + draw)";
	results[6].iterations = ANIMATION_ITERATIONS;
	results[6].ticks = elapsed_ticks(start);
	FrameRect(&moving_rect);

	draw_progress("8/8  Scaling rectangle");
	PenMode(patXor);
	FrameRect(&scaling_rect);
	start = TickCount();
	for (i = 0; i < ANIMATION_ITERATIONS; ++i) {
		FrameRect(&scaling_rect);
		InsetRect(&scaling_rect, (i & 1U) != 0U ? -8 : 8,
			  (i & 1U) != 0U ? -6 : 6);
		FrameRect(&scaling_rect);
	}
	results[7].name = "Scale rect (erase + draw)";
	results[7].iterations = ANIMATION_ITERATIONS;
	results[7].ticks = elapsed_ticks(start);
	FrameRect(&scaling_rect);
	PenNormal();

	DisposePtr(first_pixels);
	DisposePtr(second_pixels);
}

static void draw_results(void)
{
	unsigned int i;

	EraseRect(&window_record.port.portRect);
	MoveTo(12, 18);
	draw_c_string("BENCHMARK COMPLETE (60 ticks/sec)");
	for (i = 0; i < RESULT_COUNT; ++i) {
		MoveTo(12, 40 + (int)i * 20);
		draw_c_string(results[i].name);
		if (results[i].iterations != 0U) {
			draw_c_string(": ");
			draw_number(results[i].ticks);
			draw_c_string(" ticks / ");
			draw_number(results[i].iterations);
			if (results[i].ticks != 0U) {
				draw_c_string("  (");
				draw_number((results[i].iterations * 60UL) /
					    results[i].ticks);
				draw_c_string(" ops/sec)");
			}
		}
	}
	MoveTo(12, 218);
	draw_c_string("IDLE - measurement stopped. R reruns; Cmd-Q exits.");
}

int main(void)
{
	Rect bounds;
	WindowPtr window;
	WindowPtr clicked_window;
	EventRecord event;
	Boolean done = false;
	short window_part;

	InitGraf(&qd.thePort);
	InitFonts();
	InitWindows();
	TEInit();
	InitCursor();

	SetRect(&bounds, 8, 28, 504, 274);
	window = NewWindow(&window_record, &bounds,
			   (ConstStringPtr)"\pMCX QD Bench",
			   true, documentProc, (WindowPtr)-1L, true, 0L);
	if (window == NULL) {
		return 1;
	}
	SetPort(window);

	while (!done) {
		run_benchmarks();
		draw_results();
		for (;;) {
			SystemTask();
			if (!GetNextEvent(everyEvent, &event)) {
				continue;
			}
			if (event.what == updateEvt) {
				BeginUpdate(window);
				draw_results();
				EndUpdate(window);
				continue;
			}
			if (event.what == keyDown || event.what == autoKey) {
				if ((event.modifiers & cmdKey) != 0 &&
				    (event.message & charCodeMask) == 'q') {
					done = true;
					break;
				}
				if ((event.message & charCodeMask) == 'r' ||
				    (event.message & charCodeMask) == 'R') {
					break;
				}
				continue;
			}
			if (event.what == mouseDown) {
				window_part = FindWindow(event.where, &clicked_window);
				if (window_part == inGoAway && clicked_window == window &&
				    TrackGoAway(window, event.where)) {
					done = true;
					break;
				}
			}
		}
	}
	DisposeWindow(window);
	return 0;
}
