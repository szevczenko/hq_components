#include <errno.h>
#include <lwip/def.h>
#include <lwip/sockets.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>

#include "cmd_client.h"
#include "app_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "keepalive.h"
#include "parse_cmd.h"

#define MODULE_NAME "[CMD Cl Req] "
#define DEBUG_LVL   PRINT_DEBUG

#if CONFIG_DEBUG_CMD_CLIENT
#define LOG( _lvl, ... ) \
  debug_printf( DEBUG_LVL, _lvl, MODULE_NAME __VA_ARGS__ )
#else
#define LOG( PRINT_INFO, ... )
#endif

#define PAYLOAD_SIZE 256
#define QUEUE_SIZE   16

struct cmd_client_request_data
{
  SemaphoreHandle_t sem;
  void* send_data;
  void* rx_data;
  uint32_t send_data_size;
  uint32_t rx_data_size;
  uint32_t timeout_ms;
  uint32_t request_number;
  int result;
};

struct cmd_client_req_context
{
  uint32_t request_number;
  QueueHandle_t msg_queue;
  uint8_t buffer[PAYLOAD_SIZE];
};

static struct cmd_client_req_context ctx;

static int _receive_packet( TickType_t timeout )
{
  uint32_t bytes_read = 0;

  do
  {
    int ret = cmdClientRead( &ctx.buffer[bytes_read], PACKET_SIZE - bytes_read, ST2MS( timeout - xTaskGetTickCount() ) );
    if ( ret < 0 )
    {
      LOG( PRINT_ERROR, "%s Error read %d", __func__, ret );
      return ERROR;
    }

    bytes_read += ret;

    if ( timeout < xTaskGetTickCount() )
    {
      LOG( PRINT_DEBUG, "%s Timeout", __func__ );
      return TIMEOUT;
    }
  } while ( bytes_read < PACKET_SIZE );

  return bytes_read;
}

static int _request_msg_process( struct cmd_client_request_data* msg )
{
  if ( msg == NULL )
  {
    LOG( PRINT_ERROR, "Invalid arg", __func__ );
    return ERROR;
  }

  TickType_t timeout = MS2ST( msg->timeout_ms ) + xTaskGetTickCount();
  int ret = cmdClientSend( msg->send_data, msg->send_data_size );
  uint32_t packet_number = 0;

  if ( ret != msg->send_data_size )
  {
    LOG( PRINT_ERROR, "%s Bad sent size %d %d", __func__, ret, msg->send_data_size );
    return ERROR;
  }

  if ( msg->rx_data == NULL )
  {
    return true;
  }

  do
  {
    ret = _receive_packet( timeout );

    if ( ( ret < 0 ) || ( ret != PACKET_SIZE ) )
    {
      return ret < 0 ? ret : ERROR;
    }

    memcpy( &packet_number, &ctx.buffer[FRAME_REQ_NUMBER_POS], sizeof( packet_number ) );

    if ( packet_number == msg->request_number )
    {
      memcpy( msg->rx_data, ctx.buffer, msg->rx_data_size );
      return true;
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

  return ERROR;
}

static void _requests_process( void* arg )
{
  ctx.msg_queue = xQueueCreate( QUEUE_SIZE, sizeof( struct cmd_client_request_data* ) );
  struct cmd_client_request_data* msg = NULL;

  while ( 1 )
  {
    if ( xQueueReceive( ctx.msg_queue, &msg, portMAX_DELAY ) == pdTRUE )
    {
      msg->result = _request_msg_process( msg );
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

int cmdClientSetValueWithoutResp( parameter_value_t val, uint32_t value )
{
  LOG( PRINT_DEBUG, "%s: %d %d", __func__, val, value );
  if ( parameters_setValue( val, value ) == false )
  {
    LOG( PRINT_ERROR, "%s: cannot set value", __func__ );
    return false;
  }

  uint32_t request_number = ctx.request_number++;
  uint8_t sendBuff[PACKET_SIZE] = { 0 };
  int result = 0;
  struct cmd_client_request_data* msg = malloc( sizeof( struct cmd_client_request_data ) );

  memset( msg, 0, sizeof( struct cmd_client_request_data ) );
  msg->sem = xSemaphoreCreateBinary();

  if ( msg->sem == NULL )
  {
    LOG( PRINT_ERROR, "%s: cannot create semaphore", __func__ );
    return ERROR;
  }

  sendBuff[FRAME_LEN_POS] = sizeof( sendBuff );
  memcpy( &sendBuff[FRAME_REQ_NUMBER_POS], &request_number, sizeof( request_number ) );
  sendBuff[FRAME_CMD_POS] = CMD_DATA;
  sendBuff[FRAME_PARSE_TYPE_POS] = PC_SET;
  sendBuff[FRAME_VALUE_TYPE_POS] = val;
  memcpy( &sendBuff[FRAME_VALUE_POS], (uint8_t*) &value, 4 );

  msg->request_number = request_number;
  msg->send_data = (void*) sendBuff;
  msg->send_data_size = sizeof( sendBuff );
  msg->timeout_ms = 0;

  if ( xQueueSend( ctx.msg_queue, &msg, 0 ) != pdTRUE )
  {
    vSemaphoreDelete( msg->sem );
    free( msg );
    LOG( PRINT_ERROR, "%s: cannot add msg to queue", __func__ );
    return ERROR;
  }

  if ( xSemaphoreTake( msg->sem, portMAX_DELAY ) != pdTRUE )
  {
    vSemaphoreDelete( msg->sem );
    free( msg );
    LOG( PRINT_ERROR, "%s: cannot take semaphore", __func__ );
    return ERROR;
  }

  vSemaphoreDelete( msg->sem );
  result = msg->result;
  free( msg );

  if ( result < 0 )
  {
    LOG( PRINT_ERROR, "%s: Bad result", __func__ );
    return result;
  }

  return true;
}

int cmdClientGetValue( parameter_value_t val, uint32_t* value, uint32_t timeout )
{
  LOG( PRINT_DEBUG, "%s %d", __func__, val );
  if ( val >= PARAM_LAST_VALUE )
  {
    LOG( PRINT_ERROR, "%s: Invalid argument", __func__ );
    return false;
  }

  struct cmd_client_request_data* msg = malloc( sizeof( struct cmd_client_request_data ) );

  memset( msg, 0, sizeof( struct cmd_client_request_data ) );

  msg->sem = xSemaphoreCreateBinary();

  if ( msg->sem == NULL )
  {
    LOG( PRINT_ERROR, "%s: cannot create semaphore", __func__ );
    return ERROR;
  }

  uint32_t return_value = 0;
  uint32_t request_number = ctx.request_number++;
  uint32_t rx_req_number = 0;
  uint8_t rxBuff[PACKET_SIZE] = { 0 };
  uint8_t sendBuff[PACKET_SIZE] = { 0 };
  int result = 0;

  sendBuff[FRAME_LEN_POS] = sizeof( sendBuff );
  memcpy( &sendBuff[FRAME_REQ_NUMBER_POS], &request_number, sizeof( request_number ) );
  sendBuff[FRAME_CMD_POS] = CMD_REQEST;
  sendBuff[FRAME_PARSE_TYPE_POS] = PC_GET;
  sendBuff[FRAME_VALUE_TYPE_POS] = val;

  msg->rx_data = (void*) rxBuff;
  msg->rx_data_size = sizeof( rxBuff );
  msg->send_data = (void*) sendBuff;
  msg->send_data_size = sizeof( sendBuff );
  msg->request_number = request_number;
  msg->timeout_ms = timeout;

  if ( xQueueSend( ctx.msg_queue, &msg, 0 ) != pdTRUE )
  {
    vSemaphoreDelete( msg->sem );
    free( msg );
    LOG( PRINT_ERROR, "%s: cannot add msg to queue", __func__ );
    return ERROR;
  }

  if ( xSemaphoreTake( msg->sem, portMAX_DELAY ) != pdTRUE )
  {
    vSemaphoreDelete( msg->sem );
    free( msg );
    LOG( PRINT_ERROR, "%s: cannot take semaphore", __func__ );
    return ERROR;
  }

  vSemaphoreDelete( msg->sem );
  result = msg->result;
  free( msg );

  if ( result < 0 )
  {
    LOG( PRINT_ERROR, "%s: Bad result", __func__ );
    return result;
  }

  memcpy( &rx_req_number, &rxBuff[FRAME_REQ_NUMBER_POS], sizeof( rx_req_number ) );

  if ( rx_req_number != request_number )
  {
    LOG( PRINT_ERROR, "%s Bad req number %d %d", __func__, request_number, rx_req_number );
    return ERROR;
  }

  if ( CMD_ANSWER != rxBuff[FRAME_CMD_POS] )
  {
    LOG( PRINT_ERROR, "%s bad cmd %x", __func__, rxBuff[FRAME_CMD_POS] );
    return ERROR;
  }

  if ( PC_GET != rxBuff[FRAME_PARSE_TYPE_POS] )
  {
    LOG( PRINT_ERROR, "%s bad type %d", __func__, rxBuff[FRAME_PARSE_TYPE_POS] );
    return ERROR;
  }

  if ( rxBuff[FRAME_VALUE_TYPE_POS] != val )
  {
    LOG( PRINT_ERROR, "%s receive %d wait %d", __func__, rxBuff[FRAME_VALUE_TYPE_POS], val );
    return ERROR;
  }

  memcpy( &return_value, &rxBuff[FRAME_VALUE_POS], sizeof( return_value ) );

  if ( parameters_setValue( val, return_value ) == false )
  {
    LOG( PRINT_INFO, "%s error set val %d = %d", __func__, val, return_value );
    return false;
  }

  if ( value != 0 )
  {
    *value = return_value;
  }

  return true;
}

int cmdClientSendCmd( parseCmd_t cmd )
{
  LOG( PRINT_ERROR, "%s Not ready", __func__ );
  return ERROR;
}

int cmdClientSetValue( parameter_value_t val, uint32_t value, uint32_t timeout )
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

  struct cmd_client_request_data* msg = malloc( sizeof( struct cmd_client_request_data ) );

  memset( msg, 0, sizeof( struct cmd_client_request_data ) );

  msg->sem = xSemaphoreCreateBinary();

  if ( msg->sem == NULL )
  {
    LOG( PRINT_ERROR, "%s: cannot create semaphore", __func__ );
    return ERROR;
  }

  uint32_t request_number = ctx.request_number++;
  uint32_t rx_req_number = 0;
  uint8_t rxBuff[PACKET_SIZE] = { 0 };
  uint8_t sendBuff[PACKET_SIZE] = { 0 };
  int result = 0;

  sendBuff[FRAME_LEN_POS] = sizeof( sendBuff );
  memcpy( &sendBuff[FRAME_REQ_NUMBER_POS], &request_number, sizeof( request_number ) );
  sendBuff[FRAME_CMD_POS] = CMD_REQEST;
  sendBuff[FRAME_PARSE_TYPE_POS] = PC_SET;
  sendBuff[FRAME_VALUE_TYPE_POS] = val;
  memcpy( &sendBuff[FRAME_VALUE_POS], (uint8_t*) &value, 4 );

  msg->rx_data = (void*) rxBuff;
  msg->rx_data_size = sizeof( rxBuff );
  msg->send_data = (void*) sendBuff;
  msg->send_data_size = sizeof( sendBuff );
  msg->request_number = request_number;
  msg->timeout_ms = timeout;

  if ( xQueueSend( ctx.msg_queue, &msg, 0 ) != pdTRUE )
  {
    vSemaphoreDelete( msg->sem );
    free( msg );
    LOG( PRINT_ERROR, "%s: cannot add msg to queue", __func__ );
    return ERROR;
  }

  if ( xSemaphoreTake( msg->sem, portMAX_DELAY ) != pdTRUE )
  {
    vSemaphoreDelete( msg->sem );
    free( msg );
    LOG( PRINT_ERROR, "%s: cannot take semaphore", __func__ );
    return ERROR;
  }

  vSemaphoreDelete( msg->sem );
  result = msg->result;
  free( msg );

  if ( result < 0 )
  {
    LOG( PRINT_ERROR, "%s: Bad result", __func__ );
    return result;
  }

  memcpy( &rx_req_number, &rxBuff[FRAME_REQ_NUMBER_POS], sizeof( rx_req_number ) );

  if ( rx_req_number != request_number )
  {
    LOG( PRINT_ERROR, "%s Bad req number %d %d", __func__, request_number, rx_req_number );
    return ERROR;
  }

  if ( CMD_ANSWER != rxBuff[FRAME_CMD_POS] )
  {
    LOG( PRINT_ERROR, "%s bad cmd %x", __func__, rxBuff[FRAME_CMD_POS] );
    return ERROR;
  }

  if ( PC_SET != rxBuff[FRAME_PARSE_TYPE_POS] )
  {
    LOG( PRINT_ERROR, "%s bad type %d", __func__, rxBuff[FRAME_PARSE_TYPE_POS] );
    return ERROR;
  }

  if ( rxBuff[FRAME_VALUE_TYPE_POS] != val )
  {
    LOG( PRINT_ERROR, "%s receive %d wait %d", __func__, rxBuff[FRAME_VALUE_TYPE_POS], val );
    return ERROR;
  }

  if ( rxBuff[FRAME_VALUE_POS] == NEGATIVE_RESP )
  {
    LOG( PRINT_WARNING, "%s negative responce", __func__ );
    return false;
  }
  else if ( rxBuff[FRAME_VALUE_POS] == POSITIVE_RESP )
  {
    return true;
  }
  else
  {
    LOG( PRINT_ERROR, "%s Bad responce value", __func__ );
    return ERROR;
  }

  return false;
}

void cmdClientReqStartTask( void )
{
  xTaskCreate( _requests_process, "_requests_process", 4096, NULL, NORMALPRIO, NULL );
}
