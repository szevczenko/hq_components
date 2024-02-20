#include "oled.h"

#include "app_config.h"
#include "ssd1306.h"
#include "ssd1306_1bit.h"

#define YADDR1( y )     ( (uint16_t) ( ( y ) >> 3 ) * m_w )
#define BANK_ADDR1( b ) ( ( b ) * m_w )

#define DISPLAY_WIDTH    128
#define DISPLAY_HEIGHT   64
#define FONTS_TABLE_SIZE 3

enum
{
  CANVAS_MODE_BASIC = 0x00,
  /** If the flag is specified, text cursor is moved to new line when end of screen is reached */
  CANVAS_TEXT_WRAP = 0x01,
  /** This flag make bitmaps transparent (Black color) */
  CANVAS_MODE_TRANSPARENT = 0x02,
  /** If the flag is specified, text cursor is moved to new line when end of canvas is reached */
  CANVAS_TEXT_WRAP_LOCAL = 0x04,
};

enum
{
  BLACK = 0x00,    ///< Black color
  WHITE = 0xFF,    ///< White color
};

struct NanoPoint
{
  lcdint_t x;
  lcdint_t y;
};

struct oledFontData
{
  oledGLCDInfo info;
  const uint8_t* data;
};

struct oledFont
{
  lcduint_t width;
  lcduint_t height;
  struct oledFontData data[FONTS_TABLE_SIZE];
};

static lcduint_t m_w = DISPLAY_WIDTH;    ///< width of NanoCanvas area in pixels
static lcduint_t m_h = DISPLAY_HEIGHT;    ///< height of NanoCanvas area in pixels
static lcdint_t m_cursorX;    ///< current X cursor position for text output
static lcdint_t m_cursorY;    ///< current Y cursor position for text output
static uint8_t m_buf[DISPLAY_WIDTH * ( DISPLAY_HEIGHT / 8 )];    ///< Canvas data
static uint16_t m_color;    ///< current color for monochrome operations
static struct NanoPoint offset;
static struct oledFont* actual_font;

static struct oledFont font11 =
  {
    .width = 11,
    .height = 13,
    .data = {{ .data = Calibri10x13 }, { .data = Calibri13x13_RU }, { .data = Calibri11x13_PL }},
};

static struct oledFont font16 =
  {
    .width = 16,
    .height = 17,
    .data = {{ .data = Calibri15x17 }, { .data = Calibri14x17_PL }, { .data = Calibri16x17_RU }},
};

static struct oledFont font26 =
  {
    .width = 16,
    .height = 26,
    .data = {{ .data = Calibri21x24 }, { .data = Calibri21x26_PL }, { .data = Calibri26x24_RU }},
};

extern SFixedFontInfo s_fixedFont;
#ifdef CONFIG_SSD1306_UNICODE_ENABLE
extern uint8_t g_ssd1306_unicode;
#endif

static size_t write( uint8_t c )
{
  if ( c == '\n' )
  {
    m_cursorY += (lcdint_t) actual_font->height;
    m_cursorX = 0;
  }
  else if ( c == '\r' )
  {
    // skip non-printed char
  }
  else
  {
    return oled_printGLCDChar( c );
  }
  return 1;
}

static struct oledFont* _get_font_table( enum oledFontSize font_size )
{
  switch ( font_size )
  {
    case OLED_FONT_SIZE_11:
      return &font11;

    case OLED_FONT_SIZE_16:
      return &font16;

    case OLED_FONT_SIZE_26:
      return &font26;

    default:
      break;
  }

  return NULL;
}

static const uint8_t* _read_unicode_record( oledGLCDInfo* r, const uint8_t* p )
{
  r->first_symbol = ( ( pgm_read_byte( &p[3] ) << 8 ) | ( pgm_read_byte( &p[2] ) ) );
  r->last_symbol = ( ( pgm_read_byte( &p[5] ) << 8 ) | ( pgm_read_byte( &p[4] ) ) );
  r->height = pgm_read_byte( &p[6] );
  r->count = r->last_symbol - r->first_symbol;
  return ( r->count > 0 ) ? ( &p[8] ) : NULL;
}

static void _get_GLCD_char_bitmap( uint16_t unicode, SCharInfo* info )
{
  if ( info == NULL )
  {
    return;
  }

  for ( int i = 0; i < FONTS_TABLE_SIZE; i++ )
  {
    if ( ( unicode < actual_font->data[i].info.first_symbol ) || ( unicode >= actual_font->data[i].info.last_symbol ) )
    {
      continue;
    }
    /* At this point data points to jump table (height|addr|addr|addr) */
    const uint8_t* data = actual_font->data[i].data;
    unicode -= actual_font->data[i].info.first_symbol;
    data += unicode * 4 + 8;
    uint32_t addr = ( pgm_read_byte( &data[3] ) << 16 ) | ( pgm_read_byte( &data[2] ) << 8 ) | ( pgm_read_byte( &data[1] ) );
    info->width = pgm_read_byte( &data[0] );
    info->height = actual_font->height;
    info->spacing = 1;
    info->glyph = &actual_font->data[i].data[addr];
    return;
  }

  if ( !info->glyph )
  {
    info->width = 0;
    info->height = 0;
    info->spacing = actual_font->width >> 1;
  }
}

/////////////////////////////////////////////////////////////////////////////////
//
//                            COMMON GRAPHICS
//
/////////////////////////////////////////////////////////////////////////////////

void oled_init( void )
{
  m_color = WHITE;
  oled_clearScreen();
  for ( int i = 0; i < FONTS_TABLE_SIZE; i++ )
  {
    _read_unicode_record( &font11.data[i].info, font11.data[i].data );
    _read_unicode_record( &font16.data[i].info, font16.data[i].data );
    _read_unicode_record( &font26.data[i].info, font26.data[i].data );
  }
  oled_setGLCDFont( OLED_FONT_SIZE_11 );
}

void oled_clearScreen( void )
{
  memset( m_buf, 0, YADDR1( m_h ) );
}

void oled_setGLCDFont( enum oledFontSize font_size )
{
  struct oledFont* font = _get_font_table( font_size );
  if ( font != NULL )
  {
    actual_font = font;
  }
}

uint8_t oled_printGLCDChar( uint8_t c )
{
  uint16_t unicode = ssd1306_unicode16FromUtf8( c );

  if ( unicode == SSD1306_MORE_CHARS_REQUIRED )
  {
    return 0;
  }

  SCharInfo char_info = { 0 };
  _get_GLCD_char_bitmap( unicode, &char_info );
  draw_GLCD( m_cursorX,
             m_cursorY,
             char_info.width,
             char_info.height,
             char_info.glyph );
  m_cursorX += (lcdint_t) ( char_info.width + char_info.spacing );
  if ( m_cursorX > ( (lcdint_t) m_w - (lcdint_t) actual_font->width ) || ( m_cursorX > ( (lcdint_t) m_w - (lcdint_t) actual_font->width ) ) )
  {
    m_cursorY += (lcdint_t) actual_font->height;
    m_cursorX = 0;
    if ( m_cursorY > ( (lcdint_t) m_h - (lcdint_t) actual_font->height ) )
    {
      m_cursorY = 0;
    }
  }
  return 1;
}

void oled_setCursor( lcdint_t xpos, lcdint_t y )
{
  m_cursorX = xpos;
  m_cursorY = y;
}

void oled_print( const char* ch )
{
  while ( *ch )
  {
    write( *ch );
    ch++;
  }
}

void oled_printBlack( const char* ch )
{
  m_color = BLACK;
  oled_print( ch );
  m_color = WHITE;
}

void oled_printFixed( lcdint_t xpos, lcdint_t y, const char* ch, enum oledFontSize font_size )
{
  oled_setGLCDFont( font_size );
  m_cursorX = xpos;
  m_cursorY = y;
  while ( *ch )
  {
    write( *ch );
    ch++;
  }
}

void oled_printFixedBlack( lcdint_t xpos, lcdint_t y, const char* ch, enum oledFontSize font_size )
{
  m_color = BLACK;
  oled_printFixed( xpos, y, ch, font_size );
  m_color = WHITE;
}

/////////////////////////////////////////////////////////////////////////////////
//
//                             1-BIT GRAPHICS
//
/////////////////////////////////////////////////////////////////////////////////

void oled_putPixel( lcdint_t x, lcdint_t y )
{
  x -= offset.x;
  y -= offset.y;
  if ( ( x < 0 ) || ( y < 0 ) )
    return;
  if ( ( x >= (lcdint_t) m_w ) || ( y >= (lcdint_t) m_h ) )
    return;
  m_buf[YADDR1( y ) + x] |= ( 1 << ( y & 0x7 ) );
}

void oled_clearPixel( lcdint_t x, lcdint_t y )
{
  x -= offset.x;
  y -= offset.y;
  if ( ( x < 0 ) || ( y < 0 ) )
    return;
  if ( ( x >= (lcdint_t) m_w ) || ( y >= (lcdint_t) m_h ) )
    return;
  m_buf[YADDR1( y ) + x] &= ~( 1 << ( y & 0x7 ) );
}

uint8_t draw_GLCD( lcdint_t x, lcdint_t y, lcduint_t w, lcduint_t h, const uint8_t* bitmap )
{
  uint8_t i;
  uint8_t bytes_width;

  if ( ( w % 8 ) > 0 )
  {
    bytes_width = ( w / 8 ) + 1;
  }
  else
  {
    bytes_width = ( w / 8 );
  }

  for ( i = 0; i < h; i++ )
  {
    uint8_t j;
    for ( j = 0; j < bytes_width; j++ )
    {
      uint8_t dat = pgm_read_byte( bitmap + i * bytes_width + j );
      uint8_t bit;
      for ( bit = 0; bit < 8; bit++ )
      {
        if ( x + j * 8 + bit >= ssd1306_lcd.width || y + i >= ssd1306_lcd.height )
        {
          /* Don't write past the dimensions of the LCD, skip the entire char */
          return 0;
        }

        /* We should not write if the y bit exceeds font width */
        if ( ( j * 8 + bit ) >= w )
        {
          /* Skip the bit */
          continue;
        }

        if ( dat & ( 1 << bit ) )
        {
          if ( m_color == BLACK )
          {
            oled_clearPixel( x + j * 8 + bit, y + i );
          }
          else
          {
            oled_putPixel( x + j * 8 + bit, y + i );
          }
        }
      }
    }
  }
  return w;
}

void oled_update( void )
{
  ssd1306_drawBufferFast( offset.x, offset.y, m_w, m_h, m_buf );
}