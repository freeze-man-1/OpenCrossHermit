// Link-time SDL2 stand-ins for headless tests: no window, no GPU, real file I/O.
// Only the functions the engine and game code call are provided.
#include <SDL.h>
#include "soft_render.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
static int dummy;
int SDL_Init(Uint32) { return 0; }
void SDL_Quit() {}
const char* SDL_GetError() { return "fake"; }
SDL_bool SDL_SetHint(const char*, const char*) { return SDL_TRUE; }
SDL_Window* SDL_CreateWindow(const char*, int, int, int, int, Uint32) { return reinterpret_cast<SDL_Window*>(&dummy); }
int SDL_SetWindowFullscreen(SDL_Window*, Uint32) { return 0; }
Uint32 SDL_GetWindowFlags(SDL_Window*) { return 0; }
void SDL_GetWindowSize(SDL_Window*, int* w, int* h) { *w = 1024; *h = 768; }
void SDL_SetWindowSize(SDL_Window*, int, int) {}
void SDL_SetWindowPosition(SDL_Window*, int, int) {}
int SDL_GetWindowDisplayIndex(SDL_Window*) { return 0; }
int SDL_GetDisplayBounds(int, SDL_Rect* r) { *r = {0, 0, 1024, 768}; return 0; }
void SDL_DestroyWindow(SDL_Window*) {}
SDL_Renderer* SDL_CreateRenderer(SDL_Window*, int, Uint32) { return reinterpret_cast<SDL_Renderer*>(&dummy); }
void SDL_DestroyRenderer(SDL_Renderer*) {}
void SDL_DestroyTexture(SDL_Texture*) {}
int SDL_GetRendererOutputSize(SDL_Renderer*, int* w, int* h) { *w = 1024; *h = 768; return 0; }
void SDL_WarpMouseInWindow(SDL_Window*, int, int) {}
int SDL_SetRenderTarget(SDL_Renderer*, SDL_Texture*) { return 0; }
int SDL_SetTextureScaleMode(SDL_Texture*, SDL_ScaleMode) { return 0; }
int SDL_RenderCopy(SDL_Renderer*, SDL_Texture*, const SDL_Rect*, const SDL_Rect*) { return 0; }   // the frame into the window
Uint64 SDL_GetPerformanceFrequency() { return 1000; }
Uint64 SDL_GetPerformanceCounter() { return 0; }
Uint32 SDL_GetTicks() { return 0; }
int SDL_PollEvent(SDL_Event*) { return 0; }
static Uint32 g_color = 0;
int SDL_SetRenderDrawColor(SDL_Renderer*, Uint8 r, Uint8 g, Uint8 b, Uint8 a) { g_color = Uint32(a) << 24 | Uint32(r) << 16 | Uint32(g) << 8 | b; return 0; }
int SDL_RenderClear(SDL_Renderer*) { return 0; }
void SDL_RenderPresent(SDL_Renderer*) {}
int SDL_RenderFillRect(SDL_Renderer*, const SDL_Rect* r) { if (soft::enabled && r) soft::fillRect(r->x, r->y, r->w, r->h, g_color); return 0; }
int SDL_RenderGeometry(SDL_Renderer*, SDL_Texture*, const SDL_Vertex*, int, const int*, int) { return 0; }
int SDL_RenderDrawRect(SDL_Renderer*, const SDL_Rect*) { return 0; }
int SDL_SetRenderDrawBlendMode(SDL_Renderer*, SDL_BlendMode) { return 0; }
char* SDL_GetBasePath() {
    const char* p = std::getenv("CH_TEST_BASE");      // stands in for the binary's folder
    char* s = static_cast<char*>(std::malloc(std::strlen(p ? p : "./") + 2));
    std::strcpy(s, p ? p : "./");
    if (s[std::strlen(s) - 1] != '/') std::strcat(s, "/");
    return s;
}
char* SDL_GetPrefPath(const char*, const char*) { return SDL_GetBasePath(); }
void SDL_free(void* p) { std::free(p); }
SDL_RWops* SDL_RWFromFile(const char* f, const char* m) { return reinterpret_cast<SDL_RWops*>(std::fopen(f, m)); }
int SDL_RWclose(SDL_RWops* rw) { return std::fclose(reinterpret_cast<FILE*>(rw)); }
Sint64 SDL_RWsize(SDL_RWops* rw) {
    FILE* f = reinterpret_cast<FILE*>(rw);
    long cur = std::ftell(f); std::fseek(f, 0, SEEK_END); long n = std::ftell(f); std::fseek(f, cur, SEEK_SET);
    return n;
}
size_t SDL_RWread(SDL_RWops* rw, void* p, size_t s, size_t n) { return std::fread(p, s, n, reinterpret_cast<FILE*>(rw)); }
size_t SDL_RWwrite(SDL_RWops* rw, const void* p, size_t s, size_t n) { return std::fwrite(p, s, n, reinterpret_cast<FILE*>(rw)); }
int SDL_ShowSimpleMessageBox(Uint32, const char*, const char* m, SDL_Window*) { std::fprintf(stderr, "%s\n", m); return 0; }
int SDL_ShowCursor(int) { return 0; }
char* SDL_getenv(const char* n) { return std::getenv(n); }
SDL_Surface* SDL_CreateRGBSurfaceWithFormat(Uint32, int, int, int, Uint32) { return nullptr; }
int SDL_RenderReadPixels(SDL_Renderer*, const SDL_Rect*, Uint32, void*, int) { return -1; }
int SDL_SaveBMP_RW(SDL_Surface*, SDL_RWops*, int) { return -1; }
void SDL_StartTextInput(void) {}
SDL_Keymod SDL_GetModState(void) { return KMOD_NONE; }
const Uint8* SDL_GetKeyboardState(int* n) { static Uint8 keys[SDL_NUM_SCANCODES]; if (n) *n = SDL_NUM_SCANCODES; return keys; }
void SDL_SetModState(SDL_Keymod) {}
void SDL_StopTextInput(void) {}
void SDL_FreeSurface(SDL_Surface*) {}
SDL_Texture* SDL_CreateTexture(SDL_Renderer*, Uint32, int, int, int) { return reinterpret_cast<SDL_Texture*>(&dummy); }
int SDL_UpdateTexture(SDL_Texture*, const SDL_Rect*, const void*, int) { return 0; }
int SDL_SetTextureBlendMode(SDL_Texture*, SDL_BlendMode) { return 0; }
int SDL_RenderCopyF(SDL_Renderer*, SDL_Texture*, const SDL_Rect*, const SDL_FRect*) { return 0; }   // a piece of the split view
Uint32 SDL_GetMouseState(int* x, int* y) { if (x) *x = 0; if (y) *y = 0; return 0; }
SDL_BlendMode SDL_ComposeCustomBlendMode(SDL_BlendFactor, SDL_BlendFactor, SDL_BlendOperation, SDL_BlendFactor, SDL_BlendFactor,
                                         SDL_BlendOperation) { return SDL_BLENDMODE_BLEND; }
void SDL_Log(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); std::vfprintf(stderr, fmt, ap); va_end(ap); std::fputc('\n', stderr);
}
}
