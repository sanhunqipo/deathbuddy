#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <cairo.h>
#include <cairo-xlib.h>
#include <X11/extensions/XInput2.h>
#include <X11/keysym.h>

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/select.h>
#include <signal.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>
#include <errno.h>

static int64_t now_ms(void) { 
	struct timespec t; 
	clock_gettime(CLOCK_MONOTONIC, &t); 
	return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000; 
}

static double scale = 0.6;
static int time_wait = 150;
static cairo_filter_t filter = CAIRO_FILTER_GOOD;

static volatile sig_atomic_t running = 1;

static void handle_signal(int sig)
{
    (void)sig;
    running = 0;
}

/* MASSIVELY EDITED FROM:
 *! Simple Cairo/Xlib example.
 * @author Bernhard R. Fischer, 2048R/5C5FFD47 <bf@abenteuerland.at>.
 * @version 2014110801
 * Compile with
 * gcc -Wall $(pkg-config --libs --cflags cairo x11) -o cairo_xlib_simple cairo_xlib_simple.c
 */
static void draw_image(cairo_surface_t *sfc, cairo_surface_t *img) {
   cairo_t *ctx;
   ctx = cairo_create(sfc);
   /* SOURCE: replace all pixels with source*/
   cairo_set_operator(ctx, CAIRO_OPERATOR_SOURCE);
   cairo_set_source_surface(ctx, img, 0, 0);
   cairo_paint(ctx);
   cairo_destroy(ctx);
   cairo_surface_flush(sfc);
   XFlush(cairo_xlib_surface_get_display(sfc));
}


/*! Open an X11 window and create a cairo surface base on that window.
 * @param x Width of window.
 * @param y Height of window.
 * @return Returns a pointer to a valid Xlib cairo surface. The function does
 * not return on error (exit(3)).
 */
cairo_surface_t *cairo_create_x11_surface0(int x, int y)
{
   Display *dsp;
   Drawable da;
   cairo_surface_t *sfc;

   /* connect to the X server named by $DISPLAY */
   if ((dsp = XOpenDisplay(NULL)) == NULL) {
	fprintf(stderr, "cannot open X display\n"); 
	exit(1); 
   }

   /* 32-bit color visual needed for transparency */
   XVisualInfo vinfo;
   if (!XMatchVisualInfo(dsp, DefaultScreen(dsp), 32, TrueColor, &vinfo)) { 
	fprintf(stderr, "no 32-bit TrueColor visual\n"); 
	exit(1); 
   }
   
   /* A window whose visual differs from its parent's (the root) must bring its
     * own colormap and border pixel, otherwise XCreateWindow fails with BadMatch. */
   XSetWindowAttributes attr;
   attr.colormap = XCreateColormap(dsp, DefaultRootWindow(dsp), vinfo.visual, AllocNone);
   attr.border_pixel = 0;
   attr.background_pixel = 0;
   attr.override_redirect = True;
   
    /* Which core events we want. Button events on the sprite come to us; while a
    * button is held X also grabs the pointer for us, so the motion and the
    * release keep arriving even if the cursor leaves the sprite. Keyboard events
    * are not selected: this window can never have focus (see select_raw_keys). */
   da = XCreateWindow(dsp, DefaultRootWindow(dsp), 0, 0, x, y, 0, vinfo.depth, InputOutput, vinfo.visual, CWColormap | CWBorderPixel | CWBackPixel | CWOverrideRedirect, &attr);
   XSelectInput(dsp, da, ButtonPressMask | ButtonReleaseMask | ButtonMotionMask | ExposureMask);
   
   XMapWindow(dsp, da);
   XRaiseWindow(dsp, da);

   sfc = cairo_xlib_surface_create(dsp, da, vinfo.visual, x, y);
   if (cairo_surface_status(sfc) != CAIRO_STATUS_SUCCESS) {
        fprintf(stderr, "cannot create cairo surface\n"); 
	exit(1); 
   }

   return sfc;
}


/*! Destroy cairo Xlib surface and close X connection.
 */
void cairo_close_x11_surface(cairo_surface_t *sfc)
{
   Display *dsp = cairo_xlib_surface_get_display(sfc);

   cairo_surface_destroy(sfc);
   XCloseDisplay(dsp);
}

static cairo_surface_t *scale_surface(cairo_surface_t *src, double s) {
   int w = (int)(cairo_image_surface_get_width(src) * s + 0.5);
   int h = (int)(cairo_image_surface_get_height(src) * s + 0.5);
   if (w < 1) w = 1; /* a zero-sized window would be a BadValue error from X */
   if (h < 1) h = 1;
   /* transparent window */
   cairo_surface_t *dst = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
   cairo_t *c = cairo_create(dst);
   cairo_scale(c, s, s);
   cairo_set_source_surface(c, src, 0, 0);
   cairo_pattern_set_filter(cairo_get_source(c), filter);
   cairo_paint(c);
   cairo_destroy(c);
   return dst;
}

static void usage(FILE *f) {
	fprintf(f, "usage: deathbuddy [-s scale] [-t ms] [-f nearest|smooth] [-h]\n");
}

int main(int argc, char **argv) {
   signal(SIGTERM, handle_signal);
   signal(SIGINT, handle_signal);
   signal(SIGHUP, handle_signal);
   signal(SIGPIPE, SIG_IGN);

   char *end;
   int opt;
   while ((opt = getopt(argc, argv, "s:t:f:h")) != -1) {
   	switch (opt) {
        	case 's':
			scale = strtod(optarg, &end);
			if (end == optarg ||*end || !(scale > 0 && scale <= 10)) {
				fprintf(stderr, "bad scale\n");
				return 1;
			} break;
    		case 't':
			time_wait = (int)strtol(optarg, &end, 10);
			if (end == optarg || *end || time_wait < 0 || time_wait > INT_MAX) {
				fprintf(stderr, "bad delay\n");
				return 1; 
			} break;
    		case 'f':
			if (!strcmp(optarg, "nearest"))
				filter = CAIRO_FILTER_NEAREST;
			else if (!strcmp(optarg, "smooth")) {
				filter = CAIRO_FILTER_GOOD;

			} else {
				fprintf(stderr, "bad filter\n");
				return 1;
			} break;
    		case 'h':
			usage(stdout);
			return 0;
    		default:
			usage(stderr);
			return 1;
    }
}

   cairo_surface_t *sfc;

   XEvent e;
   int gx = 0, gy = 0;

   cairo_surface_t *frames[6];
   int cur_img = 0;

   char *files[] = {"basic.png", "enter.png", "left_click.png", "right_click.png", "left_typing.png", "right_typing.png" };

   char exe[PATH_MAX];
   ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
   if (n < 0) { perror("readlink"); return 1; }
   exe[n] = 0;
   *strrchr(exe, '/') = 0;


   for (int i = 0; i < 6; i++)
   {
   	char path[PATH_MAX + 64];
	snprintf(path, sizeof path, "%s/resources/%s", exe, files[i]);
   	frames[i] = cairo_image_surface_create_from_png(path);
   	if (cairo_surface_status(frames[i]) != CAIRO_STATUS_SUCCESS) {
      		fprintf(stderr, "failed to load png: %s from path %s\n", cairo_status_to_string(cairo_surface_status(frames[i])), path);
      		return 1;
   	}
	cairo_surface_t *orig = frames[i];
	frames[i] = scale_surface(orig, scale);
	cairo_surface_destroy(orig);
   }

   int64_t deadline = 0;

   sfc = cairo_create_x11_surface0(cairo_image_surface_get_width(frames[cur_img]), cairo_image_surface_get_height(frames[cur_img]));

   Display *dsp = cairo_xlib_surface_get_display(sfc);
   Window win = cairo_xlib_surface_get_drawable(sfc);

   /* Extensions are addressed by an opcode the server assigns. Keep it to recognise XInput events later. */
   int xi_op, ev, err;
   
   if (!XQueryExtension(dsp, "XInputExtension", &xi_op, &ev, &err)) {
           fprintf(stderr, "the X server has no XInput extension\n");
	   return 1; 
   }

   int maj = 2, min = 1; /* 1 is the lowest that worked on my machine*/
    
   /* A client must announce the XI2 version it speaks before using it. The server writes back the version it will actually use (so major/minor may change); an error means XI2 is not available at all. */
   if (XIQueryVersion(dsp, &maj, &min) != Success) {
           fprintf(stderr, "XInput 2 is not available\n");
	   return 1; 
   }
    
   /* Event selection is a bitmask with one bit per event type. */
   unsigned char m[XIMaskLen(XI_LASTEVENT)] = {0};
   XISetMask(m, XI_RawKeyPress);
   XISetMask(m, XI_RawKeyRelease);
   XIEventMask em = { XIAllMasterDevices, sizeof(m), m };
   
   if (XISelectEvents(dsp, DefaultRootWindow(dsp), &em, 1) != Success) {
           fprintf(stderr, "cannot select raw key events\n");
	   return 1; 
   }   

   draw_image(sfc, frames[cur_img]);

   KeyCode ret = XKeysymToKeycode(dsp, XK_Return);
   KeyCode kpret = XKeysymToKeycode(dsp, XK_KP_Enter);

   int fd = ConnectionNumber(dsp);

   while (running) {
	   while (XPending(dsp)) {
		   XNextEvent(dsp, &e);
		   switch (e.type) {
			   case GenericEvent:
				/* What the fuck */
				if (XGetEventData(dsp, &e.xcookie) && e.xcookie.extension == xi_op) {
					XIRawEvent *re = (XIRawEvent *)e.xcookie.data;
					if (e.xcookie.evtype == XI_RawKeyPress && (re->detail == ret || re->detail == kpret)) {
						if (cur_img != 1) {
							cur_img = 1;
							deadline = 0;
							draw_image(sfc, frames[cur_img]);
						}
					}
					else if (e.xcookie.evtype == XI_RawKeyPress) {
						if (cur_img == 4) {
							cur_img = 5;
							deadline = 0;
							draw_image(sfc, frames[cur_img]);
						} else {
							cur_img = 4;
							deadline = 0;
							draw_image(sfc, frames[cur_img]);
						}
					}
					else if (e.xcookie.evtype == XI_RawKeyRelease) {
						if (cur_img != 0) {
							deadline = now_ms() + time_wait;
						}
					}
				}
				XFreeEventData(dsp, &e.xcookie);
				break;
			case ButtonPress:
				if (e.xbutton.button == Button1) {
					gx = e.xbutton.x;
					gy = e.xbutton.y;

					if (cur_img != 2) {
						cur_img = 2;
						deadline = 0;
						draw_image(sfc, frames[cur_img]);
					}
				} else if (e.xbutton.button == Button3) {
					if (cur_img != 3) {
						cur_img = 3;
						deadline = 0;
						draw_image(sfc, frames[cur_img]);
					}
				}
				break;
			case ButtonRelease:
				if (e.xbutton.button == Button1) {
					deadline = now_ms() + time_wait;
				} else if (e.xbutton.button == Button3) {
					deadline = now_ms() + time_wait + 1000;
				}
				break;
			case MotionNotify:
				if (e.xmotion.state & Button1Mask) XMoveWindow(dsp, win, e.xmotion.x_root - gx, e.xmotion.y_root - gy);
				break;
			case Expose:
				if (e.xexpose.count == 0) draw_image(sfc, frames[cur_img]);
				break;
		   }
	   }
	   if (deadline && now_ms() >= deadline) {
        	cur_img = 0;
		deadline = 0;
        	draw_image(sfc, frames[cur_img]);
    	} else if (!XPending(dsp)) {
        	fd_set fds;
        	FD_ZERO(&fds);
        	FD_SET(fd, &fds);

        	struct timeval tv, *tp = NULL;      /* NULL = sleep until an event */
        	if (deadline) {
            		int64_t left = deadline - now_ms();
            		if (left < 0) left = 0;
            		tv.tv_sec = left / 1000;
            		tv.tv_usec = (left % 1000) * 1000;
            		tp = &tv;
        	}
        	int rc = select(fd + 1, &fds, NULL, NULL, tp);
        	
        	if (rc < 0) {
            		if (errno == EINTR) {
		        	/* SIGTERM/SIGINT interrupted select(). */
		        	if (!running) {
		        		break;
		        	}
                		continue;
            		}		

		    	perror("select");
		   	break;
        	}
    	}
   }

   for (int i = 0; i < 6; i++) cairo_surface_destroy(frames[i]);
   cairo_close_x11_surface(sfc);

   return 0;
}
