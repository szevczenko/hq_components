#include "keepalive.h"

#include "app_config.h"
#include "freertos/FreeRTOS.h"
#include "parse_cmd.h"

#define MODULE_NAME "[Keep] "
#define DEBUG_LVL   PRINT_INFO

#if CONFIG_DEBUG_KEEP_ALIVE
#define LOG( _lvl, ... ) \
  debug_printf( DEBUG_LVL, _lvl, MODULE_NAME __VA_ARGS__ )
#else
#define LOG( PRINT_INFO, ... )
#endif

static keepAlive_t* keepAliveTab[8];
static uint8_t tabSize;

static uint8_t keep_alive_frame[PACKET_SIZE] = { PACKET_SIZE, 0xFF, 0xFF, 0xFF, 0xFF, CMD_REQUEST, PC_KEEP_ALIVE };

void keepAliveInit( keepAlive_t* keep, uint32_t timeout, int ( *send )( uint8_t* data, uint32_t dataLen ), void ( *errorCb )( void ) )
{
  if ( keep == NULL )
  {
    return;
  }

  keep->keepAliveSend = send;
  keep->keepAliveErrorCb = errorCb;
  if ( timeout < KEEP_ALIVE_TIME_TO_NEXT )
  {
    keep->timeout = KEEP_ALIVE_TIME_TO_NEXT;
  }
  else
  {
    keep->timeout = timeout;
  }

  keepAliveAccept( keep );
  keep->keepAliveErrorFlag = 0;
  keepAliveTab[tabSize++] = keep;
}

void keepAliveAccept( keepAlive_t* keep )
{
  if ( keep == NULL )
  {
    return;
  }

  keep->keepAlive = ST2MS( xTaskGetTickCount() ) + keep->timeout;
  keep->keepAliveTry = 0;
  keep->keepAliveErrorFlag = 0;
  LOG( PRINT_DEBUG, "keepalive accept %d timeout %d\n", keep->keepAlive, keep->timeout );
}

int keepAliveCheckError( keepAlive_t* keep )
{
  return keep->keepAliveErrorFlag;
}

static void keepAliveProcess( void* pv )
{
  //LOG(PRINT_INFO, "KeepAliveTask\n");
  keepAlive_t* keep;

  while ( 1 )
  {
    for ( uint8_t i = 0; i < tabSize; i++ )
    {
      keep = keepAliveTab[i];

      if ( keep == NULL )
      {
        continue;
      }

      if ( keep->keepAliveErrorFlag || ( keep->keepAliveActiveFlag == 0 ) )
      {
        continue;
      }

      LOG( PRINT_DEBUG, "keepALive time %d < %d\n", keep->keepAlive, ST2MS( xTaskGetTickCount() ) );
      if ( keep->keepAlive < ST2MS( xTaskGetTickCount() ) )
      {
        if ( keep->keepAliveTry < KEEP_ALIVE_TRY )
        {
          if ( keep->keepAliveSend != NULL )
          {
            LOG( PRINT_DEBUG, "keepAliveSend" );
            keep->keepAliveSend( keep_alive_frame, sizeof( keep_alive_frame ) );
          }

          keep->keepAliveTry++;
          keep->keepAlive = ST2MS( xTaskGetTickCount() ) + keep->timeout;
        }
        else
        {
          keep->keepAliveErrorFlag = 1;
          if ( keep->keepAliveErrorCb != NULL )
          {
            keep->keepAliveErrorCb();
          }
        }
      }
    }

    vTaskDelay( MS2ST( 25 ) );
  }
}

void sendKeepAliveFrame( void )
{
  keepAlive_t* keep;

  for ( uint8_t i = 0; i < tabSize; i++ )
  {
    keep = keepAliveTab[i];
    if ( keep == NULL )
    {
      continue;
    }

    if ( ( keep->keepAliveSend != NULL ) && ( keep->keepAliveActiveFlag == 1 ) )
    {
      keep->keepAliveSend( keep_alive_frame, sizeof( keep_alive_frame ) );
    }
  }
}

void keepAliveStartTask( void )
{
  xTaskCreate( keepAliveProcess, "keepAliveProcess", 2048, NULL, NORMALPRIO, NULL );
}

void keepAliveStart( keepAlive_t* keep )
{
  if ( keep == NULL )
  {
    return;
  }

  keep->keepAliveActiveFlag = 1;
  keep->keepAliveErrorFlag = 0;
  keepAliveAccept( keep );
}

void keepAliveStop( keepAlive_t* keep )
{
  if ( keep == NULL )
  {
    return;
  }

  keep->keepAliveActiveFlag = 0;
}
