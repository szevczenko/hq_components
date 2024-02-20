#ifndef OLED_GLCD_H
#define OLED_GLDC_H
#include "app_config.h"

/** Structure describes font format in memory */
typedef struct
{
  uint16_t first_symbol;    ///< first unicode symbol number
  uint16_t last_symbol;    ///< last unicode symbol number
  uint8_t height;    ///< height in pixels
  uint16_t count;    ///< count symbols in table
} oledGLCDInfo;

// /** Structure describes unicode block in font data */
// typedef struct
// {
//     uint16_t start_code;  ///< unicode start code
//     uint8_t count;        ///< count of unicode chars in block
// } SUnicodeBlockRecord;

#endif