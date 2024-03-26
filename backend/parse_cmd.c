#include "parse_cmd.h"

#include "cmd_client.h"
#include "cmd_server.h"
#include "keepalive.h"
#include "parameters.h"

#define MODULE_NAME "[PARSE] "
#define DEBUG_LVL   PRINT_WARNING

#if CONFIG_DEBUG_PARSE_CMD
#define LOG( _lvl, ... ) \
  debug_printf( DEBUG_LVL, _lvl, MODULE_NAME __VA_ARGS__ )
#else
#define LOG( PRINT_INFO, ... )
#endif

static uint8_t sendBuff[PACKET_SIZE];
static uint32_t frameLenServer;

static void _parse_server( uint8_t* buff, uint32_t len );

void parse_server_buffer( uint8_t* buff, uint32_t len )
{
  uint32_t parsed_len = 0;

  do
  {
    frameLenServer = buff[parsed_len];

    if ( frameLenServer > len )
    {
      LOG( PRINT_ERROR, "%s: Bad lenth", __func__ );
      break;
    }

    if ( frameLenServer == 0 )
    {
      LOG( PRINT_ERROR, "%s: Lenth is 0", __func__ );
      break;
    }

    _parse_server( &buff[parsed_len], frameLenServer );
    len -= frameLenServer;
    parsed_len += frameLenServer;
  } while ( len > 0 );
}

static void _prepare_answer( uint32_t request_number, parseType_t type, uint8_t val )
{
  sendBuff[FRAME_LEN_POS] = PACKET_SIZE;
  memcpy( &sendBuff[FRAME_REQ_NUMBER_POS], &request_number, sizeof( request_number ) );
  sendBuff[FRAME_CMD_POS] = CMD_ANSWER;
  sendBuff[FRAME_PARSE_TYPE_POS] = type;
  sendBuff[FRAME_VALUE_TYPE_POS] = val;
}

void _parse_server( uint8_t* buff, uint32_t len )
{
  uint32_t value = 0;
  uint32_t request_number = 0;
  uint8_t val = 0;

  memcpy( &request_number, &buff[FRAME_REQ_NUMBER_POS], sizeof( request_number ) );
  memset( sendBuff, 0, sizeof( sendBuff ) );

  LOG( PRINT_DEBUG, "%s len %d, req %d, cmd %x, type %x", __func__, len, request_number, buff[FRAME_CMD_POS],
       buff[FRAME_PARSE_TYPE_POS] );

  if ( ( buff[FRAME_CMD_POS] == CMD_REQUEST ) || ( buff[FRAME_CMD_POS] == CMD_DATA ) )
  {
    parseType_t type = buff[FRAME_PARSE_TYPE_POS];

    switch ( type )
    {
      case PC_KEEP_ALIVE:
        LOG( PRINT_DEBUG, "%s get keepAlive", __func__ );
        break;

      case PC_GET_UINT32:
        memcpy( &sendBuff[FRAME_REQ_NUMBER_POS], &request_number, sizeof( request_number ) );
        val = buff[FRAME_VALUE_TYPE_POS];
        _prepare_answer( request_number, type, val );
        value = parameters_getValue( buff[FRAME_VALUE_TYPE_POS] );
        memcpy( &sendBuff[FRAME_VALUE_POS], &value, sizeof( value ) );
        cmdServerSendData( sendBuff, PACKET_SIZE );
        break;

      case PC_SET_UINT32:
        memcpy( &sendBuff[FRAME_REQ_NUMBER_POS], &request_number, sizeof( request_number ) );
        val = buff[FRAME_VALUE_TYPE_POS];
        _prepare_answer( request_number, type, val );
        memcpy( &value, &buff[FRAME_VALUE_POS], sizeof( value ) );
        if ( parameters_setValue( buff[FRAME_VALUE_TYPE_POS], value ) )
        {
          sendBuff[FRAME_VALUE_POS] = POSITIVE_RESP;
        }
        else
        {
          sendBuff[FRAME_VALUE_POS] = NEGATIVE_RESP;
        }

        if ( buff[FRAME_CMD_POS] != CMD_DATA )
        {
          cmdServerSendData( sendBuff, PACKET_SIZE );
        }

        parameters_debugPrintValue( buff[FRAME_VALUE_TYPE_POS] );
        break;

      case PC_SET_STRING:
        memcpy( &sendBuff[FRAME_REQ_NUMBER_POS], &request_number, sizeof( request_number ) );
        val = buff[FRAME_VALUE_TYPE_POS];
        _prepare_answer( request_number, type, val );
        if ( parameters_setString( val, (const char*) &buff[FRAME_VALUE_POS] ) )
        {
          sendBuff[FRAME_VALUE_POS] = POSITIVE_RESP;
        }
        else
        {
          sendBuff[FRAME_VALUE_POS] = NEGATIVE_RESP;
        }

        if ( buff[FRAME_CMD_POS] != CMD_DATA )
        {
          cmdServerSendData( sendBuff, PACKET_SIZE );
        }

        parameters_debugPrintValue( buff[FRAME_VALUE_TYPE_POS] );
        break;

      case PC_GET_STRING:
        memcpy( &sendBuff[FRAME_REQ_NUMBER_POS], &request_number, sizeof( request_number ) );
        val = buff[FRAME_VALUE_TYPE_POS];
        _prepare_answer( request_number, type, val );
        parameters_getString( val, (char*) &sendBuff[FRAME_VALUE_POS], sizeof( sendBuff ) - FRAME_VALUE_POS );
        cmdServerSendData( sendBuff, PACKET_SIZE );
        break;

      default:
        break;
    }
  }
  else if ( buff[FRAME_CMD_POS] == CMD_ANSWER )
  {
    LOG( PRINT_ERROR, "%s: CMD_ANSWER unsupported", __func__ );
  }
  else if ( buff[FRAME_CMD_POS] == CMD_COMMAND )
  {
    LOG( PRINT_ERROR, "%s: CMD_COMMAND unsupported", __func__ );
  }
  else
  {
    LOG( PRINT_ERROR, "%s: CMD_COMMAND unsupported", __func__ );
  }
}
