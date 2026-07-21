#include "font.h"
#include <string.h>

/* 3x5 glyphs, 0-9 then A-Z then punctuation. bit2 = left column, bit0 = right
 * column. Carried over from Skyrift, which is the only thing in this project
 * that is - the punctuation was added here, because story text without a full
 * stop reads as a bug rather than as a style. */
static const uint8_t FONT3X5[42][5] = {
    {7,5,5,5,7},{2,6,2,2,7},{7,1,7,4,7},{7,1,7,1,7},{5,5,7,1,1},
    {7,4,7,1,7},{7,4,7,5,7},{7,1,1,2,2},{7,5,7,5,7},{7,5,7,1,7},
    {2,5,7,5,5},{6,5,6,5,6},{3,4,4,4,3},{6,5,5,5,6},{7,4,7,4,7},
    {7,4,7,4,4},{3,4,5,5,3},{5,5,7,5,5},{7,2,2,2,7},{1,1,1,5,2},
    {5,6,4,6,5},{4,4,4,4,7},{5,7,7,5,5},{6,5,5,5,5},{7,5,5,5,7},
    {7,5,7,4,4},{7,5,5,7,1},{7,5,6,5,5},{3,4,2,1,6},{7,2,2,2,2},
    {5,5,5,5,7},{5,5,5,5,2},{5,5,7,7,5},{5,5,2,5,5},{5,5,2,2,2},
    {7,1,2,4,7},
    /* . , - : ' ! */
    {0,0,0,0,2},{0,0,0,2,4},{0,0,7,0,0},{0,2,0,2,0},{2,2,0,0,0},{2,2,2,0,2},
};

/* One 4x6 cell per glyph, 16 per row: a 64x24 texture. */
#define CELL_W 4
#define CELL_H 6
#define COLS   16
#define TEX_W  (CELL_W * COLS)
#define TEX_H  (CELL_H * 3)

static arc_texture tex;

static int glyph_index(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;
    switch (c) {
        case '.':  return 36;
        case ',':  return 37;
        case '-':  return 38;
        case ':':  return 39;
        case '\'': return 40;
        case '!':  return 41;
        default:   return -1;
    }
}

int font_init(void)
{
    static uint8_t px[TEX_W * TEX_H * 4];
    memset(px, 0, sizeof px);

    for (int g = 0; g < 42; g++) {
        int cx = (g % COLS) * CELL_W, cy = (g / COLS) * CELL_H;
        for (int row = 0; row < 5; row++) {
            uint8_t bits = FONT3X5[g][row];
            for (int col = 0; col < 3; col++) {
                if (!(bits & (4 >> col))) continue;
                uint8_t *p = &px[((cy + row) * TEX_W + cx + col) * 4];
                p[0] = p[1] = p[2] = p[3] = 255;
            }
        }
    }
    return gfx_texture_from_rgba(&tex, px, TEX_W, TEX_H, 0);
}

void font_shutdown(void) { gfx_texture_destroy(&tex); }

const arc_texture *font_texture(void) { return &tex; }

int font_width(int scale, const char *s)
{
    return (int)strlen(s) * 4 * scale;
}

int font_text(float x, float y, int scale, uint32_t color, const char *s)
{
    float pen = x;
    for (const char *c = s; *c; c++, pen += 4.0f * scale) {
        int g = glyph_index(*c);
        if (g < 0) continue;                    /* space and punctuation */

        float u0 = (g % COLS) * CELL_W / (float)TEX_W;
        float v0 = (g / COLS) * CELL_H / (float)TEX_H;
        gfx_batch_quad(pen, y, 3.0f * scale, 5.0f * scale,
                       u0, v0,
                       u0 + 3.0f / TEX_W, v0 + 5.0f / TEX_H,
                       color);
    }
    return (int)(pen - x);
}
