#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

#ifndef _KEEP_ALIVE_H
#define _KEEP_ALIVE_H

#define KEEP_ALIVE_TIMEOUT      2000
#define KEEP_ALIVE_TRY          2
#define KEEP_ALIVE_TIME_TO_NEXT 100

typedef struct
{
  uint32_t keepAlive;
  uint32_t timeout;
  uint8_t keepAliveTry;
  bool keepAliveErrorFlag;
  bool keepAliveActiveFlag;
  int ( *keepAliveSend )( uint8_t* data, uint32_t dataLen );
  void ( *keepAliveErrorCb )( void );
} keepAlive_t;

void keepAliveInit( keepAlive_t* keep, uint32_t timeout, int ( *send )( uint8_t* data, uint32_t dataLen ), void ( *errorCb )( void ) );
void keepAliveAccept( keepAlive_t* keep );
int keepAliveCheckError( keepAlive_t* keep );
void keepAliveStartTask( void );
void keepAliveStart( keepAlive_t* keep );
void keepAliveStop( keepAlive_t* keep );
void sendKeepAliveFrame( void );

#endif