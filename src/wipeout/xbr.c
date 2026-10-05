// xBR 2x, after Hyllian's xBR algorithm; the rule structure follows the
// well known CPU implementations (e.g. FFmpeg's vf_xbr). Works on RGBA.

#include <stdlib.h>
#include "xbr.h"

// Weighted YUV + alpha distance between two pixels
static inline int xbr_df(rgba_t a, rgba_t b) {
	int dr = (int)a.r - b.r;
	int dg = (int)a.g - b.g;
	int db = (int)a.b - b.b;
	int da = (int)a.a - b.a;
	int y = abs(dr * 299 + dg * 587 + db * 114) / 1000;
	int u = abs(dr * -169 + dg * -331 + db * 500) / 1000;
	int v = abs(dr * 500 + dg * -419 + db * -81) / 1000;
	return y * 48 + u * 7 + v * 6 + abs(da) * 48;
}

static inline bool xbr_eq(rgba_t a, rgba_t b) {
	return xbr_df(a, b) < 155;
}

static inline bool xbr_same(rgba_t a, rgba_t b) {
	return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

// dst = dst * (1 - w) + src * w, w in 1/256
static inline rgba_t xbr_blend(rgba_t dst, rgba_t src, int w) {
	// Don't drag fully transparent neighbors into opaque pixels (and vice
	// versa): that would smear the color key
	if ((dst.a == 0) != (src.a == 0)) {
		return dst;
	}
	rgba_t r;
	r.r = (dst.r * (256 - w) + src.r * w) >> 8;
	r.g = (dst.g * (256 - w) + src.g * w) >> 8;
	r.b = (dst.b * (256 - w) + src.b * w) >> 8;
	r.a = (dst.a * (256 - w) + src.a * w) >> 8;
	return r;
}

// One corner of the 2x2 output block. The neighborhood is passed in rotated
// so that the corner being computed is always the bottom-right one (N3):
//
//     A1 B1 C1
//  A0 A  B  C  C4
//  D0 D  E  F  F4
//  G0 G  H  I  I4
//     G5 H5 I5
#define FILT2(PE, PI, PH, PF, PG, PC, PD, PB, PA, G5, C4, G0, D0, C1, B1, F4, I4, H5, I5, A1, A0, N0, N1, N2, N3) do { \
	int e = xbr_df(PE, PC) + xbr_df(PE, PG) + xbr_df(PI, H5) + xbr_df(PI, F4) + (xbr_df(PH, PF) << 2); \
	int i = xbr_df(PH, PD) + xbr_df(PH, I5) + xbr_df(PF, I4) + xbr_df(PF, PB) + (xbr_df(PE, PI) << 2); \
	if (e <= i) { \
		rgba_t px = xbr_df(PE, PF) <= xbr_df(PE, PH) ? PF : PH; \
		if (e < i && ( \
			(!xbr_eq(PF, PB) && !xbr_eq(PH, PD)) || \
			(xbr_eq(PE, PI) && (!xbr_eq(PF, I4) && !xbr_eq(PH, I5))) || \
			xbr_eq(PE, PG) || xbr_eq(PE, PC) \
		)) { \
			int ke = xbr_df(PF, PG); \
			int ki = xbr_df(PH, PC); \
			bool left = (ke << 1) <= ki && !xbr_same(PE, PG) && !xbr_same(PD, PG); \
			bool up   = ke >= (ki << 1) && !xbr_same(PE, PC) && !xbr_same(PB, PC); \
			if (left && up) { \
				out[N3] = xbr_blend(out[N3], px, 224); \
				out[N2] = xbr_blend(out[N2], px, 64); \
				out[N1] = out[N2]; \
			} \
			else if (left) { \
				out[N3] = xbr_blend(out[N3], px, 192); \
				out[N2] = xbr_blend(out[N2], px, 64); \
			} \
			else if (up) { \
				out[N3] = xbr_blend(out[N3], px, 192); \
				out[N1] = xbr_blend(out[N1], px, 64); \
			} \
			else { \
				out[N3] = xbr_blend(out[N3], px, 128); \
			} \
		} \
		else { \
			out[N3] = xbr_blend(out[N3], px, 128); \
		} \
	} \
} while (0)

static inline rgba_t xbr_px(const rgba_t *src, int w, int h, int x, int y) {
	x = x < 0 ? 0 : (x >= w ? w - 1 : x);
	y = y < 0 ? 0 : (y >= h ? h - 1 : y);
	return src[y * w + x];
}

void xbr2x(const rgba_t *src, int w, int h, rgba_t *dst) {
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			rgba_t A1 = xbr_px(src, w, h, x - 1, y - 2);
			rgba_t B1 = xbr_px(src, w, h, x,     y - 2);
			rgba_t C1 = xbr_px(src, w, h, x + 1, y - 2);
			rgba_t A0 = xbr_px(src, w, h, x - 2, y - 1);
			rgba_t PA = xbr_px(src, w, h, x - 1, y - 1);
			rgba_t PB = xbr_px(src, w, h, x,     y - 1);
			rgba_t PC = xbr_px(src, w, h, x + 1, y - 1);
			rgba_t C4 = xbr_px(src, w, h, x + 2, y - 1);
			rgba_t D0 = xbr_px(src, w, h, x - 2, y);
			rgba_t PD = xbr_px(src, w, h, x - 1, y);
			rgba_t PE = xbr_px(src, w, h, x,     y);
			rgba_t PF = xbr_px(src, w, h, x + 1, y);
			rgba_t F4 = xbr_px(src, w, h, x + 2, y);
			rgba_t G0 = xbr_px(src, w, h, x - 2, y + 1);
			rgba_t PG = xbr_px(src, w, h, x - 1, y + 1);
			rgba_t PH = xbr_px(src, w, h, x,     y + 1);
			rgba_t PI = xbr_px(src, w, h, x + 1, y + 1);
			rgba_t I4 = xbr_px(src, w, h, x + 2, y + 1);
			rgba_t G5 = xbr_px(src, w, h, x - 1, y + 2);
			rgba_t H5 = xbr_px(src, w, h, x,     y + 2);
			rgba_t I5 = xbr_px(src, w, h, x + 1, y + 2);

			rgba_t out[4] = {PE, PE, PE, PE}; // top-left, top-right, bottom-left, bottom-right

			FILT2(PE, PI, PH, PF, PG, PC, PD, PB, PA, G5, C4, G0, D0, C1, B1, F4, I4, H5, I5, A1, A0, 0, 1, 2, 3);
			FILT2(PE, PC, PF, PB, PI, PA, PH, PD, PG, I4, A1, I5, H5, A0, D0, B1, C1, F4, C4, G0, G5, 2, 0, 3, 1);
			FILT2(PE, PA, PB, PD, PC, PG, PF, PH, PI, C1, G0, C4, F4, G5, H5, D0, A0, B1, A1, I5, I4, 3, 2, 1, 0);
			FILT2(PE, PG, PD, PH, PA, PI, PB, PF, PC, A0, I5, A1, B1, I4, F4, H5, G5, D0, G0, C4, C1, 1, 3, 0, 2);

			rgba_t *d = dst + (y * 2) * (w * 2) + x * 2;
			d[0] = out[0];
			d[1] = out[1];
			d[w * 2] = out[2];
			d[w * 2 + 1] = out[3];
		}
	}
}

void nearest2x(const rgba_t *src, int w, int h, rgba_t *dst) {
	for (int y = 0; y < h; y++) {
		rgba_t *d0 = dst + (y * 2) * (w * 2);
		rgba_t *d1 = d0 + w * 2;
		for (int x = 0; x < w; x++) {
			rgba_t p = src[y * w + x];
			d0[x * 2] = d0[x * 2 + 1] = p;
			d1[x * 2] = d1[x * 2 + 1] = p;
		}
	}
}
