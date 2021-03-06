/*
 * File: plock.c
 * Time-stamp: <2014-11-06 00:11:19 pierre>
 * Copyright (C) 2014 Pierre Lecocq
 * Description: Plock - A screen locking system
 */

/*
 * Because define defines
 */
#define _GNU_SOURCE /* getresuid */
#define VERSION "0.6"
#define BUFSIZE 255
#define TMPBUFSIZE 5
#define PASSWD_RESULT_RESET 0
#define PASSWD_RESULT_SUCCESS 1
#define PASSWD_RESULT_FAILURE 2

/*
 * Because include includes
 */
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include <pthread.h>
#include <time.h>
#include <crypt.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/xpm.h>
#include <pwd.h>
#include <shadow.h>

/*
 * Static variables
 */
static pthread_mutex_t win_mutex;
static Display *display;
static Window window;
static int screen;
static GC gc;
static XImage *xicon_lock;
static XImage *xicon_clock;
static int locked_at_ts;
static XFontStruct *font;
static char password[BUFSIZE];
static int win_width;
static int win_height;
static uid_t ruid = -1, euid = -1, suid = -1;
static int passwd_result;

typedef struct config {
    char *font_string;
    unsigned long bg;
    unsigned long fg;
    int caps_lock;
} st_config;

static st_config config;

/*
 * Icons
 */
static char *xpm_lock[] = {
    /* columns rows colors chars-per-pixel */
    "16 16 2 1",
    "  c #a5a5a5",
    ". c None",
    /* pixels */
    "......    ......",
    ".....      .....",
    "....  ...   ....",
    "....  ....  ....",
    "...  ......  ...",
    "...  ......  ...",
    "...   ....   ...",
    ".              .",
    ".              .",
    ".              .",
    ".              .",
    ".              .",
    ".              .",
    ".              .",
    ".              .",
    "..            .."
};

static char *xpm_clock[] = {
    /* columns rows colors chars-per-pixel */
    "16 16 2 1",
    "  c #a5a5a5",
    ". c None",
    /* pixels */
    ".....      .....",
    "...          ...",
    "..            ..",
    ".      .       .",
    ".      .       .",
    "       .        ",
    "       .        ",
    "       ..       ",
    "       ..       ",
    "        ..      ",
    "         ..     ",
    ".              .",
    ".              .",
    "..            ..",
    "...          ...",
    ".....      ....."
};

/*
 * Houston, we have a problem
 */
void puke(char *message)
{
    fprintf(stderr, "ERROR: %s\n", message);
    exit(EXIT_FAILURE);
}

/*
 * Print version and exit
 */
void version()
{
    printf("plock %s\n", VERSION);

    exit(EXIT_SUCCESS);
}

/*
 * Close everything
 */
void bye_bye_baby()
{
    XDestroyWindow(display, window);
    XCloseDisplay(display);
    display = NULL;

    memset(password, 0, BUFSIZE);
    free(xicon_lock);
    free(xicon_clock);
    free(font);
}

/*
 * Get time as a string
 */
char *get_time_string()
{
    char *buf;
    char *fmt = "%H:%M:%S";
    time_t timer;
    struct tm *time_info;

    buf = malloc(9 * sizeof(char));
    time(&timer);
    time_info = localtime(&timer);
    strftime(buf, 9, fmt, time_info);

    return(buf);
}

/*
 * Get "time ago" string representation
 */
char *time_ago(int timestamp)
{
    int elapsed_ts = time(NULL) - timestamp;
    int days = 0;
    int hours = 0;
    int minutes = 0;
    char *result;

    /* Calculations */
    if (elapsed_ts < 60) {
        return("Less than one minute ago");
    }

    days = elapsed_ts / 86400;
    elapsed_ts -= days * 86400;
    hours = elapsed_ts / 3600;
    elapsed_ts -= hours * 3600;
    minutes = elapsed_ts / 60;

    /* String representation */
    result = malloc(20 * sizeof(char));
    memset(result, 0, 20);
    if (days) {
        sprintf(result, "%s%dd ", result, days);
    }

    if (hours) {
        sprintf(result, "%s%dh ", result, hours);
    }

    if (minutes){
        sprintf(result, "%s%dmn", result, minutes);
    }

    sprintf(result, "%s ago", result);

    return(result);
}

/*
 * Check password
 */
int check_password(char *passwd)
{
    const char *sys_passwd = NULL;
    struct passwd *user_info;
    struct spwd *user_info_shadowed;

    user_info = getpwuid(euid);
    if (user_info == NULL) {
        puke("Cannot get user from getpwuid");
    }

    if (strcmp(user_info->pw_passwd, "x") == 0) {
        if (seteuid(0) < 0) {
            puke("Cannot set higher privileges");
        }

        user_info_shadowed = getspnam(user_info->pw_name);
        if (user_info_shadowed == NULL) {
            puke("Cannot get user from getspnam");
        }

        endspent();
        sys_passwd = user_info_shadowed->sp_pwdp;

        if (seteuid(euid) < 0) {
            puke("Cannot go back to original privileges");
        }
    } else {
        endpwent();
        sys_passwd = user_info->pw_passwd;
    }

    return (strcmp(sys_passwd, crypt(passwd, sys_passwd)) == 0);
}

/*
 * Update display
 */
void draw()
{
    char *str;
    int length;
    const int padding = 50;

    /* Lock mutex */
    pthread_mutex_lock(&win_mutex);

    XSetForeground(display, gc, config.fg);

    /* First round */
    if (!font) {
        font = XLoadQueryFont(display, config.font_string);
        if (font) {
            XSetFont(display, gc, font->fid);
        } else {
            puke("Font not found");
        }
    }

    /* Icon Lock */
    XPutImage(display, window, gc, xicon_lock, 0, 0, padding, padding, xicon_lock->width, xicon_lock->height);

    /* Locked at */
    str = time_ago(locked_at_ts);
    length = XTextWidth(font, str, strlen(str));
    XDrawString(display, window, gc, padding + xicon_lock->width + 12, padding + 14, str, strlen(str));

    /* Time & clock icon */
    str = get_time_string();
    length = XTextWidth(font, str, strlen(str));
    XDrawString(display, window, gc, win_width - length - padding, padding + 14, str, strlen(str));
    XPutImage(display, window, gc, xicon_clock, 0, 0,
              win_width - length - xicon_lock->width - padding - 12, padding, xicon_lock->width, xicon_lock->height);

    /* Hint */
    if (password[0] != 0) {
        str = "Press Enter to validate your password or Escape to cancel";
        length = XTextWidth(font, str, strlen(str));
        XSetForeground(display, gc, config.fg);
        XDrawString(display, window, gc, (win_width / 2) - (length / 2), win_height - (win_height * 0.20), str, strlen(str));
    }

    /* Password result */
    if (passwd_result != PASSWD_RESULT_RESET) {
        str = (passwd_result == PASSWD_RESULT_SUCCESS) ? "Yeah!" : "Wrong password";
        length = XTextWidth(font, str, strlen(str));
        XSetForeground(display, gc, config.fg);
        XDrawString(display, window, gc, (win_width / 2) - (length / 2), win_height - (win_height * 0.20) - padding, str, strlen(str));
        passwd_result = PASSWD_RESULT_RESET;
    }

    /* Capslock */
    if (config.caps_lock) {
        str = "Hint: Caps Lock is activated";
        length = XTextWidth(font, str, strlen(str));
        XSetForeground(display, gc, config.fg);
        XDrawString(display, window, gc, win_width - length - padding, win_height - padding, str, strlen(str));
    }

    /* Unlock mutex */
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
                draw();
            } else if (XLookupKeysym(&event.xkey, 0) == XK_Caps_Lock)  {
                /* Caps lock = warning */
                config.caps_lock = !config.caps_lock;
                draw();
            } else if (XLookupKeysym(&event.xkey, 0) == XK_Return)  {
                /* Return = check */
                if (password[0] != 0) {
                    if (check_password(password) == 1) {
                        passwd_result = PASSWD_RESULT_SUCCESS;
                        break;
                    } else {
                        passwd_result = PASSWD_RESULT_FAILURE;
                    }

                    x = 0;
                    memset(tmp, 0, TMPBUFSIZE);
                    memset(password, 0, BUFSIZE);
                    draw();
                }
            } else {
                /* Any key = store */
                if (x < BUFSIZE) {
                    XLookupString(&event.xkey, tmp, TMPBUFSIZE, &keysym, 0);

                    if (
                        IsCursorKey(keysym)
                        || IsFunctionKey(keysym)
                        || IsKeypadKey(keysym)
                        || IsMiscFunctionKey(keysym)
                        || IsModifierKey(keysym)
                        || IsPFKey(keysym)
                        || IsPrivateKeypadKey(keysym)
                    ) {
                        continue;
                    }

                    password[x] = tmp[0];
                    memset(tmp, 0, TMPBUFSIZE);
                    x++;
                    draw();
                }
            }
        } else if (event.type == Expose) {
            draw();
        }
    }

    bye_bye_baby();

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
 * Get config. Can not explain more.
 */
void get_config(int argc, char **argv)
{
    int x;
    struct option longopts[] = {
        {"bg", optional_argument, NULL, 'b'},
        {"fg", optional_argument, NULL, 'f'},
        {"version", optional_argument, NULL, 'v'},
        {0, 0, 0, 0}
    };

    /* Defaults */
    config.font_string = "-*-helvetica-*-r-*-*-14-*-*-*-*-*-*-*";
    config.bg = 0x00252525;
    config.fg = 0x00858585;
    config.caps_lock = 0;

    /* Listen to the user. Sometimes. Please. */
    if (argc > 1) {
        while ((x = getopt_long(argc, argv, "b:f:v", longopts, NULL)) != -1) {
            switch (x) {
            case 'b':
                config.bg = atol(optarg);
                break;

            case 'f':
                config.fg = atol(optarg);
                break;

            case 'v':
                version();
                break;

            default:
                ;
            }
        }
    }
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

    /* Sudo ? */
    getresuid(&ruid, &euid, &suid);
    if (ruid != 0) {
        puke("Please run the program with higher privileges (sudo)");
    }

    /* Init */
    passwd_result = PASSWD_RESULT_RESET;
    get_config(argc, argv);
    locked_at_ts = time(NULL);
    XInitThreads();

    /* Display */
    display = XOpenDisplay(NULL);
    if (display == NULL) {
        puke("Cannot open display");
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
    window = XCreateWindow(display, RootWindow(display, screen),
                           0, 0, win_width, win_height,
                           0, DefaultDepth(display, screen), CopyFromParent,
                           DefaultVisual(display, screen),
                           CWOverrideRedirect | CWBackPixel, &win_attr);
    XMapWindow(display, window);
    XFlush(display);
    XSelectInput(display, window, ExposureMask | KeyPressMask);
    XGrabKeyboard(display, window, True, GrabModeAsync, GrabModeAsync, CurrentTime);

    /* Cursor */
    hide_cursor();

    /* GC */
    gc = DefaultGC(display, screen);

    /* Icons */
    if (XpmCreateImageFromData(display, xpm_lock, &xicon_lock, NULL, NULL) < 0) {
        puke("Cannot load image lock");
    }

    if (XpmCreateImageFromData(display, xpm_clock, &xicon_clock, NULL, NULL) < 0) {
        puke("Cannot load image clock");
    }

    /* Threads */
    if (pthread_create(&th_password, NULL, f_password, NULL) < 0) {
        puke("pthread_create error for thread password");
    }

    if (pthread_create(&th_time, NULL, f_time, NULL) < 0) {
        puke("pthread_create error for thread time");
    }

    pthread_join(th_password, &ret);
    pthread_join(th_time, &ret);

    return(EXIT_SUCCESS);
}
