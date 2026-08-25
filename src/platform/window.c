#include "window.h"
#include "../game/scene.h"

#include <SDL3/SDL.h>
#include <string.h>

static SDL_Window   *win;
static SDL_Renderer *ren;
static SDL_Texture  *tex;

int window_open(const char *title, int scale)
{
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init: %s", SDL_GetError());
        return 0;
    }
    if (!SDL_CreateWindowAndRenderer(title, SCENE_W * scale, SCENE_H * scale,
                                     SDL_WINDOW_RESIZABLE, &win, &ren)) {
        SDL_Log("SDL_CreateWindowAndRenderer: %s", SDL_GetError());
        return 0;
    }
    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_XRGB8888,
                            SDL_TEXTUREACCESS_STREAMING, SCENE_W, SCENE_H);
    if (!tex)
        return 0;
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
    return 1;
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
        for (int y = 0; y < SCENE_H; y++) {
            uint32_t *row = (uint32_t *)((uint8_t *)pixels + (size_t)y * pitch);
            for (int x = 0; x < SCENE_W; x++)
                row[x] = scene_rgb(fb[y * SCENE_W + x]);
        }
        SDL_UnlockTexture(tex);
    }
    SDL_RenderClear(ren);
    SDL_RenderTexture(ren, tex, NULL, NULL);
    SDL_RenderPresent(ren);
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
            case SDLK_S:      in->shot = 1;         break;
            default: break;
            }
        }
    }

    const bool *keys = SDL_GetKeyboardState(NULL);
    in->dx = keys[SDL_SCANCODE_RIGHT] - keys[SDL_SCANCODE_LEFT];
    in->dz = keys[SDL_SCANCODE_DOWN]  - keys[SDL_SCANCODE_UP];
}
