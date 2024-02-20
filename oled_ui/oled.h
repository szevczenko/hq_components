#ifndef OLED_H_
#define OLED_H_

#include "oled_glcd.h"
#include "ssd1306.h"
#include "ssd1306_fonts.h"

extern const PROGMEM uint8_t Calibri11x13[];
extern const PROGMEM uint8_t Calibri12x13_RU[];
extern const PROGMEM uint8_t Calibri10x13_PL[];
extern const PROGMEM uint8_t Calibri13x17[];
extern const PROGMEM uint8_t Calibri14x17_PL[];
extern const PROGMEM uint8_t Calibri16x17_RU[];
extern const PROGMEM uint8_t Calibri21x24[];
extern const PROGMEM uint8_t Calibri21x24_PL[];
extern const PROGMEM uint8_t Calibri26x24_RU[];
extern const PROGMEM uint8_t Calibri21x26_PL[];

extern const PROGMEM uint8_t Bookman_Old_Style11x13[];
extern const PROGMEM uint8_t Calibri15x17[];
extern const PROGMEM uint8_t Calibri10x13[];
extern const PROGMEM uint8_t Calibri11x13_PL[];
extern const PROGMEM uint8_t Calibri13x13_RU[];

enum oledFontSize
{
  OLED_FONT_SIZE_11,
  OLED_FONT_SIZE_16,
  OLED_FONT_SIZE_26,
  OLED_FONT_SIZE_LAST
};

void oled_init( void );
void oled_clearScreen( void );
void oled_printFixed( lcdint_t xpos, lcdint_t y, const char* ch, enum oledFontSize font_size );
void oled_printFixedBlack( lcdint_t xpos, lcdint_t y, const char* ch, enum oledFontSize font_size );
void oled_update( void );
void oled_setCursor( lcdint_t xpos, lcdint_t y );
void oled_print( const char* ch );
void oled_printBlack( const char* ch );
void oled_putPixel( lcdint_t x, lcdint_t y );
void oled_clearPixel( lcdint_t x, lcdint_t y );

void oled_getGLCDCharBitmap( uint16_t unicode, SCharInfo* info );
void oled_setGLCDFont( enum oledFontSize font_size );
uint8_t oled_printGLCDChar( uint8_t c );
uint8_t draw_GLCD( lcdint_t x, lcdint_t y, lcduint_t w, lcduint_t h, const uint8_t* bitmap );

#endif