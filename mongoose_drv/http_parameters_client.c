/**
 *******************************************************************************
 * @file    http_client.c
 * @author  Dmytro Shevchenko
 * @brief   HTTP parameters client based on mongoose driver
 *******************************************************************************
 */

/* Includes ------------------------------------------------------------------*/

#include <stdint.h>
#include <string.h>

// clang-format off
#include "http_server.h"
#include "http_parameters_client.h"
#include "mongoose.h"
#include "parameters.h"
#include "parse_cmd.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/FreeRTOS.h"
#include "esp_debug_helpers.h"
// clang-format on

#define MODULE_NAME "[HTTP CLI] "
#define DEBUG_LVL   PRINT_INFO

#if CONFIG_DEBUG_ERROR_SIEWNIK
#define LOG( _lvl, ... ) \
  debug_printf( DEBUG_LVL, _lvl, MODULE_NAME __VA_ARGS__ )
#else
#define LOG( PRINT_INFO, ... )
#endif

#define HTTP_TIMEOUT 1500llu;    // Connect timeout in milliseconds
#define HOSTNAME     "http://192.168.4.1:80"

typedef enum
{
  PARAM_TYPE_U32,
  PARAM_TYPE_STRING,
  PARAM_TYPE_LAST
} param_type_t;

typedef struct
{
  parameter_value_t parameter;
  uint32_t value;
} http_u32_request_t;

typedef struct
{
  parameter_string_t parameter;
  char value[PARSE_CMD_MAX_STRING_LEN];
} http_string_request_t;

typedef struct
{
  param_type_t type;
  HTTPServerMethod_t method;

  union
  {
    http_u32_request_t u32;
    http_string_request_t str;
  } data;

  SemaphoreHandle_t semaphore;
  uint32_t code;
  bool wait_response;
} http_request_t;

static char request_url[256];
static QueueHandle_t request_queue = NULL;
static SemaphoreHandle_t mutex;
static struct mg_mgr mgr;    // Event manager
static bool done;

static const char* _get_param_name( http_request_t* request )
{
  const char* param_name = NULL;
  if ( request->type == PARAM_TYPE_U32 )
  {
    param_name = parameters_getName( request->data.u32.parameter );
  }
  else if ( request->type == PARAM_TYPE_STRING )
  {
    param_name = parameters_getStringName( request->data.str.parameter );
  }
  return param_name;
}

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

static void _set_response( http_request_t* request, uint32_t code )
{
  assert( request );
  request->code = code;
}

static error_code_t _send_request( http_request_t* request, bool wait )
{
  error_code_t result = ERROR_CODE_QUEUE_IS_FULL;
  http_request_t* mg_request = request;
  request->wait_response = wait;
  if ( wait )
  {
    request->semaphore = xSemaphoreCreateBinary();
    assert( request->semaphore );
  }
  else
  {
    request->semaphore = NULL;
    mg_request = malloc( sizeof( http_request_t ) );
    memcpy( mg_request, request, sizeof( http_request_t ) );
  }

  if ( xQueueSend( request_queue, (void*) &mg_request, (TickType_t) 10 ) == pdPASS )
  {
    if ( wait )
    {
      assert( pdTRUE == xSemaphoreTake( request->semaphore, portMAX_DELAY ) );
    }
    result = request->code == 200 ? ERROR_CODE_OK : ERROR_CODE_FAIL;
  }
  else
  {
    free( mg_request );
  }

  if ( wait )
  {
    vSemaphoreDelete( request->semaphore );
  }
  return result;
}

static bool _post_message( struct mg_connection* c, http_request_t* request )
{
  // Connected to server. Extract host name from URL
  struct mg_str host = mg_url_host( request_url );

  if ( mg_url_is_ssl( request_url ) )
  {
    struct mg_tls_opts opts = { .ca = mg_unpacked( "/certs/ca.pem" ),
                                .name = mg_url_host( request_url ) };
    mg_tls_init( c, &opts );
  }

  // Send request
  int content_length = 0;
  static char s_post_data[PARSE_CMD_MAX_STRING_LEN];
  memset( s_post_data, 0, sizeof( s_post_data ) );

  if ( request->method == HTTP_SERVER_METHOD_POST )
  {
    if ( request->type == PARAM_TYPE_U32 )
    {
      content_length = sprintf( s_post_data, "%ld", request->data.u32.value );
    }
    else if ( request->type == PARAM_TYPE_STRING )
    {
      content_length = sprintf( s_post_data, "%s", request->data.str.value );
    }
  }

  mg_printf( c,
             "%s %s HTTP/1.0\r\n"
             "Host: %.*s\r\n"
             "Content-Type: octet-stream\r\n"
             "Content-Length: %d\r\n"
             "\r\n",
             request->method == HTTP_SERVER_METHOD_POST ? "POST" : "GET",
             mg_url_uri( request_url ), (int) host.len,
             host.ptr, content_length );
  return mg_send( c, s_post_data, content_length );
}

// Print HTTP response and signal that we're done

#define FD( c_ ) ( (MG_SOCKET_TYPE) (size_t) ( c_ )->fd )

static void fn( struct mg_connection* c, int ev, void* ev_data )
{
  http_request_t* request = (http_request_t*) c->fn_data;
  LOG( PRINT_DEBUG, "EV %d", ev );
  if ( ev == MG_EV_OPEN )
  {
    // Connection created. Store connect expiration time in c->data
    *(uint64_t*) c->data = mg_millis() + HTTP_TIMEOUT;
  }
  else if ( ev == MG_EV_POLL )
  {
    if ( ( mg_millis() > *(uint64_t*) c->data ) && ( c->is_connecting || c->is_resolving ) )
    {
      _set_response( request, 504 );
      mg_error( c, "Connect timeout" );
    }
  }
  else if ( ev == MG_EV_CONNECT )
  {
    _post_message( c, request );
  }
  else if ( ev == MG_EV_HTTP_MSG )
  {
    // Response is received. Print it
    struct mg_http_message* hm = (struct mg_http_message*) ev_data;
    int code = mg_http_status( hm );
    if ( code == 200 && request->method == HTTP_SERVER_METHOD_GET )
    {
      if ( request->type == PARAM_TYPE_U32 )
      {
        int value = str2int( &hm->body );
        request->data.u32.value = value;
        assert( parameters_setValue( request->data.u32.parameter, value ) );
      }
      else if ( request->type == PARAM_TYPE_STRING )
      {
        assert( hm->body.len < sizeof( request->data.str.value ) );
        memcpy( request->data.str.value, hm->body.ptr, hm->body.len );
        assert( parameters_setString( request->data.str.parameter, request->data.str.value ) );
      }
    }

    _set_response( request, code );
    if ( code != 200 )
    {
      LOG( PRINT_ERROR, "code %d, %.*s", code, hm->body.len, hm->body.ptr );
    }
    c->is_draining = 1;    // Tell mongoose to close this connection
    done = true;
  }
  else if ( ev == MG_EV_ERROR )
  {
    _set_response( request, 500 );
  }
  else if ( ev == MG_EV_CLOSE )
  {
    if ( request->wait_response )
    {
      xSemaphoreGive( request->semaphore );
    }
    else
    {
      free( request );
    }
    xSemaphoreGive( mutex );
    done = true;
  }
}

static void _set_request_url( http_request_t* request )
{
  const char* param_name = _get_param_name( request );
  if ( request->type == PARAM_TYPE_U32 )
  {
    snprintf( request_url, sizeof( request_url ) - 1, "%s/api/parameter_u32/%s", HOSTNAME, param_name );
  }
  else if ( request->type == PARAM_TYPE_STRING )
  {
    snprintf( request_url, sizeof( request_url ) - 1, "%s/api/parameter_str/%s", HOSTNAME, param_name );
  }
  else
  {
    assert( 0 );
  }
}

static void _task( void* argv )
{
  // Create client connection
  http_request_t* request;
  mutex = xSemaphoreCreateBinary();
  xSemaphoreGive( mutex );
  while ( true )
  {
    if ( xQueueReceive( request_queue, &request, portMAX_DELAY ) == pdTRUE )
    {
      _set_request_url( request );

      done = false;
      struct mg_connection* c = mg_http_connect( &mgr, request_url, fn, (void*) request );
      assert( c );
      while ( !done )
      {
        mg_mgr_poll( &mgr, 50 );
      }
      xSemaphoreTake( mutex, portMAX_DELAY );
    }
  }
}

void HTTPParamClient_Init( void )
{
  mg_log_set( MG_LL_INFO );    // Set to 0 to disable debug
  mg_mgr_init( &mgr );    // Initialise event manager
  request_queue = xQueueCreate( 16, sizeof( http_request_t* ) );
  assert( request_queue );
  xTaskCreate( _task, "mongoose", 4096, NULL, NORMALPRIO + 1, NULL );
}

error_code_t HTTPParamClient_SetU32Value( parameter_value_t parameter, uint32_t value, uint32_t timeout )
{
  assert( parameters_setValue( parameter, value ) );
  http_request_t request = {
    .type = PARAM_TYPE_U32,
    .data.u32.parameter = parameter,
    .data.u32.value = value,
    .method = HTTP_SERVER_METHOD_POST,
  };
  return _send_request( &request, true );
}

error_code_t HTTPParamClient_GetU32Value( parameter_value_t parameter, uint32_t* value, uint32_t timeout )
{
  http_request_t request = {
    .type = PARAM_TYPE_U32,
    .data.u32.parameter = parameter,
    .method = HTTP_SERVER_METHOD_GET,
  };

  error_code_t result = _send_request( &request, true );
  if ( result && value != NULL )
  {
    *value = request.data.u32.value;
  }
  return result;
}

error_code_t HTTPParamClient_SetU32ValueDontWait( parameter_value_t parameter, uint32_t value )
{
  assert( parameters_setValue( parameter, value ) );
  http_request_t request = {
    .type = PARAM_TYPE_U32,
    .data.u32.parameter = parameter,
    .data.u32.value = value,
    .method = HTTP_SERVER_METHOD_POST,
  };
  return _send_request( &request, false );
}

error_code_t HTTPParamClient_SetStrValue( parameter_string_t parameter, const char* value, uint32_t timeout )
{
  http_request_t request = {
    .type = PARAM_TYPE_STRING,
    .data.str.parameter = parameter,
    .method = HTTP_SERVER_METHOD_POST,
  };
  strncpy( request.data.str.value, value, sizeof( request.data.str.value ) );
  return _send_request( &request, true );
}

error_code_t HTTPParamClient_GetStrValue( parameter_string_t parameter, char* value, uint32_t value_len, uint32_t timeout )
{
  http_request_t request = {
    .type = PARAM_TYPE_STRING,
    .data.str.parameter = parameter,
    .method = HTTP_SERVER_METHOD_GET,
  };

  error_code_t result = _send_request( &request, true );
  if ( result && value != NULL )
  {
    strncpy( value, request.data.str.value, value_len );
  }
  return result;
}
