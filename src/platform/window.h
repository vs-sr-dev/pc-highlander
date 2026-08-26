/* window - the SDL3 side: one 320x200 framebuffer, scaled up, and a keyboard.
 *
 * The engine never talks to SDL anywhere else.  A viewer run with --no-window
 * does not open this at all, which is what lets it render a frame and exit.
 */
#ifndef HL_WINDOW_H
#define HL_WINDOW_H

#include <stdint.h>

typedef struct {
    int quit;
    int prev, next;         /* pressed this frame: previous / next scene   */
    int toggle_depth;
    int toggle_spin;
    int shot;
    int dx, dz;             /* held: nudge the object about the floor      */
    uint32_t pad;           /* held, as the game's joypad long: the bits of
                               control.h, straight off the keyboard         */
} Input;

int  window_open(const char *title, int scale);

/* The same, for a picture that is not the game's own 320x200: the films are
 * 320x240, and they are shown at their own size rather than squeezed. */
int  window_open_size(const char *title, int w, int h, int scale);

/* Changes the shape of the open window: the game draws 320x200 and a film is
 * 320x240, and the original changed video mode between them too. */
int  window_resize(int w, int h);
void window_close(void);

/* Uploads an RGB16 (R5 B5 G6) framebuffer of the size the window was opened
 * for, and shows it. */
void window_present(const uint16_t *fb);
void window_poll(Input *in);

/* Milliseconds since the window opened, and a sleep - for anything that has
 * to keep a rate of its own, which so far is the films at 12 fps. */
uint64_t window_ms(void);
void     window_sleep(uint32_t ms);

#endif
