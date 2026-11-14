// studio.c — Helio 213 Font & Palette Editor
// Build: gcc -O2 -o studio studio.c $(sdl2-config --cflags --libs)
// Usage: ./studio

#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>

#define WIN_W 800
#define WIN_H 600
#define CELL_SIZE 32
#define GRID_SIZE (8 * CELL_SIZE) // 256

// Data Structures
static uint8_t font[256][8][8]; // [char][y][x] -> color index 0-15
static uint8_t palettes[4][16]; // [palette_id][color_idx] -> RRRGGGBB

static int current_char = 0;
static int current_color = 15; // White by default
static int current_pal = 0;    // Palette being edited

/* ---- Helpers ---- */
static uint32_t rgb_to_argb(uint8_t color) {
    int r3 = (color >> 5) & 0x7;
    int g3 = (color >> 2) & 0x7;
    int b2 = color & 0x3;
    int r8 = r3 * 255 / 7;
    int g8 = g3 * 255 / 7;
    int b8 = b2 * 255 / 3;
    return 0xFF000000u | (r8 << 16) | (g8 << 8) | b8;
}

static void draw_rect(SDL_Renderer *ren, int x, int y, int w, int h, uint32_t color) {
    SDL_SetRenderDrawColor(ren, (color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF, (color >> 24) & 0xFF);
    SDL_Rect r = {x, y, w, h};
    SDL_RenderFillRect(ren, &r);
}

// Render a string using the currently loaded font + palette
static void draw_text(SDL_Renderer *ren, const char *text, int x, int y, int scale) {
    int cx = x;
    for (const char *p = text; *p; p++) {
        int code = (unsigned char)*p;
        if (code > 127) code = '?';
        for (int py = 0; py < 8; py++) {
            for (int px = 0; px < 8; px++) {
                uint8_t cidx = font[code][py][px];
                uint32_t col = rgb_to_argb(palettes[current_pal][cidx]);
                draw_rect(ren, cx + px * scale, y + py * scale, scale, scale, col);
            }
        }
        cx += 8 * scale;
    }
}

static void save_files(void) {
    // Ensure output directory exists (ignore EEXIST)
    mkdir("memory", 0755);

    // 1. Pack Font (4-bit pixels, high nibble = left, low nibble = right)
    uint8_t packed_font[4096]; // 128 chars * 32 bytes
    memset(packed_font, 0, sizeof(packed_font));
    for (int c = 0; c < 128; c++) {
        for (int y = 0; y < 8; y++) {
            for (int x = 0; x < 8; x += 2) {
                uint8_t left = font[c][y][x] & 0xF;
                uint8_t right = font[c][y][x+1] & 0xF;
                packed_font[c * 32 + y * 4 + (x / 2)] = (left << 4) | right;
            }
        }
    }
    FILE *f = fopen("memory/font.bin", "wb");
    if (f) { fwrite(packed_font, 1, 4096, f); fclose(f); }

    // 2. Save Palettes
    f = fopen("memory/palette.bin", "wb");
    if (f) { fwrite(palettes, 1, sizeof(palettes), f); fclose(f); }
    
    printf("Saved memory/font.bin and memory/palette.bin\n");
}

static int load_files(void) {
    int loaded = 0; // bit 0 = font, bit 1 = palettes

    // 1. Load Font
    FILE *f = fopen("memory/font.bin", "rb");
    if (f) {
        uint8_t packed_font[4096];
        size_t n = fread(packed_font, 1, sizeof(packed_font), f);
        fclose(f);
        if (n == sizeof(packed_font)) {
            for (int c = 0; c < 128; c++) {
                for (int y = 0; y < 8; y++) {
                    for (int x = 0; x < 4; x++) {
                        uint8_t b = packed_font[c * 32 + y * 4 + x];
                        font[c][y][x * 2]     = (b >> 4) & 0xF;
                        font[c][y][x * 2 + 1] =  b       & 0xF;
                    }
                }
            }
            loaded |= 1;
        } else {
            fprintf(stderr, "Warning: memory/font.bin has size %zu (expected 4096)\n", n);
        }
    } else {
        fprintf(stderr, "Warning: could not open memory/font.bin for reading\n");
    }

    // 2. Load Palettes
    f = fopen("memory/palette.bin", "rb");
    if (f) {
        size_t n = fread(palettes, 1, sizeof(palettes), f);
        fclose(f);
        if (n == sizeof(palettes)) {
            loaded |= 2;
        } else {
            fprintf(stderr, "Warning: memory/palette.bin has size %zu (expected %zu)\n",
                    n, sizeof(palettes));
        }
    } else {
        fprintf(stderr, "Warning: could not open memory/palette.bin for reading\n");
    }

    if (loaded) {
        printf("Loaded%s%s\n",
               (loaded & 1) ? " memory/font.bin" : "",
               (loaded & 2) ? " memory/palette.bin" : "");
    }
    return loaded;
}

/* ---- Main ---- */
int main(int argc, char **argv) {
    (void)argc; (void)argv;
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window *win = SDL_CreateWindow("Helio Studio", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WIN_W, WIN_H, SDL_WINDOW_SHOWN);
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    // Initialize empty font and default palettes
    memset(font, 0, sizeof(font));
    
    // Palette 0: Greyscale
    for (int i = 0; i < 16; i++) { int g = i * 17; palettes[0][i] = ((g >> 5) & 7) << 5 | ((g >> 5) & 7) << 2 | ((g >> 6) & 3); }
    // Palette 1: Rainbow
    uint8_t rainbow[16] = {0x00, 0x03, 0x1C, 0x1F, 0xE0, 0xE3, 0xFC, 0xFF, 0x92, 0xB5, 0xD6, 0x6C, 0x49, 0x2A, 0x24, 0x6D};
    memcpy(palettes[1], rainbow, 16);
    // Palette 2: Warm
    for (int i = 0; i < 16; i++) { palettes[2][i] = ((i >> 1) & 7) << 5 | ((i * 2) & 7) << 2 | ((i >> 2) & 3); }
    // Palette 3: Cool
    for (int i = 0; i < 16; i++) { palettes[3][i] = ((i >> 2) & 7) << 5 | ((i >> 1) & 7) << 2 | (i & 3); }

    // Try to restore previously saved work on startup.
    load_files();

    // Only seed if the caller didn't load anything later... easier: do it
    // *after* load_files() and only if font was never loaded.
    if (!(load_files() & 1)) {
        // Fill 48..57 with a simple 5x7 digit pattern (values 0-15 map through palette)
        static const uint8_t digits[10][8] = { /* ... */ };
        for (int d = 0; d < 10; d++)
            for (int y = 0; y < 8; y++)
                for (int x = 0; x < 8; x++)
                    font['0'+d][y][x] = (digits[d][y] >> (7 - x)) & 1 ? 15 : 0;
    }

    int running = 1;
    int drawing = 0;
    int draw_color = 0;

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_ESCAPE) running = 0;
                if (e.key.keysym.sym == SDLK_s) save_files();
                if (e.key.keysym.sym == SDLK_l) load_files();
                if (e.key.keysym.sym == SDLK_RIGHT) current_char = (current_char + 1) % 128;
                if (e.key.keysym.sym == SDLK_LEFT) current_char = (current_char - 1 + 128) % 128;
                if (e.key.keysym.sym >= SDLK_1 && e.key.keysym.sym <= SDLK_4) current_pal = e.key.keysym.sym - SDLK_1;
            }
            if (e.type == SDL_MOUSEBUTTONDOWN) {
                if (e.button.button == SDL_BUTTON_LEFT) {
                    drawing = 1;
                    int mx = e.button.x;
                    int my = e.button.y;

                    // Check if clicking the pixel grid (x: 40-296, y: 40-296)
                    if (mx >= 40 && mx < 40 + GRID_SIZE && my >= 40 && my < 40 + GRID_SIZE) {
                        int px = (mx - 40) / CELL_SIZE;
                        int py = (my - 40) / CELL_SIZE;
                        font[current_char][py][px] = current_color;
                        draw_color = current_color;
                    }
                    // Check if clicking a palette color (y: 350-382)
                    else if (my >= 350 && my < 350 + CELL_SIZE) {
                        for (int i = 0; i < 16; i++) {
                            int px = 40 + i * CELL_SIZE;
                            if (mx >= px && mx < px + CELL_SIZE) {
                                current_color = i;
                            }
                        }
                    }
                }
            }
            if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                drawing = 0;
            }
            if (e.type == SDL_MOUSEMOTION && drawing) {
                int mx = e.motion.x;
                int my = e.motion.y;
                if (mx >= 40 && mx < 40 + GRID_SIZE && my >= 40 && my < 40 + GRID_SIZE) {
                    int px = (mx - 40) / CELL_SIZE;
                    int py = (my - 40) / CELL_SIZE;
                    font[current_char][py][px] = draw_color;
                }
            }
        }

        // --- Render ---
        SDL_SetRenderDrawColor(ren, 30, 30, 30, 255); // Dark grey background
        SDL_RenderClear(ren);

        // Draw Editor Grid
        for (int y = 0; y < 8; y++) {
            for (int x = 0; x < 8; x++) {
                uint8_t cidx = font[current_char][y][x];
                uint32_t col = rgb_to_argb(palettes[current_pal][cidx]);
                draw_rect(ren, 40 + x * CELL_SIZE, 40 + y * CELL_SIZE, CELL_SIZE - 1, CELL_SIZE - 1, col);
            }
        }

        // Draw Current Selection Border on Grid
        SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
        SDL_Rect grid_outline = {40, 40, GRID_SIZE, GRID_SIZE};
        SDL_RenderDrawRect(ren, &grid_outline);

        // Draw Palette
        for (int i = 0; i < 16; i++) {
            uint32_t col = rgb_to_argb(palettes[current_pal][i]);
            draw_rect(ren, 40 + i * CELL_SIZE, 350, CELL_SIZE - 1, CELL_SIZE - 1, col);
            if (i == current_color) {
                SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
                SDL_Rect sel = {40 + i * CELL_SIZE, 350, CELL_SIZE, CELL_SIZE};
                SDL_RenderDrawRect(ren, &sel);
            }
        }

        // Draw Preview (Scaled up 4x, 8x8 -> 32x32 cells -> 256x256 screen)
        for (int y = 0; y < 8; y++) {
            for (int x = 0; x < 8; x++) {
                uint8_t cidx = font[current_char][y][x];
                uint32_t col = rgb_to_argb(palettes[current_pal][cidx]);
                draw_rect(ren, 400 + x * CELL_SIZE, 40 + y * CELL_SIZE, CELL_SIZE - 1, CELL_SIZE - 1, col);
            }
        }

        // UI Overlay text rendering (using simple primitive rectangles as 'pixels' to simulate text)
        // Just simple colored blocks for visual feedback
        // "Current Char: X" represented by a progress bar
        // draw_rect(ren, 40, 550, (current_char + 1) * 4, 20, 0xFFFFFFFF); 
        
        // Character number, rendered using the loaded font itself
        char charnum[16];
        snprintf(charnum, sizeof(charnum), "%d", current_char);
        draw_text(ren, charnum, 40, 545, 2);

        // "Current Palette: P" represented by 4 squares
        for (int i = 0; i < 4; i++) {
            uint32_t col = (i == current_pal) ? 0xFFFFFFFF : 0xFF444444;
            draw_rect(ren, 40 + i * 40, 500, 30, 30, col);
        }

        SDL_RenderPresent(ren);
    }

    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}