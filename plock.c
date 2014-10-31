/*
 * File: plock.c
 * Time-stamp: <2014-10-31 17:34:11 pierre>
 * Copyright (C) 2014 Pierre Lecocq
 * Description: Plock - A screen locking system
 */

/*
 * Because include includes
 */
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <shadow.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/xpm.h>

/*
 * Because define defines
 */
#define BUFSIZE 255
#define TMPBUFSIZE 5

/*
 * Static variables
 */
static pthread_mutex_t win_mutex;
static Display *display;
static Window window;
static int screen;
static GC gc;
static XImage *xicon;
static char *locked_at;
static XFontStruct *font;
static char password[BUFSIZE];
static int win_width;
static int win_height;
static struct spwd *user_info;

typedef struct config {
    char *font_string;
    unsigned long bg;
    unsigned long fg;
    int caps_lock;
} st_config;

static st_config config;

/*
 * Get time as a string
 */
char *get_time()
{
    char *buf;
    char *fmt = "%H:%M:%S";
    time_t timer;
    struct tm *time_info;

    buf = malloc(9 * sizeof(char));
    time(&timer);
    time_info = localtime(&timer);
    strftime(buf, 9, fmt, time_info);

    return buf;
}

/*
 * Check password
 */
int check_password(char *passwd)
{
    int is_valid = 0;

    if (strcmp(user_info->sp_pwdp, passwd) == 0) {
        is_valid = 1;
    }

    return is_valid;
}

/*
 * Close display and release the prisonner
 */
void close_display()
{
    XDestroyWindow(display, window);
    XCloseDisplay(display);
    display = NULL;
}

/*
 * Update display
 */
void draw()
{
    int length;
    const int padding = 50;
    const int bottom_padding = (win_height * 0.25);
    char *str = get_time();

    pthread_mutex_lock(&win_mutex);

    XSetForeground(display, gc, config.fg);

    /* First round */
    if (!font) {
        font = XLoadQueryFont(display, config.font_string);
        if (font) {
            XSetFont(display, gc, font->fid);
        } else {
            fprintf(stderr, "Font %s not found\n", config.font_string);
            exit(1);
        }
    }

    /* Time */
    length = XTextWidth(font, str, strlen(str));
    XDrawString(display, window, gc, win_width - length - padding, padding, str, strlen(str));

    /* Icon */
    XPutImage(display, window, gc, xicon, 0, 0, win_width / 2 - xicon->width / 2, win_height - bottom_padding - padding - xicon->height, xicon->width, xicon->height);

    /* Dots */
    if (password[0] != 0) {
        XSetForeground(display, gc, 0x00353535);

        XFillArc(display, window, gc, win_width / 2 - 48, win_height - bottom_padding - padding, 24, 24, 0, 360 * 64);
        XFillArc(display, window, gc, win_width / 2 - 12, win_height - bottom_padding - padding, 24, 24, 0, 360 * 64);
        XFillArc(display, window, gc, win_width / 2 + 24, win_height - bottom_padding - padding, 24, 24, 0, 360 * 64);

        XSetForeground(display, gc, config.fg);
    }

    /* Capslock */
    if (config.caps_lock) {
        str = "Capslock is activated";
        length = XTextWidth(font, str, strlen(str));
        XSetForeground(display, gc, config.fg);
        XDrawString(display, window, gc, win_width / 2 - length / 2, win_height - bottom_padding + padding, str, strlen(str));
    }

    /* Locked at */
    length = XTextWidth(font, locked_at, strlen(locked_at));
    printf("%s\n", locked_at);
    XDrawString(display, window, gc, win_width - length - padding, win_height - padding, locked_at, strlen(locked_at));

    pthread_mutex_unlock(&win_mutex);
}

/*
 * Ping!
 */
void send_expose_event()
{
    XEvent event;

    memset(&event, 0, sizeof(event));
    event.type = Expose;
    event.xexpose.window = window;
    XSendEvent(display, window, False, ExposureMask, &event);

    XClearWindow(display, window);
    XFlush(display);
}

/*
 * Thread function for time
 */
void *f_time(void *argv)
{
    while (display != NULL) {
        send_expose_event();
        sleep(1);
    }

    pthread_exit(0);
}

/*
 * Thread function for password grabbing
 */
void *f_password(void *argv)
{
    int x;
    char tmp[BUFSIZE];
    XEvent event;
    KeySym keysym;
    int charcount;

    x = 0;
    memset(tmp, 0, TMPBUFSIZE);
    memset(password, 0, BUFSIZE);

    while (display != NULL) {
        XNextEvent(display, &event);

        if (event.type == KeyPress) {
            if (XLookupKeysym(&event.xkey, 0) == XK_Escape)  {
                /* Escape = reset */
                x = 0;
                memset(tmp, 0, TMPBUFSIZE);
                memset(password, 0, BUFSIZE);
            } else if (XLookupKeysym(&event.xkey, 0) == XK_Caps_Lock)  {
                /* Caps lock  = warning */
                config.caps_lock = !config.caps_lock;
            } else if (XLookupKeysym(&event.xkey, 0) == XK_Return)  {
                /* Return = check */
                if (check_password(password)) {
                    fprintf(stdout, "Access granted\n");
                    break;
                } else {
                    fprintf(stdout, "You failed!\n");
                }

                x = 0;
                memset(tmp, 0, TMPBUFSIZE);
                memset(password, 0, BUFSIZE);
            } else {
                /* Any key = store */
                if (x < BUFSIZE) {
                    charcount = XLookupString(&event.xkey, tmp, TMPBUFSIZE, &keysym, 0);
                    printf("Hit %d = %c\n", charcount, tmp[0]);
                    password[x] = tmp[0];
                    memset(tmp, 0, TMPBUFSIZE);
                    x++;
                    draw();
                }
            }
        }

        if (event.type == Expose) {
            draw();
        }
    }

    close_display();

    pthread_exit(0);
}

/*
 * Create an invisible cursor
 * credits: http://www.linuxforums.org/forum/programming-scripting/59012-xlib-hide-mouse-pointer.html
 */
void hide_cursor()
{
    Cursor cursor;
    Pixmap bm_no;
    Colormap color_map;
    XColor black, dummy;
    static char bm_no_data[] = {0, 0, 0, 0, 0, 0, 0, 0};

    color_map = DefaultColormap(display, screen);
    XAllocNamedColor(display, color_map, "black", &black, &dummy);
    bm_no = XCreateBitmapFromData(display, window, bm_no_data, 8, 8);
    cursor = XCreatePixmapCursor(display, bm_no, bm_no, &black, &black, 0, 0);

    XDefineCursor(display, window, cursor);
    XFreeCursor(display, cursor);
}

/*
 * Load config. Can not explain more.
 */
void load_config()
{
    config.font_string = "-*-helvetica-*-r-*-*-14-*-*-*-*-*-*-*";
    config.bg = 0x00252525;
    config.fg = 0x00858585;
    config.caps_lock = 0;
}

/*
 * Main function, obviously
 */
int main(int argc, char **argv)
{
    pthread_t th_password;
    pthread_t th_time;
    void *ret;
    Screen *displayed_screen;
    XSetWindowAttributes win_attr;

    /* Init */
    load_config();
    locked_at = malloc(19 * sizeof(char));
    sprintf(locked_at, "Locked at %s", get_time());

    user_info = getspnam(getenv('USER'));
    if (user_info == NULL) {
        fprintf(stderr, "Can not retrieve current user information\n");
        exit(1);
    }

    XInitThreads();

    /* Display */
    display = XOpenDisplay(NULL);
    if (display == NULL) {
        fprintf(stderr, "Cannot open display\n");
        exit(1);
    }

    /* Screens */
    fprintf(stdout, "Managing %d screen(s)\n", ScreenCount(display));
    screen = DefaultScreen(display);
    displayed_screen = DefaultScreenOfDisplay(display);
    win_width = displayed_screen->width;
    win_height = displayed_screen->height;

    /* Window */
    win_attr.override_redirect = 1;
    win_attr.background_pixel = config.bg;
    window = XCreateWindow(display, RootWindow(display, screen), 0, 0, win_width, win_height, 0, DefaultDepth(display, screen), CopyFromParent, DefaultVisual(display, screen), CWOverrideRedirect | CWBackPixel, &win_attr);
    XMapWindow(display, window);
    XFlush(display);
    XSelectInput(display, window, ExposureMask | KeyPressMask);

    /* Cursor */
    hide_cursor();

    /* GC */
    gc = DefaultGC(display, screen);

    /* Threads */
    if (XpmReadFileToImage(display, "icon.xpm", &xicon, NULL, NULL) < 0) {
        fprintf(stderr, "Cannot open icon.xpm\n");
        exit(1);
    }

    if (pthread_create(&th_password, NULL, f_password, NULL) < 0) {
        fprintf(stderr, "pthread_create error for thread password\n");
        exit(1);
    }

    if (pthread_create(&th_time, NULL, f_time, NULL) < 0) {
        fprintf(stderr, "pthread_create error for thread time\n");
        exit(1);
    }

    pthread_join(th_password, &ret);
    pthread_join(th_time, &ret);

    return(0);
}
