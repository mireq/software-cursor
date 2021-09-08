#include <getopt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XInput2.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/shape.h>


#define CHANGE_POS 1
#define CHANGE_SHAPE 2


static Display *display;
static Window window;
static Window root_window;
static int pixmap_size = 0;
static int highlight_radius = -1;
static uint32_t *cursor_pixmap_data;
static uint32_t *b1_highlight;
static uint32_t *b2_highlight;
static uint32_t *b3_highlight;
static uint32_t mouse_mask;


static int parse_size(char *optarg) {
	char *charval;
	int intval = strtol(optarg, &charval, 10);
	if (charval == optarg) {
		fprintf(stderr, "Not a number: %s\n", optarg);
		abort();
	}
	if (intval < 0) {
		fprintf(stderr, "Size cannot be negative: %s\n", optarg);
		abort();
	}
	if (intval > 2048) {
		fprintf(stderr, "Maximum size is 2048, but given: %s\n", optarg);
		abort();
	}
	return intval;
}


static void parse_options(int argc, char *argv[]) {
	int c;
	static struct option long_options[] =
	{
		{"size", required_argument, 0, 's'},
		{"highlight-radius", required_argument, 0, 'r'},
		{0, 0, 0, 0}
	};

	pixmap_size = 32 * 2 + 1;
	highlight_radius = -1;

	while (1) {
		int option_index = 0;
		c = getopt_long(argc, argv, "s:r:", long_options, &option_index);
		if (c == -1) {
			break;
		}
		switch (c) {
			case 0:
				break;
			case 's':
				pixmap_size = parse_size(optarg) * 2 + 1;
				break;
			case 'r':
				highlight_radius = parse_size(optarg);
				break;
			case '?':
				fprintf(stderr, "Usage %s [-s maximum-cursor-size] [-r highlight-radius]\n", argv[0]);
				exit(-1);
			default:
				abort();
		}
	}

	if (highlight_radius < 0) {
		highlight_radius = pixmap_size / 2;
	}
	else {
		if (highlight_radius > pixmap_size) {
			fprintf(stderr, "Highlight radius cannot be larger than size");
			abort();
		}
	}
}

static void make_highlight(uint32_t color, uint32_t *pixmap) {
	int radius_sq = highlight_radius * highlight_radius;
	//int radius_sq_start = highlight_radius * highlight_radius + highlight_radius;
	int offset = pixmap_size / 2;
	uint32_t base_alpha = (color & 0xff000000) >> 24;
	uint32_t base_color = color & 0x00ffffff;
	for (int y = -highlight_radius; y <= highlight_radius; ++y) {
		for (int x = -highlight_radius; x <= highlight_radius; ++x) {
			int dist_sq = x * x + y * y;
			if (dist_sq < radius_sq) {
				pixmap[x+offset + (y+offset)*pixmap_size] = base_color | (base_alpha << 24);
			}
			/*
			else if (dist_sq < radius_sq_start) {
				uint32_t aa_alpha = ((radius_sq_start - dist_sq - 1) * base_alpha) / highlight_radius;
				pixmap[x+offset + (y+offset)*pixmap_size] = base_color | (aa_alpha << 24);
			}
			*/
		}
	}
}


static void make_highlights() {
	memset(b1_highlight, 0, sizeof(uint32_t) * pixmap_size * pixmap_size);
	memset(b2_highlight, 0, sizeof(uint32_t) * pixmap_size * pixmap_size);
	memset(b3_highlight, 0, sizeof(uint32_t) * pixmap_size * pixmap_size);
	if (!highlight_radius) {
		return;
	}

	make_highlight(0x80808000, b1_highlight);
	make_highlight(0x80800080, b2_highlight);
	make_highlight(0x80008080, b3_highlight);
}


static void *check_malloc(size_t size) {
	void *mem = (void *)malloc(size);
	if (mem == NULL) {
		fprintf(stderr, "Out of memory");
		abort();
	}
	return mem;
}


#define MIN_VAL(a,b) (((a)<(b))?(a):(b))
#define MAX_VAL(a,b) (((a)>(b))?(a):(b))


static void update_cursor_pixmap() {
	XFixesCursorImage *img = XFixesGetCursorImage(display);

	Bool has_highlight = highlight_radius && (mouse_mask & (Button1Mask | Button2Mask | Button3Mask));
	if (has_highlight) {
		if (mouse_mask & Button1Mask) {
			memcpy(cursor_pixmap_data, b1_highlight, sizeof(uint32_t) * pixmap_size * pixmap_size);
		}
		else if (mouse_mask & Button2Mask) {
			memcpy(cursor_pixmap_data, b2_highlight, sizeof(uint32_t) * pixmap_size * pixmap_size);
		}
		else if (mouse_mask & Button3Mask) {
			memcpy(cursor_pixmap_data, b3_highlight, sizeof(uint32_t) * pixmap_size * pixmap_size);
		}
	}
	else {
		memset(cursor_pixmap_data, 0, sizeof(uint32_t) * pixmap_size * pixmap_size);
	}

	const int x = pixmap_size / 2 - img->xhot;
	const int y = pixmap_size / 2 - img->yhot;
	const size_t src_size = img->width * img->height;
	const size_t target_size = pixmap_size * pixmap_size;
	const size_t line_w = MIN_VAL(img->width + x, pixmap_size) - MAX_VAL(x, 0);
	const size_t src_skip = img->width - line_w;
	const size_t target_skip = pixmap_size - line_w;
	size_t src_pos = 0;
	size_t target_pos = 0;
	size_t x_pos = 0;
	size_t y_pos = 0;

	if (y < 0) {
		src_pos = (-y) * img->width;
	}
	if (x < 0) {
		src_pos -= x;
	}
	if (y > 0) {
		target_pos = y * pixmap_size;
	}
	if (x > 0) {
		target_pos += x;
	}

	if (has_highlight) {
		while (src_pos < src_size && target_pos < target_size) {
			uint32_t src_color = img->pixels[src_pos];
			uint32_t bg_color = cursor_pixmap_data[target_pos];
			uint32_t src_alpha = src_color >> 24;
			uint32_t bg_alpha = bg_color >> 24;
			uint32_t a_over = src_alpha + ((bg_alpha * (255 - src_alpha)) / 255);
			bg_alpha = ((bg_alpha * (255 - src_alpha)) / 255);
			uint32_t color = 0;
			if (a_over > 0) {
				color = (a_over << 24) |
					(((src_alpha * (src_color & 0xff)) + (bg_alpha * (bg_color & 0xff))) / a_over) |
					((((src_alpha * ((src_color & 0xff00 >> 8))) + (bg_alpha * ((bg_color & 0xff00) >> 8))) / a_over) << 8) |
					((((src_alpha * ((src_color & 0xff0000 >> 16))) + (bg_alpha * ((bg_color & 0xff0000) >> 16))) / a_over) << 16)
				;
			}
			cursor_pixmap_data[target_pos] = color;
			x_pos++;

			if (x_pos == line_w) {
				x_pos = 0;
				y_pos++;
				src_pos += src_skip;
				target_pos += target_skip;
			}
			src_pos++;
			target_pos++;
		}
	}
	else {
		while (src_pos < src_size && target_pos < target_size) {
			uint32_t src_color = img->pixels[src_pos];
			cursor_pixmap_data[target_pos] = src_color;
			x_pos++;

			if (x_pos == line_w) {
				x_pos = 0;
				y_pos++;
				src_pos += src_skip;
				target_pos += target_skip;
			}
			src_pos++;
			target_pos++;
		}
	}

	XFree(img);
}


static void set_window_hints() {
	char windowName[] = "SoftwareCursor";
	char windowClass[] = "softwarecursor";
	XClassHint *classhint = XAllocClassHint();
	classhint->res_name = windowName;
	classhint->res_class = windowClass;
	XSetClassHint(display, window, classhint);
	XFree(classhint);

	Atom ATOM = XInternAtom(display, "ATOM", False);
	Atom _NET_WM_STATE = XInternAtom(display, "_NET_WM_STATE", False);
	Atom _NET_WM_STATE_ABOVE = XInternAtom(display, "_NET_WM_STATE_ABOVE", False);
	Atom _NET_WM_WINDOW_TYPE = XInternAtom(display, "_NET_WM_WINDOW_TYPE", False);
	Atom _NET_WM_WINDOW_TYPE_POPUP_MENU = XInternAtom(display, "_NET_WM_WINDOW_TYPE_POPUP_MENU", False);
	XChangeProperty(display, window, _NET_WM_STATE, ATOM, 32, PropModeReplace, (unsigned char *) &_NET_WM_STATE_ABOVE, 1);
	XChangeProperty(display, window, _NET_WM_WINDOW_TYPE, ATOM, 32, PropModeReplace, (unsigned char *) &_NET_WM_WINDOW_TYPE_POPUP_MENU, 1);

	XEvent ev;
	memset(&ev, 0, sizeof(ev));
	ev.type = ClientMessage;
	ev.xclient.type = ClientMessage;
	ev.xclient.message_type = _NET_WM_STATE;
	ev.xclient.display = display;
	ev.xclient.window = window;
	ev.xclient.format = 32;
	ev.xclient.data.l[0] = 1; // set
	ev.xclient.data.l[1] = _NET_WM_STATE_ABOVE;
	XLockDisplay(display);
	XSendEvent(display, root_window, 0, SubstructureRedirectMask | SubstructureNotifyMask, &ev);
	XUnlockDisplay(display);
}


static void update_cursor_position() {
	int cursor_position_x = 0;
	int cursor_position_y = 0;
	Window root_cursor, child_cursor;
	int child_x, child_y;
	if (XQueryPointer(display, root_window, &root_cursor, &child_cursor, &cursor_position_x, &cursor_position_y, &child_x, &child_y, &mouse_mask)) {
		int offset = pixmap_size / 2;
		XMoveWindow(display, window, cursor_position_x - offset, cursor_position_y - offset);
	}
}


int main(int argc, char *argv[])
{
	parse_options(argc, argv);

	cursor_pixmap_data = (uint32_t *)check_malloc(sizeof(uint32_t) * pixmap_size * pixmap_size);
	b1_highlight = (uint32_t *)check_malloc(sizeof(uint32_t) * pixmap_size * pixmap_size);
	b2_highlight = (uint32_t *)check_malloc(sizeof(uint32_t) * pixmap_size * pixmap_size);
	b3_highlight = (uint32_t *)check_malloc(sizeof(uint32_t) * pixmap_size * pixmap_size);
	make_highlights();

	display = XOpenDisplay(NULL);
	root_window = DefaultRootWindow(display);

	int xfixes_event_base, xfixes_error_base;
	if (!XFixesQueryExtension(display, &xfixes_event_base, &xfixes_error_base)) {
		fprintf(stderr, "No Xfixes extension\n");
		return -1;
	}

	int xi_opcode, xi_event, xi_error;
	if (!XQueryExtension(display, "XInputExtension", &xi_opcode, &xi_event, &xi_error)) {
		fprintf(stderr, "No XInput extension\n");
		return -1;
	}

	unsigned char mask_bytes[(XI_LASTEVENT + 7) / 8] = {0};
	XISetMask(mask_bytes, XI_Motion);
	XISetMask(mask_bytes, XI_RawButtonPress);
	XISetMask(mask_bytes, XI_RawButtonRelease);

	XIEventMask evmasks[1];
	evmasks[0].deviceid = XIAllDevices;
	evmasks[0].mask_len = sizeof(mask_bytes);
	evmasks[0].mask = mask_bytes;
	XISelectEvents(display, root_window, evmasks, 1);

	int changes = 0;

	XVisualInfo vinfo;
	XMatchVisualInfo(display, XDefaultScreen(display), 32, TrueColor, &vinfo);

	XSetWindowAttributes attr;
	attr.colormap = XCreateColormap(display, root_window, vinfo.visual, AllocNone);
	attr.border_pixel = 0;
	attr.background_pixel = 0;
	attr.override_redirect = 0;
	attr.background_pixmap = None;

	window = XCreateWindow(display, root_window, 0, 0, pixmap_size, pixmap_size, 0, vinfo.depth, InputOutput, vinfo.visual, CWColormap | CWBorderPixel | CWBackPixel | CWEventMask | CWOverrideRedirect, &attr);

	XFixesSelectCursorInput(display, root_window, XFixesDisplayCursorNotifyMask);
	XSelectInput(display, window, ExposureMask | StructureNotifyMask | VisibilityChangeMask);
	XSelectInput(display, root_window, PropertyChangeMask | FocusChangeMask);

	// Transient to events
	attr.override_redirect = 1;
	XChangeWindowAttributes(display, window, CWOverrideRedirect, &attr);
	XserverRegion region = XFixesCreateRegion (display, NULL, 0);
	XFixesSetWindowShapeRegion(display, window, ShapeBounding, 0, 0, 0);
	XFixesSetWindowShapeRegion(display, window, ShapeInput, 0, 0, region);
	XFixesDestroyRegion(display, region);

	XMapWindow(display, window);

	XFlush(display);

	GC context = XCreateGC(display, window, 0, NULL);
	XImage *img = XCreateImage(display, vinfo.visual, vinfo.depth, ZPixmap, 0, (char *)cursor_pixmap_data, pixmap_size, pixmap_size, 32, 0);

	set_window_hints();
	update_cursor_pixmap();
	update_cursor_position();

	XEvent e;
	while (1) {
		XNextEvent(display, &e);
		while (1) {
			if (e.type == xfixes_event_base + XFixesCursorNotify) {
				changes |= CHANGE_SHAPE;
			}
			if (e.type == GenericEvent && e.xcookie.extension == xi_opcode) {
				XGetEventData(display, &e.xcookie);
				if (e.xcookie.evtype == XI_Motion) {
					changes |= CHANGE_POS;
				}
				else {
					changes |= CHANGE_SHAPE | CHANGE_POS;
					update_cursor_position();
					XRaiseWindow(display, window);
				}
				XFreeEventData(display, &e.xcookie);
			}
			if (e.type == Expose) {
				XPutImage(display, window, context, img, 0, 0, 0, 0, pixmap_size, pixmap_size);
			}
			if (e.type == VisibilityNotify || e.type == PropertyChangeMask || e.type == FocusChangeMask) {
				XRaiseWindow(display, window);
			}

			if (XPending(display)) {
				XNextEvent(display, &e);
			}
			else {
				break;
			}
		}
		if (changes) {
			if (changes & CHANGE_SHAPE) {
				update_cursor_pixmap();
				XPutImage(display, window, context, img, 0, 0, 0, 0, pixmap_size, pixmap_size);
			}
			if (changes & CHANGE_POS) {
				update_cursor_position();
			}
			changes = 0;
		}
	}
	free(cursor_pixmap_data);
	free(b1_highlight);
	free(b2_highlight);
	free(b3_highlight);
	XCloseDisplay(display);
	return 0;
}
