#ifndef XBR_H
#define XBR_H

#include "../types.h"

// xBR 2x pixel art upscaler (Hyllian's algorithm, lv2 rules). dst must have
// room for (w*2) * (h*2) pixels. Alpha is taken into account, so transparent
// (color keyed) areas keep clean edges.
void xbr2x(const rgba_t *src, int w, int h, rgba_t *dst);

// Plain 2x pixel duplication, for things that must stay pixelated or that
// change every frame (the intro video)
void nearest2x(const rgba_t *src, int w, int h, rgba_t *dst);

#endif
