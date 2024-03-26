#include <errno.h>
#include <lwip/def.h>
#include <lwip/sockets.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>

#include "app_config.h"
#include "cmd_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "keepalive.h"
#include "parse_cmd.h"

#define MODULE_NAME "[CMD Cl Req] "
#define DEBUG_LVL   PRINT_INFO

#if CONFIG_DEBUG_CMD_CLIENT
#define LOG( _lvl, ... ) \
  debug_printf( DEBUG_LVL, _lvl, MODULE_NAME __VA_ARGS__ )
#else
#define LOG( PRINT_INFO, ... )
#endif

#define PAYLOAD_SIZE 256
#define QUEUE_SIZE   16

typedef struct
{
  SemaphoreHandle_t sem;
  void* send_data;
  void* rx_data;
  uint32_t send_data_size;
  uint32_t rx_data_size;
  uint32_t timeout_ms;
  uint32_t request_number;
  error_code_t result;
} request_command_data_t;

struct cmd_client_req_context
{
  uint32_t request_number;
  QueueHandle_t msg_queue;
  uint8_t buffer[PAYLOAD_SIZE];
};

static struct cmd_client_req_context ctx;

static error_code_t _receive_packet( TickType_t timeout, uint32_t* read_bytes )
{
  uint32_t bytes_read = 0;

  do
  {
    int ret = cmdClientRead( &ctx.buffer[bytes_read], PACKET_SIZE - bytes_read, ST2MS( timeout - xTaskGetTickCount() ) );
    if ( ret < 0 )
    {
      LOG( PRINT_ERROR, "%s Error read %d", __func__, ret );
      return ERROR_CODE_FAIL;
    }

    bytes_read += ret;

    if ( timeout < xTaskGetTickCount() )
    {
      LOG( PRINT_DEBUG, "%s Timeout", __func__ );
      return ERROR_CODE_TIMEOUT;
    }
  } while ( bytes_read < PACKET_SIZE );

  *read_bytes = bytes_read;
  return ERROR_CODE_OK;
}

static error_code_t _request_msg_process( request_command_data_t* msg, uint32_t* read_len )
{
  assert( msg );
  assert( read_len );

  TickType_t timeout = MS2ST( msg->timeout_ms ) + xTaskGetTickCount();
  int ret = cmdClientSend( msg->send_data, msg->send_data_size );
  uint32_t packet_number = 0;
  *read_len = 0;

  if ( ret != msg->send_data_size )
  {
    LOG( PRINT_ERROR, "%s Bad sent size %d %d", __func__, ret, msg->send_data_size );
    return ERROR_CODE_FAIL;
  }

  if ( msg->rx_data == NULL )
  {
    return ERROR_CODE_OK;
  }

  do
  {
    error_code_t result = _receive_packet( timeout, read_len );

    if ( result != ERROR_CODE_OK )
    {
      return result;
    }

    memcpy( &packet_number, &ctx.buffer[FRAME_REQ_NUMBER_POS], sizeof( packet_number ) );

    if ( packet_number == msg->request_number )
    {
      memcpy( msg->rx_data, ctx.buffer, msg->rx_data_size );
      return ERROR_CODE_OK;
    }
    else
    {
      LOG( PRINT_DEBUG, "%s Bad packet number %d wait %d", __func__, packet_number, msg->request_number );
    }

    if ( timeout < xTaskGetTickCount() )
    {
      LOG( PRINT_DEBUG, "%s Timeout", __func__ );
      break;
    }
  } while ( true );

  return ERROR_CODE_FAIL;
}

static void _requests_process( void* arg )
{
  ctx.msg_queue = xQueueCreate( QUEUE_SIZE, sizeof( request_command_data_t* ) );
  request_command_data_t* msg = NULL;

  while ( 1 )
  {
    if ( xQueueReceive( ctx.msg_queue, &msg, portMAX_DELAY ) == pdTRUE )
    {
      uint32_t len = 0;
      msg->result = _request_msg_process( msg, &len );
      if ( msg->sem != NULL )
      {
        xSemaphoreGive( msg->sem );
      }
    }
    else
    {
      LOG( PRINT_ERROR, "%s xQueueReceive", __func__ );
    }
  }
}

static void _cleanup_msg( request_command_data_t* msg )
{
  if ( msg != NULL )
  {
    vSemaphoreDelete( msg->sem );
    if ( msg->send_data != NULL )
    {
      free( msg->send_data );
    }

    if ( msg->rx_data != NULL )
    {
      free( msg->rx_data );
    }

    free( msg );
  }
}

static request_command_data_t* _prepare_msg( uint8_t val, parseType_t type, uint32_t timeout )
{
  request_command_data_t* msg = malloc( sizeof( request_command_data_t ) );
  uint8_t* sendBuff = malloc( PACKET_SIZE );
  uint8_t* rxBuff = NULL;
  assert( msg );
  assert( sendBuff );
  memset( msg, 0, sizeof( request_command_data_t ) );
  memset( sendBuff, 0, PACKET_SIZE );
  msg->sem = xSemaphoreCreateBinary();
  assert( msg->sem );

  uint32_t request_number = ctx.request_number++;

  sendBuff[FRAME_LEN_POS] = PACKET_SIZE;
  memcpy( &sendBuff[FRAME_REQ_NUMBER_POS], &request_number, sizeof( request_number ) );

  sendBuff[FRAME_PARSE_TYPE_POS] = type;
  sendBuff[FRAME_VALUE_TYPE_POS] = val;
  sendBuff[FRAME_CMD_POS] = CMD_REQUEST;

  if ( timeout == 0 )
  {
    sendBuff[FRAME_CMD_POS] = CMD_DATA;
  }
  else
  {
    rxBuff = malloc( PACKET_SIZE );
    assert( rxBuff );
    msg->rx_data = (void*) rxBuff;
    msg->rx_data_size = PACKET_SIZE;
  }

  msg->send_data = (void*) sendBuff;
  msg->send_data_size = PACKET_SIZE;
  msg->request_number = request_number;
  msg->timeout_ms = timeout;

  return msg;
}

static request_command_data_t* _prepare_u32_msg( parameter_value_t val, uint32_t value, parseType_t type, uint32_t timeout )
{
  request_command_data_t* msg = _prepare_msg( val, type, timeout );
  if ( msg == NULL )
  {
    return NULL;
  }

  if ( type == PC_SET_UINT32 )
  {
    memcpy( &( (uint8_t*) msg->send_data )[FRAME_VALUE_POS], (uint8_t*) &value, sizeof( value ) );
  }

  return msg;
}

static request_command_data_t* _prepare_string_msg( parameter_string_t val, char* str, parseType_t type, uint32_t timeout )
{
  request_command_data_t* msg = _prepare_msg( val, type, timeout );
  if ( msg == NULL )
  {
    return NULL;
  }

  if ( type == PC_SET_STRING )
  {
    strcpy( &( (char*) msg->send_data )[FRAME_VALUE_POS], str );
  }

  return msg;
}

error_code_t cmdClientSetValueWithoutResp( parameter_value_t val, uint32_t value )
{
  LOG( PRINT_DEBUG, "%s: %d %d", __func__, val, value );
  if ( parameters_setValue( val, value ) == false )
  {
    LOG( PRINT_ERROR, "%s: cannot set value", __func__ );
    return ERROR_CODE_FAIL;
  }

  request_command_data_t* msg = _prepare_u32_msg( val, value, PC_SET_UINT32, 0 );

  if ( msg == NULL )
  {
    return ERROR_CODE_FAIL;
  }

  if ( xQueueSend( ctx.msg_queue, &msg, 0 ) != pdTRUE )
  {
    _cleanup_msg( msg );
    LOG( PRINT_ERROR, "%s: cannot add msg to queue", __func__ );
    return ERROR_CODE_FAIL;
  }

  if ( xSemaphoreTake( msg->sem, portMAX_DELAY ) != pdTRUE )
  {
    _cleanup_msg( msg );
    LOG( PRINT_ERROR, "%s: cannot take semaphore", __func__ );
    return ERROR_CODE_FAIL;
  }

  _cleanup_msg( msg );

  if ( msg->result != ERROR_CODE_OK )
  {
    LOG( PRINT_ERROR, "%s: Bad result", __func__ );
  }

  return msg->result;
}

error_code_t cmdClientGetValue( parameter_value_t val, uint32_t* value, uint32_t timeout )
{
  LOG( PRINT_DEBUG, "%s %d", __func__, val );
  if ( val >= PARAM_LAST_VALUE )
  {
    LOG( PRINT_ERROR, "%s: Invalid argument", __func__ );
    return ERROR_CODE_FAIL;
  }

  request_command_data_t* msg = _prepare_u32_msg( val, 0, PC_GET_UINT32, timeout );

  if ( xQueueSend( ctx.msg_queue, &msg, 0 ) != pdTRUE )
  {
    _cleanup_msg( msg );
    LOG( PRINT_ERROR, "%s: cannot add msg to queue", __func__ );
    return ERROR_CODE_FAIL;
  }

  if ( xSemaphoreTake( msg->sem, portMAX_DELAY ) != pdTRUE )
  {
    _cleanup_msg( msg );
    LOG( PRINT_ERROR, "%s: cannot take semaphore", __func__ );
    return ERROR_CODE_FAIL;
  }

  if ( msg->result != ERROR_CODE_OK )
  {
    _cleanup_msg( msg );
    LOG( PRINT_ERROR, "%s: Bad result", __func__ );
    return msg->result;
  }

  uint32_t rx_req_number = 0;
  memcpy( &rx_req_number, &( (char*) msg->rx_data )[FRAME_REQ_NUMBER_POS], sizeof( rx_req_number ) );

  if ( rx_req_number != msg->request_number )
  {
    LOG( PRINT_ERROR, "%s Bad req number %d %d", __func__, msg->request_number, rx_req_number );
    _cleanup_msg( msg );
    return ERROR_CODE_FAIL;
  }

  if ( CMD_ANSWER != ( (char*) msg->rx_data )[FRAME_CMD_POS] )
  {
    LOG( PRINT_ERROR, "%s bad cmd %x", __func__, ( (char*) msg->rx_data )[FRAME_CMD_POS] );
    _cleanup_msg( msg );
    return ERROR_CODE_FAIL;
  }

  if ( PC_GET_UINT32 != ( (char*) msg->rx_data )[FRAME_PARSE_TYPE_POS] )
  {
    LOG( PRINT_ERROR, "%s bad type %d", __func__, ( (char*) msg->rx_data )[FRAME_PARSE_TYPE_POS] );
    _cleanup_msg( msg );
    return ERROR_CODE_FAIL;
  }

  if ( ( (char*) msg->rx_data )[FRAME_VALUE_TYPE_POS] != val )
  {
    LOG( PRINT_ERROR, "%s receive %d wait %d", __func__, ( (char*) msg->rx_data )[FRAME_VALUE_TYPE_POS], val );
    _cleanup_msg( msg );
    return ERROR_CODE_FAIL;
  }

  uint32_t return_value = 0;
  memcpy( &return_value, &( (char*) msg->rx_data )[FRAME_VALUE_POS], sizeof( return_value ) );

  if ( parameters_setValue( val, return_value ) == false )
  {
    LOG( PRINT_INFO, "%s error set val %d = %d", __func__, val, return_value );
    _cleanup_msg( msg );
    return ERROR_CODE_FAIL;
  }

  if ( value != NULL )
  {
    *value = return_value;
  }

  _cleanup_msg( msg );
  return ERROR_CODE_OK;
}

error_code_t cmdClientGetString( parameter_string_t val, char* str, uint32_t str_len, uint32_t timeout )
{
  LOG( PRINT_DEBUG, "%s %d", __func__, val );
  if ( val >= PARAM_STR_LAST_VALUE )
  {
    LOG( PRINT_ERROR, "%s: Invalid argument", __func__ );
    return ERROR_CODE_FAIL;
  }

  request_command_data_t* msg = _prepare_string_msg( val, NULL, PC_GET_STRING, timeout );

  if ( xQueueSend( ctx.msg_queue, &msg, 0 ) != pdTRUE )
  {
    _cleanup_msg( msg );
    LOG( PRINT_ERROR, "%s: cannot add msg to queue", __func__ );
    return ERROR_CODE_FAIL;
  }

  if ( xSemaphoreTake( msg->sem, portMAX_DELAY ) != pdTRUE )
  {
    _cleanup_msg( msg );
    LOG( PRINT_ERROR, "%s: cannot take semaphore", __func__ );
    return ERROR_CODE_FAIL;
  }

  if ( msg->result != ERROR_CODE_OK )
  {
    _cleanup_msg( msg );
    LOG( PRINT_ERROR, "%s: Bad result", __func__ );
    return msg->result;
  }

  uint32_t rx_req_number = 0;
  memcpy( &rx_req_number, &( (char*) msg->rx_data )[FRAME_REQ_NUMBER_POS], sizeof( rx_req_number ) );

  if ( rx_req_number != msg->request_number )
  {
    LOG( PRINT_ERROR, "%s Bad req number %d %d", __func__, msg->request_number, rx_req_number );
    _cleanup_msg( msg );
    return ERROR_CODE_FAIL;
  }

  if ( CMD_ANSWER != ( (char*) msg->rx_data )[FRAME_CMD_POS] )
  {
    LOG( PRINT_ERROR, "%s bad cmd %x", __func__, ( (char*) msg->rx_data )[FRAME_CMD_POS] );
    _cleanup_msg( msg );
    return ERROR_CODE_FAIL;
  }

  if ( PC_GET_STRING != ( (char*) msg->rx_data )[FRAME_PARSE_TYPE_POS] )
  {
    LOG( PRINT_ERROR, "%s bad type %d", __func__, ( (char*) msg->rx_data )[FRAME_PARSE_TYPE_POS] );
    _cleanup_msg( msg );
    return ERROR_CODE_FAIL;
  }

  if ( ( (char*) msg->rx_data )[FRAME_VALUE_TYPE_POS] != val )
  {
    LOG( PRINT_ERROR, "%s receive %d wait %d", __func__, ( (char*) msg->rx_data )[FRAME_VALUE_TYPE_POS], val );
    _cleanup_msg( msg );
    return ERROR_CODE_FAIL;
  }

  char* resp_str = &( (char*) msg->rx_data )[FRAME_VALUE_POS];

  if ( parameters_setString( val, resp_str ) == false )
  {
    LOG( PRINT_INFO, "%s error set val %d = %s", __func__, val, resp_str );
    _cleanup_msg( msg );
    return ERROR_CODE_FAIL;
  }

  if ( str != NULL && str_len > strlen( resp_str ) )
  {
    strcpy( str, resp_str );
  }

  _cleanup_msg( msg );
  return ERROR_CODE_OK;
}

error_code_t cmdClientSetValue( parameter_value_t val, uint32_t value, uint32_t timeout )
{
  LOG( PRINT_DEBUG, "%s %d", __func__, val );
  if ( val >= PARAM_LAST_VALUE )
  {
    LOG( PRINT_ERROR, "%s: Invalid argument", __func__ );
    return false;
  }

  if ( parameters_setValue( val, value ) == false )
  {
    LOG( PRINT_ERROR, "%s: canot set value %d = %d", __func__, val, value );
    return false;
  }

  request_command_data_t* msg = _prepare_u32_msg( val, value, PC_SET_UINT32, timeout );

  if ( xQueueSend( ctx.msg_queue, &msg, 0 ) != pdTRUE )
  {
    LOG( PRINT_ERROR, "%s: cannot add msg to queue", __func__ );
    _cleanup_msg( msg );
    return ERROR_CODE_FAIL;
  }

  if ( xSemaphoreTake( msg->sem, portMAX_DELAY ) != pdTRUE )
  {
    LOG( PRINT_ERROR, "%s: cannot take semaphore", __func__ );
    _cleanup_msg( msg );
    return ERROR_CODE_FAIL;
  }

  if ( msg->result < 0 )
  {
    LOG( PRINT_ERROR, "%s: Bad result", __func__ );
    return msg->result;
  }

  uint32_t rx_req_number = 0;
  memcpy( &rx_req_number, &( (char*) msg->rx_data )[FRAME_REQ_NUMBER_POS], sizeof( rx_req_number ) );

  if ( rx_req_number != msg->request_number )
  {
    LOG( PRINT_ERROR, "%s Bad req number %d %d", __func__, msg->request_number, rx_req_number );
    _cleanup_msg( msg );
    return ERROR_CODE_FAIL;
  }

  if ( CMD_ANSWER != ( (char*) msg->rx_data )[FRAME_CMD_POS] )
  {
    LOG( PRINT_ERROR, "%s bad cmd %x", __func__, ( (char*) msg->rx_data )[FRAME_CMD_POS] );
    _cleanup_msg( msg );
    return ERROR_CODE_FAIL;
  }

  if ( PC_SET_UINT32 != ( (char*) msg->rx_data )[FRAME_PARSE_TYPE_POS] )
  {
    LOG( PRINT_ERROR, "%s bad type %d", __func__, ( (char*) msg->rx_data )[FRAME_PARSE_TYPE_POS] );
    _cleanup_msg( msg );
    return ERROR_CODE_FAIL;
  }

  if ( ( (char*) msg->rx_data )[FRAME_VALUE_TYPE_POS] != val )
  {
    LOG( PRINT_ERROR, "%s receive %d wait %d", __func__, ( (char*) msg->rx_data )[FRAME_VALUE_TYPE_POS], val );
    _cleanup_msg( msg );
    return ERROR_CODE_FAIL;
  }

  if ( ( (char*) msg->rx_data )[FRAME_VALUE_POS] == NEGATIVE_RESP )
  {
    LOG( PRINT_WARNING, "%s negative responce", __func__ );
    _cleanup_msg( msg );
    return ERROR_CODE_FAIL;
  }
  else if ( ( (char*) msg->rx_data )[FRAME_VALUE_POS] == POSITIVE_RESP )
  {
    return ERROR_CODE_OK;
  }

  LOG( PRINT_ERROR, "%s Bad responce value", __func__ );
  _cleanup_msg( msg );
  return ERROR_CODE_FAIL;
}

void cmdClientReqStartTask( void )
{
  xTaskCreate( _requests_process, "_requests_process", 4096, NULL, NORMALPRIO, NULL );
}
