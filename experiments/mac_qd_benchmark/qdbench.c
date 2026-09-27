/* SPDX-License-Identifier: MIT */
/* Small 68000 Macintosh QuickDraw benchmark for uMac targets. */

#include <Events.h>
#include <Fonts.h>
#include <Memory.h>
#include <Menus.h>
#include <OSUtils.h>
#include <Quickdraw.h>
#include <TextEdit.h>
#include <Windows.h>

#include <string.h>

enum {
	FILE_MENU_ID = 128,
	FILE_QUIT_ITEM = 1,
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
static WindowPtr app_window;
static MenuHandle file_menu;
static Boolean quit_requested;
static Boolean rerun_requested;
static Boolean benchmark_running;
static const char *benchmark_stage;

static void draw_results(void);

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
	benchmark_stage = stage;
	EraseRect(&window_record.port.portRect);
	MoveTo(12, 20);
	draw_c_string("MCX QD BENCH V2 - RUNNING");
	MoveTo(12, 46);
	draw_c_string(stage);
	MoveTo(12, 72);
	draw_c_string("Please wait for the COMPLETE results screen.");
	SystemTask();
}

static void handle_menu_choice(long choice)
{
	short menu_id = (short)((unsigned long)choice >> 16);
	short item = (short)(choice & 0xffffL);

	if (menu_id == FILE_MENU_ID && item == FILE_QUIT_ITEM) {
		quit_requested = true;
	}
	HiliteMenu(0);
}

static void handle_event(const EventRecord *event)
{
	WindowPtr clicked_window;
	short window_part;
	unsigned char key;
	Rect drag_bounds;

	if (event->what == updateEvt &&
	    (WindowPtr)event->message == app_window) {
		BeginUpdate(app_window);
		if (benchmark_running) {
			draw_progress(benchmark_stage);
		} else {
			draw_results();
		}
		EndUpdate(app_window);
		return;
	}
	if (event->what == keyDown || event->what == autoKey) {
		key = (unsigned char)(event->message & charCodeMask);
		if ((event->modifiers & cmdKey) != 0) {
			if (key == 'q' || key == 'Q') {
				quit_requested = true;
			} else {
				handle_menu_choice(MenuKey(key));
			}
		} else if (!benchmark_running && (key == 'r' || key == 'R')) {
			rerun_requested = true;
		}
		return;
	}
	if (event->what != mouseDown) {
		return;
	}
	window_part = FindWindow(event->where, &clicked_window);
	if (window_part == inMenuBar) {
		handle_menu_choice(MenuSelect(event->where));
	} else if (window_part == inGoAway && clicked_window == app_window &&
		   TrackGoAway(app_window, event->where)) {
		quit_requested = true;
	} else if (window_part == inDrag && clicked_window == app_window) {
		drag_bounds = qd.screenBits.bounds;
		drag_bounds.top += 24;
		DragWindow(app_window, event->where, &drag_bounds);
	}
}

static void service_events(void)
{
	EventRecord event;

	SystemTask();
	while (GetNextEvent(everyEvent, &event)) {
		handle_event(&event);
	}
}

static void service_timed(unsigned long *excluded_ticks)
{
	unsigned long start = TickCount();

	service_events();
	*excluded_ticks += elapsed_ticks(start);
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
	unsigned long excluded_ticks;
	unsigned long i;
	unsigned int result_index;

	benchmark_running = true;
	for (result_index = 0; result_index < RESULT_COUNT; ++result_index) {
		results[result_index].name = "Not run";
		results[result_index].iterations = 0;
		results[result_index].ticks = 0;
	}

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
		benchmark_running = false;
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
	excluded_ticks = 0;
	start = TickCount();
	for (i = 0; i < FRAME_ITERATIONS; ++i) {
		FrameRect(&frame_rect);
		if ((i & 0xffU) == 0U) {
			service_timed(&excluded_ticks);
			if (quit_requested) {
				goto finished;
			}
		}
	}
	results[0].name = "FrameRect screen";
	results[0].iterations = FRAME_ITERATIONS;
	results[0].ticks = elapsed_ticks(start) - excluded_ticks;

	draw_progress("2/8  PaintRect screen");
	PenMode(patXor);
	excluded_ticks = 0;
	start = TickCount();
	for (i = 0; i < PAINT_ITERATIONS; ++i) {
		PaintRect(&paint_rect);
		if ((i & 0xffU) == 0U) {
			service_timed(&excluded_ticks);
			if (quit_requested) {
				goto finished;
			}
		}
	}
	results[1].name = "PaintRect screen";
	results[1].iterations = PAINT_ITERATIONS;
	results[1].ticks = elapsed_ticks(start) - excluded_ticks;
	PenNormal();

	draw_progress("3/8  CopyBits screen-screen");
	excluded_ticks = 0;
	start = TickCount();
	for (i = 0; i < COPY_ITERATIONS; ++i) {
		CopyBits(&qd.screenBits, &qd.screenBits,
			 &screen_src, &screen_dst, srcCopy, NULL);
		if ((i & 0xffU) == 0U) {
			service_timed(&excluded_ticks);
			if (quit_requested) {
				goto finished;
			}
		}
	}
	results[2].name = "CopyBits screen-screen";
	results[2].iterations = COPY_ITERATIONS;
	results[2].ticks = elapsed_ticks(start) - excluded_ticks;

	draw_progress("4/8  CopyBits screen-offscreen");
	excluded_ticks = 0;
	start = TickCount();
	for (i = 0; i < COPY_ITERATIONS; ++i) {
		CopyBits(&qd.screenBits, &first,
			 &screen_src, &offscreen_rect, srcCopy, NULL);
		if ((i & 0xffU) == 0U) {
			service_timed(&excluded_ticks);
			if (quit_requested) {
				goto finished;
			}
		}
	}
	results[3].name = "CopyBits screen-offscreen";
	results[3].iterations = COPY_ITERATIONS;
	results[3].ticks = elapsed_ticks(start) - excluded_ticks;

	draw_progress("5/8  CopyBits offscreen-offscreen");
	excluded_ticks = 0;
	start = TickCount();
	for (i = 0; i < COPY_ITERATIONS; ++i) {
		CopyBits(&first, &second,
			 &offscreen_rect, &offscreen_rect, srcCopy, NULL);
		if ((i & 0xffU) == 0U) {
			service_timed(&excluded_ticks);
			if (quit_requested) {
				goto finished;
			}
		}
	}
	results[4].name = "CopyBits offscreen-offscreen";
	results[4].iterations = COPY_ITERATIONS;
	results[4].ticks = elapsed_ticks(start) - excluded_ticks;

	draw_progress("6/8  CopyBits offscreen-screen");
	excluded_ticks = 0;
	start = TickCount();
	for (i = 0; i < COPY_ITERATIONS; ++i) {
		CopyBits(&second, &qd.screenBits,
			 &offscreen_rect, &screen_dst, srcCopy, NULL);
		if ((i & 0xffU) == 0U) {
			service_timed(&excluded_ticks);
			if (quit_requested) {
				goto finished;
			}
		}
	}
	results[5].name = "CopyBits offscreen-screen";
	results[5].iterations = COPY_ITERATIONS;
	results[5].ticks = elapsed_ticks(start) - excluded_ticks;

	/* One iteration erases the old outline and draws the changed outline. */
	draw_progress("7/8  Moving rectangle");
	PenMode(patXor);
	FrameRect(&moving_rect);
	excluded_ticks = 0;
	start = TickCount();
	for (i = 0; i < ANIMATION_ITERATIONS; ++i) {
		FrameRect(&moving_rect);
		OffsetRect(&moving_rect, (i & 1U) != 0U ? -32 : 32, 0);
		FrameRect(&moving_rect);
		if ((i & 0xffU) == 0U) {
			service_timed(&excluded_ticks);
			if (quit_requested) {
				goto finished;
			}
		}
	}
	results[6].name = "Move rect (erase + draw)";
	results[6].iterations = ANIMATION_ITERATIONS;
	results[6].ticks = elapsed_ticks(start) - excluded_ticks;
	FrameRect(&moving_rect);

	draw_progress("8/8  Scaling rectangle");
	PenMode(patXor);
	FrameRect(&scaling_rect);
	excluded_ticks = 0;
	start = TickCount();
	for (i = 0; i < ANIMATION_ITERATIONS; ++i) {
		FrameRect(&scaling_rect);
		InsetRect(&scaling_rect, (i & 1U) != 0U ? -8 : 8,
			  (i & 1U) != 0U ? -6 : 6);
		FrameRect(&scaling_rect);
		if ((i & 0xffU) == 0U) {
			service_timed(&excluded_ticks);
			if (quit_requested) {
				goto finished;
			}
		}
	}
	results[7].name = "Scale rect (erase + draw)";
	results[7].iterations = ANIMATION_ITERATIONS;
	results[7].ticks = elapsed_ticks(start) - excluded_ticks;
	FrameRect(&scaling_rect);

finished:
	PenNormal();
	benchmark_running = false;

	DisposePtr(first_pixels);
	DisposePtr(second_pixels);
}

static void draw_results(void)
{
	unsigned int i;

	EraseRect(&window_record.port.portRect);
	MoveTo(12, 18);
	draw_c_string("MCX QD BENCH V2 - COMPLETE (60 ticks/sec)");
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
	EventRecord event;

	InitGraf(&qd.thePort);
	InitFonts();
	InitWindows();
	InitMenus();
	TEInit();
	InitCursor();

	ClearMenuBar();
	file_menu = NewMenu(FILE_MENU_ID, (ConstStr255Param)"\pFile");
	if (file_menu == NULL) {
		return 1;
	}
	AppendMenu(file_menu, (ConstStr255Param)"\pQuit/Q");
	InsertMenu(file_menu, 0);
	DrawMenuBar();

	SetRect(&bounds, 8, 42, 504, 300);
	app_window = NewWindow(&window_record, &bounds,
			   (ConstStringPtr)"\pMCX QD Bench V2",
			   true, documentProc, (WindowPtr)-1L, true, 0L);
	if (app_window == NULL) {
		DisposeMenu(file_menu);
		return 1;
	}
	SetPort(app_window);

	while (!quit_requested) {
		rerun_requested = false;
		run_benchmarks();
		if (quit_requested) {
			break;
		}
		draw_results();
		while (!quit_requested && !rerun_requested) {
			SystemTask();
			if (!GetNextEvent(everyEvent, &event)) {
				continue;
			}
			handle_event(&event);
		}
	}
	DisposeWindow(app_window);
	DisposeMenu(file_menu);
	ClearMenuBar();
	DrawMenuBar();
	return 0;
}
