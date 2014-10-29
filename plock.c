/*
 * File: plock.c
 * Time-stamp: <2014-10-29 23:09:26 pierre>
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
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/xpm.h>

/*
 * Because define defines
 */
#define WIN_WIDTH 1024
#define WIN_HEIGHT 768
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
int check_password(char *str)
{
    int is_valid = 0;

    fprintf(stdout, "Checking password [%s]\n", str);

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
    int padding = 20;
    int bottom_padding = 100;
    char *str = get_time();

    pthread_mutex_lock(&win_mutex);

    XSetForeground(display, gc, config.fg);

    if (!font) {
        font = XLoadQueryFont(display, config.font_string);
        if (font) {
            XSetFont(display, gc, font->fid);
        } else {
            fprintf(stderr, "Font %s not found\n", config.font_string);
        }
    }


    /* Time */
    length = XTextWidth(font, str, strlen(str));
    XDrawString(display, window, gc, WIN_WIDTH - length - padding, padding, str, strlen(str));

    /* Icon */
    XPutImage(display, window, gc, xicon, 0, 0, WIN_WIDTH / 2 - xicon->width / 2, WIN_HEIGHT - bottom_padding - padding - xicon->height, xicon->width, xicon->height);



    /* Dots */
    if (password[0] != 0) {
        XSetForeground(display, gc, 0x00353535);

        XFillArc(display, window, gc, WIN_WIDTH / 2 - 48, WIN_HEIGHT - bottom_padding - padding, 24, 24, 0, 360 * 64);
        XFillArc(display, window, gc, WIN_WIDTH / 2 - 12, WIN_HEIGHT - bottom_padding - padding, 24, 24, 0, 360 * 64);
        XFillArc(display, window, gc, WIN_WIDTH / 2 + 24, WIN_HEIGHT - bottom_padding - padding, 24, 24, 0, 360 * 64);

        XSetForeground(display, gc, config.fg);
    }

    /* Capslock */
    if (config.caps_lock) {
        str = "Capslock is activated";
        length = XTextWidth(font, str, strlen(str));
        XSetForeground(display, gc, config.fg);
        XDrawString(display, window, gc, WIN_WIDTH / 2 - length / 2, WIN_HEIGHT - bottom_padding + padding, str, strlen(str));
    }

    /* Locked at */
    length = XTextWidth(font, locked_at, strlen(locked_at));
    XDrawString(display, window, gc, WIN_WIDTH - length - padding, WIN_HEIGHT - padding, locked_at, strlen(locked_at));

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

    load_config();

    locked_at = malloc(19 * sizeof(char));
    sprintf(locked_at, "Locked at %s", get_time());

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

    /* Window */
    window = XCreateSimpleWindow(display, RootWindow(display, screen), 1, 1, WIN_WIDTH, WIN_HEIGHT, 0, 0, config.bg);
    XMapWindow(display, window);
    XFlush(display);
    XSelectInput(display, window, ExposureMask | KeyPressMask);
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