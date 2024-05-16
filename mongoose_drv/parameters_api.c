
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
#include "parse_cmd.h"

/* Private macros ------------------------------------------------------------*/

#define MODULE_NAME "[Param API] "
#define DEBUG_LVL   PRINT_INFO

#if 1
#define LOG( _lvl, ... ) \
  debug_printf( DEBUG_LVL, _lvl, MODULE_NAME __VA_ARGS__ )
#else
#define LOG( PRINT_INFO, ... )
#endif

#define API_U32_URI  "/api/parameter_u32/"
#define API_U32_NAME "parameter_u32"
#define API_STR_URI  "/api/parameter_str/"
#define API_STR_NAME "parameter_str"

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
    if ( isdigit( (unsigned char) str->ptr[i] ) )
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
    snprintf( buffer, sizeof( buffer ) - 1, "%s%s", API_U32_URI, parameters_getName( i ) );
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
  LOG( PRINT_INFO, "%s %d Parameter not exist %*s", __func__, uri->len, uri->len, uri->ptr );
  sprintf( response_buffer, "Parameter not exist" );
  response.code = 400;
  return response;
}

static HTTPServerResponse_t _parameters_str_parse_cb( struct mg_str* uri, struct mg_str* data, HTTPServerMethod_t method )
{
  char buffer[128];
  HTTPServerResponse_t response = { .msg = response_buffer };
  for ( int i = 0; i < PARAM_STR_LAST_VALUE; i++ )
  {
    memset( buffer, 0, sizeof( buffer ) );
    snprintf( buffer, sizeof( buffer ) - 1, "%s%s", API_STR_URI, parameters_getStringName( i ) );
    struct mg_str parameters_uri = mg_str( buffer );
    if ( mg_match( *uri, parameters_uri, NULL ) )
    {
      switch ( method )
      {
        case HTTP_SERVER_METHOD_GET:
          assert( parameters_getString( i, response_buffer, sizeof( response_buffer ) ) );
          response.code = 200;
          return response;

        case HTTP_SERVER_METHOD_POST:
          assert( data );
          assert( data->len < PARSE_CMD_MAX_STRING_LEN );
          char str[PARSE_CMD_MAX_STRING_LEN] = {};
          strncpy( str, data->ptr, data->len );
          if ( parameters_setString( i, str ) )
          {
            sprintf( response_buffer, "OK" );
            response.code = 200;
          }
          else
          {
            sprintf( response_buffer, "Fail set value (%s)", str );
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
  LOG( PRINT_INFO, "%s %d Parameter not exist %*s", __func__, uri->len, uri->len, uri->ptr );
  sprintf( response_buffer, "Parameter not exist %.*s", uri->len, uri->ptr );
  response.code = 400;
  return response;
}

/* Public functions ---------------------------------------------------------*/

void ParametersAPI_Init( void )
{
  HTTPServerApiToken_t token = {
    .api_name = API_U32_NAME,
    .cb = _parameters_parse_cb,
  };

  HTTPServerApiToken_t token_str = {
    .api_name = API_STR_NAME,
    .cb = _parameters_str_parse_cb,
  };

  HTTPServer_AddApiToken( &token );
  HTTPServer_AddApiToken( &token_str );
}
