#include "window.h"

#include "../game/control.h"
#include "../game/scene.h"

#include <SDL3/SDL.h>
#include <string.h>

static SDL_Window   *win;
static SDL_Renderer *ren;
static SDL_Texture  *tex;
static int           tex_w = SCENE_W, tex_h = SCENE_H, tex_scale = 1;

int window_open_size(const char *title, int w, int h, int scale)
{
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init: %s", SDL_GetError());
        return 0;
    }
    if (!SDL_CreateWindowAndRenderer(title, w * scale, h * scale,
                                     SDL_WINDOW_RESIZABLE, &win, &ren)) {
        SDL_Log("SDL_CreateWindowAndRenderer: %s", SDL_GetError());
        return 0;
    }
    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_XRGB8888,
                            SDL_TEXTUREACCESS_STREAMING, w, h);
    if (!tex)
        return 0;
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
    tex_w = w;
    tex_h = h;
    tex_scale = scale > 0 ? scale : 1;
    return 1;
}

int window_resize(int w, int h)
{
    if (!ren)
        return 0;
    if (w == tex_w && h == tex_h)
        return 1;
    SDL_Texture *t = SDL_CreateTexture(ren, SDL_PIXELFORMAT_XRGB8888,
                                       SDL_TEXTUREACCESS_STREAMING, w, h);
    if (!t)
        return 0;
    if (tex)
        SDL_DestroyTexture(tex);
    tex = t;
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
    SDL_SetWindowSize(win, w * tex_scale, h * tex_scale);
    tex_w = w;
    tex_h = h;
    return 1;
}

int window_open(const char *title, int scale)
{
    return window_open_size(title, SCENE_W, SCENE_H, scale);
}

void window_close(void)
{
    if (tex) SDL_DestroyTexture(tex);
    if (ren) SDL_DestroyRenderer(ren);
    if (win) SDL_DestroyWindow(win);
    SDL_Quit();
}

void window_present(const uint16_t *fb)
{
    void *pixels;
    int pitch;
    if (SDL_LockTexture(tex, NULL, &pixels, &pitch)) {
        for (int y = 0; y < tex_h; y++) {
            uint32_t *row = (uint32_t *)((uint8_t *)pixels + (size_t)y * pitch);
            for (int x = 0; x < tex_w; x++)
                row[x] = scene_rgb(fb[y * tex_w + x]);
        }
        SDL_UnlockTexture(tex);
    }
    SDL_RenderClear(ren);
    SDL_RenderTexture(ren, tex, NULL, NULL);
    SDL_RenderPresent(ren);
}

uint64_t window_ms(void)
{
    return SDL_GetTicks();
}

void window_sleep(uint32_t ms)
{
    SDL_Delay(ms);
}

void window_poll(Input *in)
{
    in->prev = in->next = in->toggle_depth = in->toggle_spin = in->shot = 0;

    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_EVENT_QUIT)
            in->quit = 1;
        if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
            switch (e.key.key) {
            case SDLK_ESCAPE: case SDLK_Q: in->quit = 1; break;
            case SDLK_LEFTBRACKET:  case SDLK_PAGEDOWN: in->prev = 1; break;
            case SDLK_RIGHTBRACKET: case SDLK_PAGEUP:   in->next = 1; break;
            case SDLK_Z:      in->toggle_depth = 1; break;
            case SDLK_SPACE:  in->toggle_spin = 1;  break;
            case SDLK_F2:     in->shot = 1;         break;
            default: break;
            }
        }
    }

    const bool *keys = SDL_GetKeyboardState(NULL);
    in->dx = keys[SDL_SCANCODE_RIGHT] - keys[SDL_SCANCODE_LEFT];
    in->dz = keys[SDL_SCANCODE_DOWN]  - keys[SDL_SCANCODE_UP];

    /* The same keys again, as the joypad long AICTRL.GAS reads.  The arrows
     * are the pad; A, S and D are the three fire buttons, and holding down
     * with one of them is the guard - which is what the AI presses when it
     * blocks, so the player has the same three moves it does. */
    in->pad = 0;
    if (keys[SDL_SCANCODE_UP])    in->pad |= 1u << JOY_UP;
    if (keys[SDL_SCANCODE_DOWN])  in->pad |= 1u << JOY_DOWN;
    if (keys[SDL_SCANCODE_LEFT])  in->pad |= 1u << JOY_LEFT;
    if (keys[SDL_SCANCODE_RIGHT]) in->pad |= 1u << JOY_RIGHT;
    if (keys[SDL_SCANCODE_A])     in->pad |= 1u << FIRE_A;
    if (keys[SDL_SCANCODE_S])     in->pad |= 1u << FIRE_B;
    if (keys[SDL_SCANCODE_D])     in->pad |= 1u << FIRE_C;
    /* The keypad half, which is COLLECT's: OPTION opens the inventory,
     * `*` and `#` leave it. */
    if (keys[SDL_SCANCODE_TAB])    in->pad |= 1u << PAD_OPTION;
    if (keys[SDL_SCANCODE_ESCAPE]) in->pad |= 1u << PAD_STAR;
}
