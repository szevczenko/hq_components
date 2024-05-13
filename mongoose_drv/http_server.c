
/**
 *******************************************************************************
 * @file    http_server.c
 * @author  Dmytro Shevchenko
 * @brief   HTTP Server
 *******************************************************************************
 */

/* Includes ------------------------------------------------------------------*/

#include "http_server.h"

#include "app_config.h"
#include "esp_system.h"
#include "mongoose.h"

/* Private macros ------------------------------------------------------------*/
#define MODULE_NAME "[HTTP SERV] "
#define DEBUG_LVL   PRINT_INFO

#if CONFIG_DEBUG_ERROR_SIEWNIK
#define LOG( _lvl, ... ) \
  debug_printf( DEBUG_LVL, _lvl, MODULE_NAME __VA_ARGS__ )
#else
#define LOG( PRINT_INFO, ... )
#endif

#ifndef HTTP_URL
#define HTTP_URL "http://0.0.0.0:8000"
#endif

#define ARRAY_SIZE( _array ) sizeof( _array ) / sizeof( _array[0] )
#define CONNECTION_TIMEOUT   5000

static HTTPServerApiToken_t tokens[16];
static TickType_t last_msg_time;
static uint32_t tokens_size;
static const char* method_names[] = {
  [HTTP_SERVER_METHOD_GET] = "GET",
  [HTTP_SERVER_METHOD_PUT] = "PUT",
  [HTTP_SERVER_METHOD_POST] = "POST",
  [HTTP_SERVER_METHOD_DELETE] = "DELETE",
  [HTTP_SERVER_METHOD_PATCH] = "PATCH",
  [HTTP_SERVER_METHOD_UNHALLOWED] = "UNHALLOWED" };

/* Private functions declaration ---------------------------------------------*/

/* Private variables ---------------------------------------------------------*/

/* Private functions ---------------------------------------------------------*/

static HTTPServerMethod_t _get_method( struct mg_str* name )
{
  for ( HTTPServerMethod_t i = 0; i < HTTP_SERVER_METHOD_LAST; i++ )
  {
    if ( mg_vcasecmp( name, method_names[i] ) == 0 )
    {
      return i;
    }
  }
  return HTTP_SERVER_METHOD_UNHALLOWED;
}

static void fn( struct mg_connection* c, int ev, void* ev_data )
{
  if ( ev == MG_EV_HTTP_MSG )
  {
    last_msg_time = xTaskGetTickCount();
    struct mg_http_message* hm = (struct mg_http_message*) ev_data;
    if ( mg_http_match_uri( hm, "/api/#" ) )
    {
      for ( uint32_t i = 0; i < tokens_size; i++ )
      {
        char buffer[128] = {};
        mg_snprintf( buffer, sizeof( buffer ), "/api/%s#", tokens[i].api_name );

        if ( mg_http_match_uri( hm, buffer ) )
        {
          HTTPServerMethod_t method = _get_method( &hm->method );
          HTTPServerResponse_t response = tokens[i].cb( &hm->uri, &hm->body, method );
          mg_http_reply( c, response.code, response.headers, response.msg );
          return;
        }
      }
    }
    printf( "Warning: Request not implemented.\n\rURI %.*s\n\r BODY %.*s\n\r", (int) hm->uri.len, hm->uri.ptr,
            (int) hm->body.len, hm->body.ptr );
    mg_http_reply( c, 400, "", "FAIL" );
  }
}

static void _task( void* argv )
{
  LOG( PRINT_INFO, "Init http server" );
  struct mg_mgr mgr;
  mg_mgr_init( &mgr );    // Init manager
  mg_log_set( MG_LL_INFO );    // Set log level
  mg_http_listen( &mgr, HTTP_URL, fn, &mgr );    // Setup listener
  for ( ;; )
    mg_mgr_poll( &mgr, 1000 );    // Event loop
  mg_mgr_free( &mgr );    // Cleanup
}

/* Public functions ---------------------------------------------------------*/

void HTTPServer_Init( void )
{
  xTaskCreate( _task, "mongoose", 8096, NULL, 13, NULL );
}

void HTTPServer_AddApiToken( HTTPServerApiToken_t* token )
{
  assert( tokens_size < ARRAY_SIZE( tokens ) );
  memcpy( &tokens[tokens_size], token, sizeof( tokens[tokens_size] ) );
  tokens_size++;
}

bool HTTPServer_IsClientConnected( void )
{
  if ( last_msg_time != 0 )
  {
    TickType_t diff = xTaskGetTickCount() - last_msg_time;
    if ( ST2MS( diff ) < CONNECTION_TIMEOUT )
    {
      return true;
    }
  }
  return false;
}
