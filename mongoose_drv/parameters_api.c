
/**
 *******************************************************************************
 * @file    parameters_api.c
 * @author  Dmytro Shevchenko
 * @brief   Parameters API source code
 *******************************************************************************
 */

/* Includes ------------------------------------------------------------------*/

#include "parameters_api.h"

#include <ctype.h>

#include "dev_config.h"
#include "http_server.h"
#include "parameters.h"

/* Private macros ------------------------------------------------------------*/

#define MODULE_NAME "[Param API] "
#define DEBUG_LVL   PRINT_INFO

#if CONFIG_DEBUG_ERROR_SIEWNIK
#define LOG( _lvl, ... ) \
  debug_printf( DEBUG_LVL, _lvl, MODULE_NAME __VA_ARGS__ )
#else
#define LOG( PRINT_INFO, ... )
#endif

#define API_URI  "/api/parameter/"
#define API_NAME "parameter"

/* Private functions declaration ---------------------------------------------*/

/* Private variables ---------------------------------------------------------*/

static char response_buffer[128];

/* Private functions ---------------------------------------------------------*/

static int str2int( struct mg_str* str )
{
  int i;
  int ret = 0;
  for ( i = 0; i < str->len; i++ )
  {
    if ( isdigit( (unsigned char)str->ptr[i] ) )
    {
      ret = ret * 10 + ( str->ptr[i] - '0' );
    }
    else
    {
      break;
    }
  }
  return ret;
}

static HTTPServerResponse_t _parameters_parse_cb( struct mg_str* uri, struct mg_str* data, HTTPServerMethod_t method )
{
  char buffer[128];
  HTTPServerResponse_t response = { .msg = response_buffer };
  for ( int i = 0; i < PARAM_LAST_VALUE; i++ )
  {
    memset( buffer, 0, sizeof( buffer ) );
    snprintf( buffer, sizeof( buffer ) - 1, "%s%s", API_URI, parameters_getName( i ) );
    struct mg_str parameters_uri = mg_str( buffer );
    if ( mg_match( *uri, parameters_uri, NULL ) )
    {
      switch ( method )
      {
        case HTTP_SERVER_METHOD_GET:
          sprintf( response_buffer, "%ld", parameters_getValue( i ) );
          response.code = 200;
          return response;

        case HTTP_SERVER_METHOD_POST:
          assert( data );
          int value = str2int( data );
          if ( parameters_setValue( i, value ) )
          {
            sprintf( response_buffer, "OK" );
            response.code = 200;
          }
          else
          {
            sprintf( response_buffer, "Fail set value %d", value );
            response.code = 400;
          }
          return response;

        default:
          sprintf( response_buffer, "Method not allowed" );
          response.code = 405;
          return response;
      }
    }
  }
  sprintf( response_buffer, "Parameter not exist" );
  response.code = 400;
  return response;
}

/* Public functions ---------------------------------------------------------*/

void ParametersAPI_Init( void )
{
  HTTPServerApiToken_t token = {
    .api_name = API_NAME,
    .cb = _parameters_parse_cb,
  };

  HTTPServer_AddApiToken( &token );
}
