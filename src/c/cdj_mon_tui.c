/*
 * 
 * A small text ui library to write this
 * 

  CDJ monitor

  [XDJ-1000            ] id=02 type=1 playing=true bpm=120.00
  [XDJ-1000            ] id=03 type=1 playing=true bpm=120.00
  [VDJ-1000            ] id=05 type=1 playing=true bpm=120.00
  [rekordbox           ] id=17 type=1 playing=false
 * 
 */


#include <stdlib.h>
#include <string.h>

#include <stdio.h>
#include <termios.h>
#include <sys/ioctl.h>


#define TUI_NORMAL     "\033[0m"
#define TUI_BOLD       "\033[1m"

/**
 * termios uses columns (x) and rows (height - y)
 */
typedef struct  {
    int row;
    int col;
} dim;

static void tui_term_store();
static void tui_term_reset();
static void tui_write_bytes(const char* bytes, int len);
static dim tui_translate(int x, int y);
static char* crop(char* string, int max_len);
static char* fixed_length(char* string, int len);
static void tui_init(int y);
static void tui_exit();
static void tui_text_at(char* string, int x, int y);
static void tui_cdj_update(int pos, char* model_name, char* data);
static int tui_get_width();
static int tui_get_height();
static void tui_delete_line();
static void tui_set_window_title(const char* title);
static void tui_set_cursor_pos(int x, int y);
static void tui_cursor_off();
static void tui_cursor_on();


/**
 *
 */
static void tui_cdj_update(int slot, char* model_name, char* data)
{
    if (slot > tui_get_height()) return;

    tui_set_cursor_pos(0, 0);
    tui_delete_line();
    tui_set_cursor_pos(2, slot);
    tui_delete_line();
    putc('[', stdout);
    fputs(TUI_BOLD, stdout);
    fputs(fixed_length(model_name, 20), stdout);
    fputs(TUI_NORMAL, stdout);
    putc(']', stdout);
    tui_text_at(data, 24, slot);
    fflush(stdout);
}


static struct termios* term_orig = NULL;

static void tui_term_store()
{
    term_orig = (struct termios*) calloc(1, sizeof(struct termios));
    tcgetattr(0, term_orig);
}

static void tui_term_reset()
{
    if (term_orig) {
        tcsetattr(0, TCSANOW, term_orig);
        free(term_orig);
        term_orig = NULL;
    }
}

static void tui_write_bytes(const char* bytes, int len)
{
    for ( int i = 0 ; i < len; i++) {
        putchar(bytes[i] & 0xff);
    }
}

/*
 * convert x ,y from bottom corner to row col used by termio
 */
static dim tui_translate(int x, int y)
{
    dim rv;
    rv.row = tui_get_height() - y;
    rv.col = x + 1;
    return rv;
}

static char* crop(char* string, int max_len)
{
    char* new_string = malloc(max_len + 1);
    strncpy(new_string, string, max_len);
    new_string[max_len - 1] = '\0';
    return new_string;
}

/**
 * forces a string to a given length, cropping or padding with whitespace
 */
static char* fixed_length(char* string, int len)
{
    int min = strlen(string) < len ? strlen(string) : len;
    int i;
    char* new_string = malloc(len + 1);
    for (i = min; i < len; i++) new_string[i] = ' ';
    strncpy(new_string, string, min);
    new_string[len - 1] = '\0';
    return new_string;
}


static void tui_init(int y)
{
    int i;
    tui_cursor_off();
    tui_term_store();
    for (i = 0 ; i < y - 1 ; i++) puts("");
}

static void tui_exit()
{
    tui_set_cursor_pos(0, 0);
    printf("\n");
    tui_delete_line();
    tui_term_reset();
    tui_cursor_on();
}

static void tui_text_at(char* string, int x, int y)
{
    if (y > tui_get_height()) return;

    tui_set_cursor_pos(x, y);
    char* new_string = crop(string, tui_get_width() - x);
    fputs(new_string, stdout);
    free(new_string);
}

static int tui_get_width()
{
    struct winsize w;
    ioctl(0, TIOCGWINSZ, &w);
    return w.ws_col;
}

static int tui_get_height()
{
    struct winsize w;
    ioctl(0, TIOCGWINSZ, &w);
    return w.ws_row;
}

static void tui_delete_line()
{
    const char delete_line[] = { 27, 91, 50, 75 }; // ESC[2K
    tui_write_bytes(delete_line, 4);
}

static void tui_set_window_title(const char* title)
{
    const char set_window_title[] = { 27, 93, 50, 59 } ;
    tui_write_bytes(set_window_title, 4); // ESC]2;
    fputs(title, stdout);
    putc(7, stdout);// BEL
    fflush(stdout);
}

static void tui_set_cursor_pos(int x, int y)
{
    dim p = tui_translate(x, y);
    
    const char set_cursor_pos[] = { 27, 91 }; // ESC[
    tui_write_bytes(set_cursor_pos, 2);
    printf("%i", p.row);
    putc(59, stdout); // ;
    printf("%i", p.col);
    putc(102, stdout); // f
    fflush(stdout);
}

static void tui_cursor_off()
{
    const char cursor_off[] = { 27, 91, 63, 50, 53, 108 }; // ESC[ ?25l
    tui_write_bytes(cursor_off, 6);
    fflush(stdout);
}

static void tui_cursor_on()
{
    const char cursor_on[] = { 27, 91, 63, 50, 53, 104 }; // ESC[ ?25h
    tui_write_bytes(cursor_on, 6);
    fflush(stdout);
}
