#ifndef FROST_DISPLAY_COLOR_H
#define FROST_DISPLAY_COLOR_H
/* This unit displays RGB565, high byte first. Upstream's BGR assumption
 * produced orange for requested blue on the user's physical GP-5.
 * UI_PANEL converts to little-endian buffer storage, without channel swap. */
#define FROST_RGB565(r,g,b) ((uint16_t)((((r)>>3)<<11)|(((g)>>2)<<5)|((b)>>3)))
#ifdef UI_PANEL
#define COLOR(r,g,b) ((uint16_t)((FROST_RGB565(r,g,b)>>8)|(FROST_RGB565(r,g,b)<<8)))
#else
#define COLOR(r,g,b) FROST_RGB565(r,g,b)
#endif
#endif
