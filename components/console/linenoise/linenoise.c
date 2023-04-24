/* linenoise.c -- guerrilla line editing library against the idea that a
 * line editing lib needs to be 20,000 lines of C code.
 *
 * You can find the latest source code at:
 *
 *   http://github.com/antirez/linenoise
 *
 * Does a number of crazy assumptions that happen to be true in 99.9999% of
 * the 2010 UNIX computers around.
 * ------------------------------------------------------------------------
 * Heavy modifications for better user experience by Ci4Rail GmbH, 2023:
 * - avoid complete redraw of the line for simple insertions and edits
 * 
 * Restrictions:
 * - editing doesn't work if line exceeeds terminal width
 * ------------------------------------------------------------------------
 *
 * Copyright (c) 2010-2023, Salvatore Sanfilippo <antirez at gmail dot com>
 * Copyright (c) 2010-2013, Pieter Noordhuis <pcnoordhuis at gmail dot com>
 * Copyright (c) 2023, Ci4Rail GmbH, <enginering at ci4rail dot com>
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *  *  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *  *  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ------------------------------------------------------------------------
 *
 * References:
 * - http://invisible-island.net/xterm/ctlseqs/ctlseqs.html
 * - http://www.3waylabs.com/nw/WWW/products/wizcon/vt220.html
 *
 * Todo list:
 * - Filter bogus Ctrl+<char> combinations.
 * - Win32 support
 *
 * Bloat:
 * - History search like Ctrl+r in readline?
 *
 * List of escape sequences used by this program, we do everything just
 * with three sequences. In order to be so cheap we may have some
 * flickering effect with some slow terminal, but the lesser sequences
 * the more compatible.
 *
 * EL (Erase Line)
 *    Sequence: ESC [ n K
 *    Effect: if n is 0 or missing, clear from cursor to end of line
 *    Effect: if n is 1, clear from beginning of line to cursor
 *    Effect: if n is 2, clear entire line
 *
 * CUF (CUrsor Forward)
 *    Sequence: ESC [ n C
 *    Effect: moves cursor forward n chars
 *
 */

#include <termios.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "linenoise.h"

#define LINENOISE_DEFAULT_HISTORY_MAX_LEN 100
#define LINENOISE_MAX_LINE 512
static linenoiseCompletionCallback *completionCallback = NULL;
static linenoiseHintsCallback *hintsCallback = NULL;
static linenoiseFreeHintsCallback *freeHintsCallback = NULL;
static void refreshLineWithCompletion(struct linenoiseState *ls, linenoiseCompletions *lc);
static void modifyPos(struct linenoiseState *l, int pos);
static void abAppend(struct abuf *ab, const char *s, int len);
int linenoiseHistoryAdd(const char *line);
static void refreshLine(struct linenoiseState *l);

static struct termios orig_termios; /* In order to restore at exit.*/
static int maskmode = 0;            /* Show "***" instead of input. For passwords. */
static int rawmode = 0;             /* For atexit() function to check if restore is needed*/
static int history_max_len = LINENOISE_DEFAULT_HISTORY_MAX_LEN;
static int history_len = 0;
static char **history = NULL;

enum KEY_ACTION {
    KEY_NULL = 0,   /* NULL */
    CTRL_A = 1,     /* Ctrl+a */
    CTRL_B = 2,     /* Ctrl-b */
    CTRL_C = 3,     /* Ctrl-c */
    CTRL_D = 4,     /* Ctrl-d */
    CTRL_E = 5,     /* Ctrl-e */
    CTRL_F = 6,     /* Ctrl-f */
    CTRL_H = 8,     /* Ctrl-h */
    TAB = 9,        /* Tab */
    CTRL_K = 11,    /* Ctrl+k */
    CTRL_L = 12,    /* Ctrl+l */
    ENTER = 13,     /* Enter */
    CTRL_N = 14,    /* Ctrl-n */
    CTRL_P = 16,    /* Ctrl-p */
    CTRL_R = 18,    /* Ctrl-r */
    CTRL_T = 20,    /* Ctrl-t */
    CTRL_U = 21,    /* Ctrl+u */
    CTRL_W = 23,    /* Ctrl+w */
    ESC = 27,       /* Escape */
    BACKSPACE = 127 /* Backspace */
};


#if 0
// to test how it feels with a slow terminal
int my_slow_write(int fd, const void *buf, size_t count) {
    const char *p = buf;
    int rv = count;
    while (count--) {
        if (write(fd, p++, 1) == -1) {
            return -1;
        }
        usleep(30000);
    }
    return rv;
}


#define write(fd,buf,count) my_slow_write(fd,buf,count)
#endif


/* ======================= Low level terminal handling ====================== */

static void doWrite(struct linenoiseState *l, const char *s, size_t len)
{
    write(l->ofd, s, len);
}

static void flushABuffer(struct linenoiseState *l)
{
    if (l->ab.len) {
        doWrite(l, l->ab.b, l->ab.len);
        l->ab.len = 0;
    }
}


/* Enable "mask mode". When it is enabled, instead of the input that
 * the user is typing, the terminal will just display a corresponding
 * number of asterisks, like "****". This is useful for passwords and other
 * secrets that should not be displayed. */
void linenoiseMaskModeEnable(void)
{
    maskmode = 1;
}

/* Disable mask mode. */
void linenoiseMaskModeDisable(void)
{
    maskmode = 0;
}


/* Raw mode: 1960 magic shit. */
static int enableRawMode(int fd)
{
    struct termios raw;

    if (!isatty(STDIN_FILENO))
        goto fatal;
    if (tcgetattr(fd, &orig_termios) == -1)
        goto fatal;

    raw = orig_termios; /* modify the original mode */
    /* input modes: no break, no CR to NL, no parity check, no strip char,
     * no start/stop output control. */
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    /* output modes - disable post processing */
    raw.c_oflag &= ~(OPOST);
    /* control modes - set 8 bit chars */
    raw.c_cflag |= (CS8);
    /* local modes - choing off, canonical off, no extended functions,
     * no signal chars (^Z,^C) */
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    /* control chars - set return condition: min number of bytes and timer.
     * We want read to return every single byte, without timeout. */
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0; /* 1 byte, no timer */

    /* put terminal in raw mode after flushing */
    if (tcsetattr(fd, TCSAFLUSH, &raw) < 0)
        goto fatal;
    rawmode = 1;
    return 0;

fatal:
    errno = ENOTTY;
    return -1;
}

static void disableRawMode(int fd)
{
    /* Don't even check the return value as it's too late. */
    if (rawmode && tcsetattr(fd, TCSAFLUSH, &orig_termios) != -1)
        rawmode = 0;
}


/* Beep, used for completion when there is nothing to complete or when all
 * the choices were already shown. */
static void linenoiseBeep(struct linenoiseState *l)
{
    abAppend(&l->ab, "\x7", 1);
}

/* ============================== Completion ================================ */

/* Free a list of completion option populated by linenoiseAddCompletion(). */
static void freeCompletions(linenoiseCompletions *lc)
{
    size_t i;
    for (i = 0; i < lc->len; i++){
        if (lc->cvec[i] != NULL){
            free(lc->cvec[i]);
            lc->cvec[i] = NULL;
        }
    }
    if (lc->cvec != NULL){
        free(lc->cvec);
        lc->cvec = NULL;
    }
}

/* Called by completeLine() and linenoiseShow() to render the current
 * edited line with the proposed completion. If the current completion table
 * is already available, it is passed as second argument, otherwise the
 * function will use the callback to obtain it.
 *
 */
static void refreshLineWithCompletion(struct linenoiseState *l, linenoiseCompletions *lc)
{
    /* Obtain the table of completions if the caller didn't provide one. */
    linenoiseCompletions ctable = {0, NULL};
    if (lc == NULL) {
        completionCallback(l->buf, &ctable);
        lc = &ctable;
    }

    /* Show the edited line with completion if possible, or just refresh. */
    if (l->completion_idx < lc->len) {
        l->len = strlen(lc->cvec[l->completion_idx]);
        strncpy(l->buf, lc->cvec[l->completion_idx], l->buflen-1 );
        modifyPos(l, l->len);
        refreshLine(l);

    } else {
        refreshLine(l);
    }

    /* Free the completions table if needed. */
    if (lc != &ctable)
        freeCompletions(&ctable);
}

/* This is an helper function for linenoiseEdit*() and is called when the
 * user types the <tab> key in order to complete the string currently in the
 * input.
 *
 * The state of the editing is encapsulated into the pointed linenoiseState
 * structure as described in the structure definition.
 *
 * If the function returns non-zero, the caller should handle the
 * returned value as a byte read from the standard input, and process
 * it as usually: this basically means that the function may return a byte
 * read from the termianl but not processed. Otherwise, if zero is returned,
 * the input was consumed by the completeLine() function to navigate the
 * possible completions, and the caller should read for the next characters
 * from stdin. */
static int completeLine(struct linenoiseState *l, int keypressed)
{
    linenoiseCompletions lc = {0, NULL};
    int nwritten;
    char c = keypressed;

    completionCallback(l->completion_str, &lc);
    if (lc.len == 0) {
        linenoiseBeep(l);
        l->in_completion = 0;
    } else {
        switch (c) {
        case TAB: /* tab */
            if (l->in_completion == 0) {
                l->in_completion = 1;
                l->completion_idx = 0;
            } else {
                l->completion_idx = (l->completion_idx + 1) % (lc.len );
                if (l->completion_idx == lc.len)
                    linenoiseBeep(l);
            }
            c = 0;
            break;
        case 27: /* escape */
            /* Re-show original buffer */
            strcpy(l->buf, l->completion_str);
            l->len = strlen(l->buf);
            modifyPos(l, l->len);
            if (l->completion_idx < lc.len)
                refreshLine(l);
            l->in_completion = 0;
            c = 0;
            break;
        default:
            /* Update buffer and return */
            if (l->completion_idx < lc.len) {
                nwritten = snprintf(l->buf, l->buflen, "%s", lc.cvec[l->completion_idx]);
                l->len = nwritten;
                modifyPos(l, l->len);
            }
            l->in_completion = 0;
            break;
        }

        /* Show completion or original buffer */
        if (l->in_completion && l->completion_idx < lc.len) {
            refreshLineWithCompletion(l, &lc);
        } else {
            refreshLine(l);
        }
    }
    if(!l->in_completion){
        free(l->completion_str);
        l->completion_str = NULL;
    }

    freeCompletions(&lc);
    return c; /* Return last read character */
}

/* Register a callback function to be called for tab-completion. */
void linenoiseSetCompletionCallback(linenoiseCompletionCallback *fn)
{
    completionCallback = fn;
}

/* Register a hits function to be called to show hits to the user at the
 * right of the prompt. */
void linenoiseSetHintsCallback(linenoiseHintsCallback *fn)
{
    hintsCallback = fn;
}

/* Register a function to free the hints returned by the hints callback
 * registered with linenoiseSetHintsCallback(). */
void linenoiseSetFreeHintsCallback(linenoiseFreeHintsCallback *fn)
{
    freeHintsCallback = fn;
}

/* This function is used by the callback function registered by the user
 * in order to add completion options given the input string when the
 * user typed <tab>. See the example.c source code for a very easy to
 * understand example. */
void linenoiseAddCompletion(linenoiseCompletions *lc, const char *str)
{
    size_t len = strlen(str);
    char *copy, **cvec;

    copy = malloc(len + 1);
    if (copy == NULL)
        return;
    strcpy(copy, str);
    cvec = realloc(lc->cvec, sizeof(char *) * (lc->len + 1));
    if (cvec == NULL) {
        free(copy);
        return;
    }
    lc->cvec = cvec;
    lc->cvec[lc->len] = copy;
    lc->len++;
}

/* =========================== Line editing ================================= */


static void abInit(struct abuf *ab)
{
    ab->b = NULL;
    ab->len = 0;
}

static void abAppend(struct abuf *ab, const char *s, int len)
{
    char *new = realloc(ab->b, ab->len + len);

    if (new == NULL)
        return;
    memcpy(new + ab->len, s, len);
    ab->b = new;
    ab->len += len;
}

static void abFree(struct abuf *ab)
{
    if(ab->b){
        free(ab->b);
        ab->b = NULL;
    }
}


static void moveCursorLeftWithBackspace(struct linenoiseState *l, int n)
{
    while(n--) {
        abAppend(&l->ab, "\x08", 1);
    }
}

static void moveCursorRight(struct linenoiseState *l, int n)
{
    char seq[32];
    snprintf(seq, sizeof(seq), "\x1b[%dC", n);
    abAppend(&l->ab, seq, strlen(seq));
}

static void printFromPosToRight(struct linenoiseState *l, int max)
{
    int len = l->len - l->pos;
    if( max != -1 ){
        if( len > max ){
            len = max;
        }
    }
    if( len > 0){
        abAppend(&l->ab, &l->buf[l->pos], len);
    }
}

static void modifyPos(struct linenoiseState *l, int pos)
{
    if (pos < 0)
        pos = 0;
    else if (pos > l->len)
        pos = l->len;
    if (pos < l->pos) {
        moveCursorLeftWithBackspace(l, l->pos - pos);
    } else if (pos > l->pos) {
        moveCursorRight(l, pos - l->pos);
    }
    l->pos = pos;
}


static void showHint(struct linenoiseState *l, char *hint, int color, int bold)
{
    int hintlen = strlen(hint);
    char seq[64];

    printFromPosToRight(l, -1); // ensure the cursor is at the end of the line
    if (bold == 1 && color == -1)
        color = 37;
    if (color != -1 || bold != 0)
        snprintf(seq, 64, "\033[%d;%d;49m", bold, color);
    else
        seq[0] = '\0';
    abAppend(&l->ab, seq, strlen(seq));
    abAppend(&l->ab, hint, hintlen);
    if (color != -1 || bold != 0)
        abAppend(&l->ab, "\033[0m", 4);
    moveCursorLeftWithBackspace(l, hintlen + (l->len - l->pos));
    l->showing_hint = hint;
    l->hint_pos = l->pos;
}

static void freeHint(struct linenoiseState *l)
{
    if (l->showing_hint) {
        if (freeHintsCallback)
            freeHintsCallback(l->showing_hint);
        l->showing_hint = NULL;
    }
}

static void clearHint(struct linenoiseState *l)
{
    if( !l->showing_hint)
        return;

    int hintlen = strlen(l->showing_hint);
    int clearlen;
    int back;
    if( l->pos < l->hint_pos ){
        // the cursor is before the hint, so we need to move it to the right
        // before clearing the hint
        moveCursorRight(l, l->hint_pos - l->pos);
        clearlen = hintlen;
        back = hintlen + (l->hint_pos - l->pos);
    } else {
        // the cursor is after the hint or at the same position
        clearlen = hintlen - (l->pos - l->hint_pos);
        back = clearlen;
    }

    for( int i = 0; i < clearlen; i++ ){
        abAppend(&l->ab, " ", 1);
    }
    moveCursorLeftWithBackspace(l, back);
    freeHint(l);
}

static void handleHints(struct linenoiseState *l)
{
    int color = -1, bold = 0;
    
    if(!hintsCallback)
        return;
    
    abInit(&l->ab);
    char *hint = hintsCallback(l->buf, &color, &bold);
    if (hint) {
        if( l->showing_hint){
            if(strcmp(l->showing_hint, hint) != 0){
                // a hint is shown, but now there is a new one
                clearHint(l);
                showHint(l, hint, color, bold);
            } else {
                // the same hint is shown, nothing to do
            }
        } else {
            // no hint is shown, show the new one
            showHint(l, hint, color, bold);
        }
    } else {
        // no more hint, clear the shown one
        clearHint(l);
    }
    flushABuffer(l);
    abFree(&l->ab);
}

/* Single line low level line refresh.
 *
 * Rewrite the currently edited line accordingly to the buffer content,
 * and cursor position.
 */
static void refreshLine(struct linenoiseState *l)
{
    char seq[64];
    char *buf = l->buf;
    size_t len = l->len;

    moveCursorLeftWithBackspace(l, l->pos);

    /* Erase to right */
    snprintf(seq, sizeof(seq), "\x1b[0K");
    abAppend(&l->ab, seq, strlen(seq));

    /* Write current buffer content */
    if (maskmode == 1) {
        while (len--)
            abAppend(&l->ab, "*", 1);
    } else {
        abAppend(&l->ab, buf, len);
    }
    if( l->pos < l->len ){
        /* Move cursor to original position. */
        moveCursorLeftWithBackspace(l, l->len - l->pos);
    }
}


/* Insert the character 'c' at cursor current position.
 *
 * On error writing to the terminal -1 is returned, otherwise 0. */
int linenoiseEditInsert(struct linenoiseState *l, char c)
{
    if (l->len < l->buflen) {
        char d = (maskmode == 1) ? '*' : c;
        abAppend(&l->ab, &d, 1);
        if (l->len == l->pos) {
            l->buf[l->pos] = c;
            l->len++;
            l->pos++;
            l->buf[l->len] = '\0';
        } else {
            memmove(l->buf + l->pos + 1, l->buf + l->pos, l->len - l->pos);
            l->buf[l->pos] = c;
            l->len++;
            l->pos++;
            l->buf[l->len] = '\0';
            abAppend(&l->ab, l->buf + l->pos, l->len - l->pos);
            moveCursorLeftWithBackspace(l, l->len - l->pos);
        }
    }
    return 0;
}

/* Move cursor on the left. */
void linenoiseEditMoveLeft(struct linenoiseState *l)
{
    if (l->pos > 0) {
        modifyPos(l, l->pos - 1);
    }
}

/* Move cursor on the right. */
void linenoiseEditMoveRight(struct linenoiseState *l)
{
    if (l->pos != l->len) {
        modifyPos(l, l->pos + 1);
    }
}

/* Move cursor to the start of the line. */
void linenoiseEditMoveHome(struct linenoiseState *l)
{
    if (l->pos != 0) {
        modifyPos(l, 0);
    }
}

/* Move cursor to the end of the line. */
void linenoiseEditMoveEnd(struct linenoiseState *l)
{
    if (l->pos != l->len) {
        modifyPos(l, l->len);
    }
}


/* Substitute the currently edited line with the next or previous history
 * entry as specified by 'dir'. */
#define LINENOISE_HISTORY_NEXT 0
#define LINENOISE_HISTORY_PREV 1
void linenoiseEditHistoryNext(struct linenoiseState *l, int dir)
{
    if (history_len > 1) {
        /* Update the current history entry before to
         * overwrite it with the next one. */
        free(history[history_len - 1 - l->history_index]);
        history[history_len - 1 - l->history_index] = strdup(l->buf);
        /* Show the new entry */
        l->history_index += (dir == LINENOISE_HISTORY_PREV) ? 1 : -1;
        if (l->history_index < 0) {
            l->history_index = 0;
            return;
        } else if (l->history_index >= history_len) {
            l->history_index = history_len - 1;
            return;
        }
        strncpy(l->buf, history[history_len - 1 - l->history_index], l->buflen);
        l->buf[l->buflen - 1] = '\0';
        l->len = strlen(l->buf);
        modifyPos(l, l->len);
        refreshLine(l);
    }
}

/* Delete the character at the right of the cursor without altering the cursor
 * position. Basically this is what happens with the "Delete" keyboard key. */
void linenoiseEditDelete(struct linenoiseState *l)
{
    if (l->len > 0 && l->pos < l->len) {
        memmove(l->buf + l->pos, l->buf + l->pos + 1, l->len - l->pos - 1);
        l->len--;
        l->buf[l->len] = '\0';
        //refreshLine(l);
        abAppend(&l->ab, l->buf + l->pos, l->len - l->pos);
        abAppend(&l->ab, " ", 1);
        moveCursorLeftWithBackspace(l, l->len - l->pos +1);
    }
}

/* Backspace implementation. */
void linenoiseEditBackspace(struct linenoiseState *l)
{
    if (l->pos > 0 && l->len > 0) {
        memmove(l->buf + l->pos - 1, l->buf + l->pos, l->len - l->pos);
        l->pos--;
        l->len--;
        l->buf[l->len] = '\0';
        if(l->pos != l->len){
            moveCursorLeftWithBackspace(l, 1);
            abAppend(&l->ab, l->buf + l->pos, l->len - l->pos);
            abAppend(&l->ab, " ", 1);
            moveCursorLeftWithBackspace(l, l->len - l->pos + 1);
        } else {
            abAppend(&l->ab, "\x08 \x08", 3);
        }
    }
}

/* This function is part of the multiplexed API of Linenoise, that is used
 * in order to implement the blocking variant of the API but can also be
 * called by the user directly in an event driven program. It will:
 *
 * 1. Initialize the linenoise state passed by the user.
 * 2. Put the terminal in RAW mode.
 * 3. Show the prompt.
 * 4. Return control to the user, that will have to call linenoiseEditFeed()
 *    each time there is some data arriving in the standard input.
 *
 * When linenoiseEditFeed() returns non-NULL, the user finished with the
 * line editing session (pressed enter CTRL-D/C): in this case the caller
 * needs to call linenoiseEditStop() to put back the terminal in normal
 * mode. This will not destroy the buffer, as long as the linenoiseState
 * is still valid in the context of the caller.
 *
 * The function returns 0 on success, or -1 if writing to standard output
 * fails. If stdin_fd or stdout_fd are set to -1, the default is to use
 * STDIN_FILENO and STDOUT_FILENO.
 */
int linenoiseEditStart(struct linenoiseState *l,
    int stdin_fd,
    int stdout_fd,
    char *buf,
    size_t buflen,
    const char *prompt)
{
    /* Populate the linenoise state that we pass to functions implementing
     * specific editing functionalities. */
    l->in_completion = 0;
    l->ifd = stdin_fd != -1 ? stdin_fd : STDIN_FILENO;
    l->ofd = stdout_fd != -1 ? stdout_fd : STDOUT_FILENO;
    l->buf = buf;
    l->buflen = buflen;
    l->prompt = prompt;
    l->plen = strlen(prompt);
    l->pos = 0;
    l->len = 0;
    l->history_index = 0;
    l->showing_hint = NULL;
    l->hint_pos = 0;
    l->completion_str = NULL;

    /* Buffer starts empty. */
    l->buf[0] = '\0';
    l->buflen--; /* Make sure there is always space for the nulterm */

    /* If stdin is not a tty, stop here with the initialization. We
     * will actually just read a line from standard input in blocking
     * mode later, in linenoiseEditFeed(). */
    if (!isatty(l->ifd))
        return 0;

    /* Enter raw mode. */
    if (enableRawMode(l->ifd) == -1)
        return -1;

    /* The latest history entry is always our current buffer, that
     * initially is just an empty string. */
    linenoiseHistoryAdd("");

    doWrite(l, prompt, l->plen);
    return 0;
}

char *linenoiseEditMore =
    "If you see this, you are misusing the API: when linenoiseEditFeed() is called, if it returns linenoiseEditMore "
    "the user is yet editing the line. See the README file for more information.";

/* This function is part of the multiplexed API of linenoise, see the top
 * comment on linenoiseEditStart() for more information. Call this function
 * each time there is some data to read from the standard input file
 * descriptor. In the case of blocking operations, this function can just be
 * called in a loop, and block.
 *
 * The function returns linenoiseEditMore to signal that line editing is still
 * in progress, that is, the user didn't yet pressed enter / CTRL-D. Otherwise
 * the function returns the pointer to the heap-allocated buffer with the
 * edited line, that the user should free with linenoiseFree().
 *
 * On special conditions, NULL is returned and errno is populated:
 *
 * EAGAIN if the user pressed Ctrl-C
 * ENOENT if the user pressed Ctrl-D
 *
 * Some other errno: I/O error.
 */
char *linenoiseEditFeed(struct linenoiseState *l)
{
    char c;
    int nread;
    char seq[3];
    char *rv = NULL;
    
    nread = read(l->ifd, &c, 1);
    if (nread <= 0)
        return NULL;

    abInit(&l->ab);
    
    /* Only autocomplete when the callback is set. It returns < 0 when
     * there was an error reading from fd. Otherwise it will return the
     * character that should be handled next. */
    if ((l->in_completion || c == TAB) && completionCallback != NULL) {
        if(!l->in_completion){
            l->completion_str = strdup(l->buf);
        }
        int ret = completeLine(l, c);
        c = ret;
        /* Return on errors */
        if (ret < 0)
            goto EXIT;
        /* Read next character when 0 */
        if (ret == 0){
            rv = linenoiseEditMore;
            goto EXIT;
        }
    }

    switch (c) {
    case ENTER: /* enter */
        history_len--;
        free(history[history_len]);
        if (hintsCallback) {
            /* Force a refresh without hints to leave the previous
             * line as the user typed it after a newline. */
            linenoiseHintsCallback *hc = hintsCallback;
            hintsCallback = NULL;
            refreshLine(l);
            hintsCallback = hc;
        }
        rv = strdup(l->buf);
        goto EXIT;
    case CTRL_C: /* ctrl-c */
        errno = EAGAIN;
        goto EXIT;
    case BACKSPACE: /* backspace */
    case 8:         /* ctrl-h */
        linenoiseEditBackspace(l);
        break;
    case CTRL_D: /* ctrl-d, remove char at right of cursor, or if the
                    line is empty, act as end-of-file. */
        if (l->len > 0) {
            linenoiseEditDelete(l);
        } else {
            history_len--;
            free(history[history_len]);
            errno = ENOENT;
            goto EXIT;
        }
        break;
    case CTRL_T: /* ctrl-t, swaps current character with previous. */
        if (l->pos > 0 && l->pos < l->len) {
            int aux = l->buf[l->pos - 1];
            l->buf[l->pos - 1] = l->buf[l->pos];
            l->buf[l->pos] = aux;
            if (l->pos != l->len - 1){
                modifyPos(l, l->pos+1);
            }
            refreshLine(l);
        }
        break;
    case CTRL_B: /* ctrl-b */
        linenoiseEditMoveLeft(l);
        break;
    case CTRL_F: /* ctrl-f */
        linenoiseEditMoveRight(l);
        break;
    case CTRL_P: /* ctrl-p */
        linenoiseEditHistoryNext(l, LINENOISE_HISTORY_PREV);
        break;
    case CTRL_N: /* ctrl-n */
        linenoiseEditHistoryNext(l, LINENOISE_HISTORY_NEXT);
        break;
    case ESC: /* escape sequence */
        /* Read the next two bytes representing the escape sequence.
         * Use two calls to handle slow terminals returning the two
         * chars at different times. */
        if (read(l->ifd, seq, 1) == -1)
            break;
        if (read(l->ifd, seq + 1, 1) == -1)
            break;

        /* ESC [ sequences. */
        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                /* Extended escape, read additional byte. */
                if (read(l->ifd, seq + 2, 1) == -1)
                    break;
                if (seq[2] == '~') {
                    switch (seq[1]) {
                    case '3': /* Delete key. */
                        linenoiseEditDelete(l);
                        break;
                    }
                }
            } else {
                switch (seq[1]) {
                case 'A': /* Up */
                    linenoiseEditHistoryNext(l, LINENOISE_HISTORY_PREV);
                    break;
                case 'B': /* Down */
                    linenoiseEditHistoryNext(l, LINENOISE_HISTORY_NEXT);
                    break;
                case 'C': /* Right */
                    linenoiseEditMoveRight(l);
                    break;
                case 'D': /* Left */
                    linenoiseEditMoveLeft(l);
                    break;
                case 'H': /* Home */
                    linenoiseEditMoveHome(l);
                    break;
                case 'F': /* End*/
                    linenoiseEditMoveEnd(l);
                    break;
                }
            }
        }

        /* ESC O sequences. */
        else if (seq[0] == 'O') {
            switch (seq[1]) {
            case 'H': /* Home */
                linenoiseEditMoveHome(l);
                break;
            case 'F': /* End*/
                linenoiseEditMoveEnd(l);
                break;
            }
        }
        break;
    default:
        if (linenoiseEditInsert(l, c))
            goto EXIT;
        break;
    case CTRL_U: /* Ctrl+u, delete the whole line. */
        l->buf[0] = '\0';
        l->len = 0;
        modifyPos(l, 0);
        refreshLine(l);
        break;
    case CTRL_K: /* Ctrl+k, delete from current to end of line. */
        l->buf[l->pos] = '\0';
        l->len = l->pos;
        refreshLine(l);
        break;
    case CTRL_A: /* Ctrl+a, go to the start of the line */
        linenoiseEditMoveHome(l);
        break;
    case CTRL_E: /* ctrl+e, go to the end of the line */
        linenoiseEditMoveEnd(l);
        break;
    case CTRL_R:
        refreshLine(l);
        break;
    case TAB: // ignore
        break;
    }
    rv = linenoiseEditMore;
EXIT:
    flushABuffer(l);
    abFree(&l->ab);
    return rv;
}

/* This is part of the multiplexed linenoise API. See linenoiseEditStart()
 * for more information. This function is called when linenoiseEditFeed()
 * returns something different than NULL. At this point the user input
 * is in the buffer, and we can restore the terminal in normal mode. */
void linenoiseEditStop(struct linenoiseState *l)
{
    clearHint(l);
    if (!isatty(l->ifd))
        return;
    disableRawMode(l->ifd);
    printf("\n");
}

/* This just implements a blocking loop for the multiplexed API.
 * In many applications that are not event-drivern, we can just call
 * the blocking linenoise API, wait for the user to complete the editing
 * and return the buffer. */
static char *linenoiseBlockingEdit(int stdin_fd, int stdout_fd, char *buf, size_t buflen, const char *prompt)
{
    struct linenoiseState l;

    /* Editing without a buffer is invalid. */
    if (buflen == 0) {
        errno = EINVAL;
        return NULL;
    }

    linenoiseEditStart(&l, stdin_fd, stdout_fd, buf, buflen, prompt);
    char *res;
    while ((res = linenoiseEditFeed(&l)) == linenoiseEditMore){
        handleHints(&l);
    }
    linenoiseEditStop(&l);
    return res;
}



/* The high level function that is the main API of the linenoise library.
 * This function checks if the terminal has basic capabilities, just checking
 * for a blacklist of stupid terminals, and later either calls the line
 * editing function or uses dummy fgets() so that you will be able to type
 * something even in the most desperate of the conditions. */
char *linenoise(const char *prompt)
{
    char buf[LINENOISE_MAX_LINE];

    char *retval = linenoiseBlockingEdit(STDIN_FILENO, STDOUT_FILENO, buf, LINENOISE_MAX_LINE, prompt);
    return retval;
}

/* This is just a wrapper the user may want to call in order to make sure
 * the linenoise returned buffer is freed with the same allocator it was
 * created with. Useful when the main program is using an alternative
 * allocator. */
void linenoiseFree(void *ptr)
{
    if (ptr == linenoiseEditMore)
        return;  // Protect from API misuse.
    free(ptr);
}

/* ================================ History ================================= */


/* This is the API call to add a new entry in the linenoise history.
 * It uses a fixed array of char pointers that are shifted (memmoved)
 * when the history max length is reached in order to remove the older
 * entry and make room for the new one, so it is not exactly suitable for huge
 * histories, but will work well for a few hundred of entries.
 *
 * Using a circular buffer is smarter, but a bit more complex to handle. */
int linenoiseHistoryAdd(const char *line)
{
    char *linecopy;

    if (history_max_len == 0)
        return 0;

    /* Initialization on first call. */
    if (history == NULL) {
        history = malloc(sizeof(char *) * history_max_len);
        if (history == NULL)
            return 0;
        memset(history, 0, (sizeof(char *) * history_max_len));
    }

    /* Don't add duplicated lines. */
    if (history_len && !strcmp(history[history_len - 1], line))
        return 0;

    /* Add an heap allocated copy of the line in the history.
     * If we reached the max length, remove the older line. */
    linecopy = strdup(line);
    if (!linecopy)
        return 0;
    if (history_len == history_max_len) {
        free(history[0]);
        memmove(history, history + 1, sizeof(char *) * (history_max_len - 1));
        history_len--;
    }
    history[history_len] = linecopy;
    history_len++;
    return 1;
}

/* Set the maximum length for the history. This function can be called even
 * if there is already some history, the function will make sure to retain
 * just the latest 'len' elements if the new history length value is smaller
 * than the amount of items already inside the history. */
int linenoiseHistorySetMaxLen(int len)
{
    char **new;

    if (len < 1)
        return 0;
    if (history) {
        int tocopy = history_len;

        new = malloc(sizeof(char *) * len);
        if (new == NULL)
            return 0;

        /* If we can't copy everything, free the elements we'll not use. */
        if (len < tocopy) {
            int j;

            for (j = 0; j < tocopy - len; j++)
                free(history[j]);
            tocopy = len;
        }
        memset(new, 0, sizeof(char *) * len);
        memcpy(new, history + (history_len - tocopy), sizeof(char *) * tocopy);
        free(history);
        history = new;
    }
    history_max_len = len;
    if (history_len > history_max_len)
        history_len = history_max_len;
    return 1;
}

/* Save the history in the specified file. On success 0 is returned
 * otherwise -1 is returned. */
int linenoiseHistorySave(const char *filename) {
    FILE *fp;
    int j;

    fp = fopen(filename,"w");
    if (fp == NULL) return -1;
    for (j = 0; j < history_len; j++)
        fprintf(fp,"%s\n",history[j]);
    fclose(fp);
    return 0;
}

/* Load the history from the specified file. If the file does not exist
 * zero is returned and no operation is performed.
 *
 * If the file exists and the operation succeeded 0 is returned, otherwise
 * on error -1 is returned. */
int linenoiseHistoryLoad(const char *filename) {
    FILE *fp = fopen(filename,"r");
    char buf[LINENOISE_MAX_LINE];

    if (fp == NULL) return -1;

    while (fgets(buf,LINENOISE_MAX_LINE,fp) != NULL) {
        char *p;

        p = strchr(buf,'\r');
        if (!p) p = strchr(buf,'\n');
        if (p) *p = '\0';
        linenoiseHistoryAdd(buf);
    }
    fclose(fp);
    return 0;
}