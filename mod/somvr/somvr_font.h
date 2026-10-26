/* somvr_font.h - a small embedded bitmap font for the VR settings menu (somvr_vrmenu.h; docs/re-xr-pipeline.md 15): 8x16
 * cells in the style of the PC's VGA text font (2-pixel stems, drawn here from scratch as ASCII art, public domain),
 * printable ASCII plus three menu glyphs. The layer rasterises the panel with it into an RGBA staging buffer. Pure: no
 * globals (the caller keeps one somvr_font), no OpenXR/Vulkan. Tested by tests/test_vrmenu.c.
 *
 * Cell metrics (rows 0..15 top to bottom, columns 0..7 left to right): capitals and digits rows 2..11 (baseline row 11),
 * lower case x-height from row 5, descenders down to row 14; column 7 and rows 0 and 15 stay empty (the spacing), so
 * glyphs never touch at any scale. */
#ifndef SOMVR_FONT_H_INCLUDED
#define SOMVR_FONT_H_INCLUDED

#include <stddef.h>
#include <stdint.h>

#define SOMVR_FONT_W 8
#define SOMVR_FONT_H 16
#define SOMVR_FONT_FIRST 32   /* ' ' */
#define SOMVR_FONT_LAST 0x81  /* the last glyph */
#define SOMVR_FONT_N (SOMVR_FONT_LAST - SOMVR_FONT_FIRST + 1)
#define SOMVR_FONT_BASELINE 11
enum { SOMVR_FONT_DEG = 0x7f, SOMVR_FONT_LEFT = 0x80, SOMVR_FONT_RIGHT = 0x81 }; /* degree sign, solid triangles */

typedef struct {
    uint8_t row[SOMVR_FONT_N][SOMVR_FONT_H]; /* bit 7 = column 0 */
    int ok;                                  /* every glyph's art parsed */
} somvr_font;

/* fills f from the art; 0 = ok, else the first glyph code whose art is malformed (f->ok = 0, that glyph blank) */
int somvr_font_init(somvr_font *f);
/* code -> its glyph index, or the '?' glyph for a code outside the font */
int somvr_font_index(int ch);
/* row y (0..15) of glyph ch, bit 7 = column 0 (0 outside the cell) */
uint8_t somvr_font_row(const somvr_font *f, int ch, int y);
/* the ink's box of glyph ch in the cell: {x0, y0, x1, y1} inclusive; 0 = no ink (the space) */
int somvr_font_ink(const somvr_font *f, int ch, int box[4]);
/* the width of s at an integer scale (every glyph advances SOMVR_FONT_W x scale pixels) */
int somvr_font_text_w(const char *s, int scale);
/* Draws s into an RGBA8 image (w x h, rows top to bottom, 4 bytes per pixel; bgra: B and R swapped) at x, y (the
 * cell's top left) at an integer scale (1..8), in colour rgba (straight; stored premultiplied, as the panel's
 * BLEND_TEXTURE_SOURCE_ALPHA quad wants). Clipped to the image. Returns the pen's x after the text. */
int somvr_font_draw(const somvr_font *f, uint8_t *px, int w, int h, int x, int y, const char *s, int scale,
                    const uint8_t rgba[4], int bgra);

#endif
