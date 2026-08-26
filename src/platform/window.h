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
void window_close(void);

/* Uploads a 320x200 RGB16 (R5 B5 G6) framebuffer and shows it. */
void window_present(const uint16_t *fb);
void window_poll(Input *in);

#endif
