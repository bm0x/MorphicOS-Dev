#ifndef FONT_H
#define FONT_H

#include <stdint.h>

/* 
 * 8x16 Bitmap Font (VGA adaptation)
 * Only printable ASCII + replacement char for simplicity in this artifact.
 * Each character is 16 bytes (1 byte per row, 8 bits width).
 */

extern const uint8_t font8x16[256 * 16];

#endif // FONT_H
