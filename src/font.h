/* ARCLIGHT - 3x5 bitmap font baked into a texture at boot.
 * Enough for HUD numbers and in-world hint text; the shipping game will want
 * something with lowercase, but not yet. */
#ifndef ARC_FONT_H
#define ARC_FONT_H

#include "gfx.h"

int  font_init(void);
void font_shutdown(void);

/* Must be called between gfx_batch_begin(sprite shader, font_texture()) and
 * gfx_batch_end(). Returns the width drawn, in pixels. */
int  font_text(float x, float y, int scale, uint32_t color, const char *s);
int  font_width(int scale, const char *s);

const arc_texture *font_texture(void);

#endif
