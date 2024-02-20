#include "cmd_client.h"

#include <errno.h>
#include <lwip/def.h>
#include <lwip/sockets.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>

#include "app_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "keepalive.h"
#include "parse_cmd.h"

#define MODULE_NAME "[CMD Cl] "
#define DEBUG_LVL   PRINT_WARNING

#if CONFIG_DEBUG_CMD_CLIENT
#define LOG( _lvl, ... ) \
  debug_printf( DEBUG_LVL, _lvl, MODULE_NAME __VA_ARGS__ )
#else
#define LOG( PRINT_INFO, ... )
#endif

#define PAYLOAD_SIZE 256

enum cmd_client_app_state
{
  CMD_CLIENT_IDLE,
  CMD_CLIENT_CREATE_SOC,
  CMD_CLIENT_CONNECT_SERVER,
  CMD_CLIENT_STATE_READY,
  CMD_CLIENT_CLOSE_SOC,
  CMD_CLIENT_CHECK_ERRORS,
  CMD_CLIENT_TOP,
};

__attribute__( ( unused ) ) static const char* cmd_client_state_name[] =
  {
    [CMD_CLIENT_IDLE] = "CMD_CLIENT_IDLE",
    [CMD_CLIENT_CREATE_SOC] = "CMD_CLIENT_CREATE_SOC",
    [CMD_CLIENT_CONNECT_SERVER] = "CMD_CLIENT_CONNECT_SERVER",
    [CMD_CLIENT_STATE_READY] = "CMD_CLIENT_STATE_READY",
    [CMD_CLIENT_CLOSE_SOC] = "CMD_CLIENT_CLOSE_SOC",
    [CMD_CLIENT_CHECK_ERRORS] = "CMD_CLIENT_CHECK_ERRORS",
};

/** @brief  CMD CL application context structure. */
struct cmd_client_context
{
  enum cmd_client_app_state state;
  struct sockaddr_in ip_addr;
  int socket;
  char cmd_ip_addr[16];
  uint32_t cmd_port;
  bool error;
  volatile bool start;
  volatile bool disconect_req;
  char payload[PAYLOAD_SIZE];
  size_t payload_size;
  uint8_t responce_buff[PAYLOAD_SIZE];
  uint32_t responce_buff_len;
  keepAlive_t keepAlive;
  SemaphoreHandle_t waitResponceSem;
  SemaphoreHandle_t mutexSemaphore;
};

static struct cmd_client_context ctx;

extern void cmdClientReqStartTask( void );

static void _change_state( enum cmd_client_app_state new_state )
{
  if ( new_state < CMD_CLIENT_TOP )
  {
    ctx.state = new_state;
    LOG( PRINT_INFO, "State -> %s", cmd_client_state_name[new_state] );
  }
  else
  {
    LOG( PRINT_ERROR, "Error change state: %d", new_state );
  }
}

/**
 * @brief   CMD Client application CMD_CLIENT_IDLE state.
 */
static void _idle_state( void )
{
  if ( ctx.disconect_req )
  {
    ctx.start = false;
    ctx.error = false;
    ctx.disconect_req = false;
  }

  if ( ctx.start )
  {
    ctx.error = false;
    _change_state( CMD_CLIENT_CREATE_SOC );
  }
  else
  {
    osDelay( 100 );
  }
}

/**
 * @brief   CMD Client application CREATE_SOC state.
 */
static void _create_soc_state( void )
{
  if ( ctx.socket != -1 )
  {
    _change_state( CMD_CLIENT_CLOSE_SOC );
    LOG( PRINT_ERROR, "Cannot create socket" );
    return;
  }

  ctx.socket = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );

  if ( ctx.socket >= 0 )
  {
    _change_state( CMD_CLIENT_CONNECT_SERVER );
  }
  else
  {
    ctx.error = true;
    _change_state( CMD_CLIENT_CLOSE_SOC );
    LOG( PRINT_ERROR, "error create sock" );
  }
}

/**
 * @brief   CMD Client application CMD_CLIENT_CONNECT_SERVER state.
 */
static void _connect_server_state( void )
{
  ctx.ip_addr.sin_addr.s_addr = inet_addr( ctx.cmd_ip_addr );
  ctx.ip_addr.sin_family = AF_INET;
  ctx.ip_addr.sin_port = htons( ctx.cmd_port );

  if ( connect( ctx.socket, (struct sockaddr*) &ctx.ip_addr, sizeof( ctx.ip_addr ) ) < 0 )
  {
    LOG( PRINT_ERROR, "socket connect failed errno=%d ", errno );
    ctx.error = true;
    _change_state( CMD_CLIENT_CLOSE_SOC );
    return;
  }

  LOG( PRINT_INFO, "Conected to server" );
  keepAliveStart( &ctx.keepAlive );
  _change_state( CMD_CLIENT_STATE_READY );
}

static void _connect_ready_state( void )
{
  if ( !ctx.start || ctx.disconect_req )
  {
    _change_state( CMD_CLIENT_CLOSE_SOC );
    return;
  }

  osDelay( 50 );
}

static void _close_soc_state( void )
{
  if ( ctx.socket != -1 )
  {
    close( ctx.socket );
    ctx.socket = -1;
    xQueueReset( (QueueHandle_t) ctx.waitResponceSem );
    xQueueReset( (QueueHandle_t) ctx.mutexSemaphore );
  }
  else
  {
    LOG( PRINT_ERROR, "Socket is not opened %s", __func__ );
  }

  keepAliveStop( &ctx.keepAlive );
  _change_state( CMD_CLIENT_CHECK_ERRORS );
}

static void _check_errors_state( void )
{
  if ( ctx.error )
  {
    LOG( PRINT_INFO, "%s Error detected", __func__ );
  }

  ctx.error = false;
  _change_state( CMD_CLIENT_IDLE );
}

//--------------------------------------------------------------------------------

static void cmd_client_task( void* arg )
{
  while ( 1 )
  {
    switch ( ctx.state )
    {
      case CMD_CLIENT_IDLE:
        _idle_state();
        break;

      case CMD_CLIENT_CREATE_SOC:
        _create_soc_state();
        break;

      case CMD_CLIENT_CONNECT_SERVER:
        _connect_server_state();
        break;

      case CMD_CLIENT_STATE_READY:
        _connect_ready_state();
        break;

      case CMD_CLIENT_CLOSE_SOC:
        _close_soc_state();
        break;

      case CMD_CLIENT_CHECK_ERRORS:
        _check_errors_state();
        break;

      default:
        _change_state( CMD_CLIENT_IDLE );
        break;
    }
  }
}

void cmd_client_ctx_init( void )
{
  _change_state( CMD_CLIENT_IDLE );
  ctx.error = false;
  ctx.start = false;
  ctx.disconect_req = false;
  ctx.socket = -1;
  ctx.payload_size = 0;
  strcpy( ctx.cmd_ip_addr, "192.168.4.1" );
  ctx.cmd_port = 8080;
}

//--------------------------------------------------------------------------------------------------------------------------------------------------------

int cmdClientSend( uint8_t* buffer, uint32_t len )
{
  LOG( PRINT_DEBUG, "%s", __func__ );

  if ( ( ctx.socket == -1 ) || ( ctx.state != CMD_CLIENT_STATE_READY ) )
  {
    LOG( PRINT_INFO, "%s bad module state", __func__ );
    return -1;
  }

  int ret = send( ctx.socket, buffer, len, 0 );

  if ( ret < 0 )
  {
    LOG( PRINT_ERROR, "%s error send msg", __func__ );
  }

  if ( ret > 0 )
  {
    keepAliveAccept( &ctx.keepAlive );
  }

  LOG( PRINT_DEBUG, "%s: %d", __func__, ret );
  return ret;
}

int cmdClientRead( uint8_t* buffer, uint32_t len, uint32_t timeout_ms )
{
  if ( ( buffer == NULL ) || ( len == 0 ) )
  {
    LOG( PRINT_ERROR, "%s Invalid arg", __func__ );
    return ERROR;
  }

  if ( !cmdClientIsConnected() || ( ctx.socket < 0 ) )
  {
    LOG( PRINT_ERROR, "%s Invalid state", __func__ );
    return ERROR;
  }

  int ret = 0;
  fd_set set;

  FD_ZERO( &set );
  FD_SET( ctx.socket, &set );
  struct timeval timeout_time;

  timeout_time.tv_sec = timeout_ms / 1000;
  timeout_time.tv_usec = ( timeout_ms % 1000 ) * 1000;

  ret = select( ctx.socket + 1, &set, NULL, NULL, &timeout_time );

  if ( ret < 0 )
  {
    ctx.error = true;
    _change_state( CMD_CLIENT_CLOSE_SOC );
    LOG( PRINT_ERROR, "error select errno %d", errno );
    return ERROR;
  }
  else if ( ret == 0 )
  {
    return TIMEOUT;
  }

  if ( ctx.start && !ctx.disconect_req && FD_ISSET( ctx.socket, &set ) )
  {
    ret = read( ctx.socket, (char*) buffer, len );
    if ( ret > 0 )
    {
      keepAliveAccept( &ctx.keepAlive );
      return ret;
    }
    else if ( ret == 0 )
    {
      LOG( PRINT_ERROR, "error read 0 %d", ctx.socket );
      return 0;
    }
    else
    {
      ctx.error = true;
      _change_state( CMD_CLIENT_CLOSE_SOC );
      LOG( PRINT_ERROR, "error read errno %d", errno );
      return ERROR;
    }
  }
  else
  {
    LOG( PRINT_ERROR, "%s Bad state after select", __func__ );
    _change_state( CMD_CLIENT_CLOSE_SOC );
    return ERROR;
  }

  return ERROR;
}

static int keepAliveSend( uint8_t* data, uint32_t dataLen )
{
  LOG( PRINT_DEBUG, "%s", __func__ );
  return cmdClientSend( data, dataLen );
}

static void cmdClientErrorKACb( void )
{
  LOG( PRINT_DEBUG, "%s", __func__ );
  cmdClientDisconnect();
}

void cmdClientStartTask( void )
{
  keepAliveInit( &ctx.keepAlive, 2800, keepAliveSend, cmdClientErrorKACb );
  ctx.waitResponceSem = xSemaphoreCreateBinary();
  ctx.mutexSemaphore = xSemaphoreCreateBinary();
  cmd_client_ctx_init();
  xSemaphoreGive( ctx.mutexSemaphore );
  ctx.socket = -1;
  xTaskCreate( cmd_client_task, "cmd_client_task", 4096, NULL, NORMALPRIO, NULL );
  cmdClientReqStartTask();
}

void cmdClientStart( void )
{
  ctx.start = 1;
  ctx.disconect_req = false;
}

void cmdClientDisconnect( void )
{
  ctx.disconect_req = true;
}

int cmdClientIsConnected( void )
{
  return !ctx.disconect_req && ctx.start && ( ctx.state == CMD_CLIENT_STATE_READY );
}

int cmdClientTryConnect( uint32_t timeout )
{
  ctx.disconect_req = 0;
  uint32_t time_now = ST2MS( xTaskGetTickCount() );

  do
  {
    if ( cmdClientIsConnected() )
    {
      return 1;
    }

    vTaskDelay( MS2ST( 10 ) );
  } while ( time_now + timeout < ST2MS( xTaskGetTickCount() ) );

  if ( cmdClientIsConnected() )
  {
    return 1;
  }

  return 0;
}

void cmdClientStop( void )
{
  ctx.start = 0;
}
