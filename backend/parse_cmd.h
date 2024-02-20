#ifndef _PARSE_CMD_H
#define _PARSE_CMD_H
#include "app_config.h"
#include "parameters.h"

#define CMD_REQEST  0x11
#define CMD_ANSWER  0x22
#define CMD_DATA    0x33
#define CMD_COMMAND 0x44

#define POSITIVE_RESP 0xFF
#define NEGATIVE_RESP 0xFE

#define FRAME_LEN_POS        0
#define FRAME_REQ_NUMBER_POS 1
#define FRAME_CMD_POS        5
#define FRAME_PARSE_TYPE_POS 6
#define FRAME_VALUE_TYPE_POS 7
#define FRAME_VALUE_POS      8

#define PACKET_SIZE 16

typedef enum
{
  PC_KEEP_ALIVE,
  PC_SET,
  PC_GET,
  PC_SET_ALL,
  PC_GET_ALL,
  PC_LAST,
} parseType_t;

typedef enum
{
  PC_CMD_NONE,
  PC_CMD_RESET_ERROR,
  PC_CMD_LAST,
} parseCmd_t;

void parse_server_buffer( uint8_t* buff, uint32_t len );

#endif
