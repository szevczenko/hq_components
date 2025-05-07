/**
 *******************************************************************************
 * @file    hawkbit_process.c
 * @author  Dmytro Shevchenko
 * @brief   HAWKBIT process implementation
 *******************************************************************************
 */

/* Includes ------------------------------------------------------------------*/
#include "hawkbit_process.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>

#include "app_config.h"
#include "dev_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "hawkbit_config.h"
#include "hawkbit_data.h"
#include "hawkbit_parser.h"
#include "mongoose_task.h"
#include "ota_drv.h"

/* Private macros ------------------------------------------------------------*/
#define MODULE_NAME             "[HAWKBIT_P] "
#define DEBUG_LVL               PRINT_INFO
#define MAX_CHUNK_SIZE          ( 8192 * 2 )    // 16 KB
#define MAX_RESPONSE_SIZE       2048
#define POLLING_INTERVAL        ( 5 * 60 * 1000 )    // 5 minutes in milliseconds
#define HTTP_REQUEST_TIMEOUT_MS 10000
#define DOWNLOAD_TIMEOUT_MS     300000
#define DEFAULT_SUCCESS_MSG     "The update was successfully installed."
#define DEFAULT_FAILURE_MSG     "The update failed."
#define DEFAULT_CANCEL_MSG      "The update was canceled by the user."

#if CONFIG_DEBUG_HTTP_HAWKBIT
#define LOG( _lvl, ... ) \
  debug_printf( DEBUG_LVL, _lvl, MODULE_NAME __VA_ARGS__ )
#else
#define LOG( PRINT_INFO, ... )
#endif

/* Data types ----------------------------------------------------------------*/
typedef enum
{
  HAWKBIT_POLL_SERVER,
  HAWKBIT_POST_CONFIG_DATA,
  HAWKBIT_DOWNLOAD_IMAGE,
  HAWKBIT_POST_HAWKBIT_RESULT,
  HAWKBIT_STOP_ACTION_ID,
} hawkbit_event_t;

typedef enum
{
  HAWKBIT_UPDATE_RESULT_NONE,
  HAWKBIT_UPDATE_RESULT_SUCCESS,
  HAWKBIT_UPDATE_RESULT_FAILED
} hawkbit_update_result_t;

typedef struct
{
  QueueHandle_t queue;
  hawkbit_update_result_t update_result;
  char update_result_details[256];
  uint32_t action_id;

  int file_size;
  int downloaded_size;
  uint8_t download_percent;

  char url[256];
  bool use_tls;
  char address[HAWKBIT_CONFIG_STR_SIZE];
  char tenant[HAWKBIT_CONFIG_STR_SIZE];
} hawkbit_context_t;

typedef struct
{
  const char* url;
  const char* ca;
  size_t total_size;
  size_t total_written;
  SemaphoreHandle_t semaphore;
} ota_download_ctx_t;

/* Private variables ---------------------------------------------------------*/
static char response_buffer[MAX_RESPONSE_SIZE];
static int response_code;
static char url_config_data[HAWKBIT_URL_SIZE];
static char url_deployment_base[HAWKBIT_URL_SIZE];
static char url_deployment_feedback[HAWKBIT_URL_SIZE + 64];
static char url_cancel_action[HAWKBIT_URL_SIZE];
static hawkbit_deployment_t hawkbit_deployment;
static TimerHandle_t polling_timer;
static TaskHandle_t hawkbit_task_handle = NULL;

static hawkbit_context_t ctx;
static struct mg_connection* client_conn = NULL;
static SemaphoreHandle_t http_semaphore;
static hawkbit_data_t hawkbit_data;

extern const unsigned char ca_cert_start[] asm( "_binary_ca_cert_pem_start" );
extern const unsigned char ca_cert_end[] asm( "_binary_ca_cert_pem_end" );

/* Function prototypes ------------------------------------------------------*/
static void _send_event( hawkbit_event_t event );
static bool _find_action_id( const char* str, uint32_t* id );
static void _timer_polling_callback( TimerHandle_t timer );
static bool _http_request( const char* url, const char* method, const char* data, const char* accept );
static bool _post_hawkbit_result( const char* result, const char* execution, const char* details, bool is_cancel_action, int cnt );
static bool _download_firmware( const char* url, const char* ca );
static void _handle_poll_server( void );
static void _handle_post_config( void );
static void _handle_download_image( void );
static void _handle_post_result( void );
static void _handle_cancel_action( void );
static void _init_update_status( void );
static void _process_task( void* parameters );
static void _get_host_header( const char* url, char* host_header_buf, size_t buf_size );

/* HTTP Connection Helpers ---------------------------------------------------*/
static void _setup_tls_connection( struct mg_connection* c, const char* url )
{
  if ( mg_url_is_ssl( url ) )
  {
    struct mg_tls_opts opts_ca = {
      .ca = mg_str( (char*) ca_cert_start ),
      .name = mg_url_host( url ),
    };
    mg_tls_init( c, &opts_ca );
  }
}

static char* _get_auth_token( void )
{
  static char token[64];
  HAWKBITConfig_GetString( token, HAWKBIT_CONFIG_VALUE_TOKEN, sizeof( token ) );
  return token;
}

static void _prepare_request_headers( char* request_header, size_t header_size,
                                      const char* url, const char* method,
                                      const char* accept, const char* data,
                                      const char* range )
{
  char host_header[128];
  const char* path = mg_url_uri( url );
  if ( !path || *path == '\0' )
    path = "/";

  _get_host_header( url, host_header, sizeof( host_header ) );
  char* token = _get_auth_token();

  if ( strcmp( method, "GET" ) == 0 || strcmp( method, "HEAD" ) == 0 )
  {
    snprintf( request_header, header_size,
              "%s %s HTTP/1.1\r\n"
              "%s"
              "Authorization: GatewayToken %s\r\n"
              "%s"
              "Accept: %s\r\n\r\n",
              method, path, host_header, token,
              range ? range : "",
              accept ? accept : "application/json" );
  }
  else if ( strcmp( method, "POST" ) == 0 || strcmp( method, "PUT" ) == 0 )
  {
    snprintf( request_header, header_size,
              "%s %s HTTP/1.1\r\n"
              "%s"
              "Authorization: GatewayToken %s\r\n"
              "Content-Type: application/json\r\n"
              "Content-Length: %d\r\n\r\n",
              method, path, host_header, token,
              data ? strlen( data ) : 0 );
  }
}

/* Event handling ------------------------------------------------------------*/
static void _send_event( hawkbit_event_t event )
{
  if ( xQueueSend( ctx.queue, &event, 0 ) != pdPASS )
  {
    LOG( PRINT_ERROR, "Failed to send event %d", event );
  }
}

/* Action ID handling --------------------------------------------------------*/
static bool _find_action_id( const char* str, uint32_t* id )
{
  const char* search_pattern = "deploymentBase/";
  char* found = strstr( str, search_pattern );

  if ( found == NULL )
  {
    return false;
  }

  found += strlen( search_pattern );
  *id = atoi( found );
  return true;
}

/* Timer callback -----------------------------------------------------------*/
static void _timer_polling_callback( TimerHandle_t timer )
{
  _send_event( HAWKBIT_POLL_SERVER );
}

/* Function to generate host header string */
static void _get_host_header( const char* url, char* host_header_buf, size_t buf_size )
{
  struct mg_str host = mg_url_host( url );
  int port = mg_url_port( url );

  if ( mg_url_is_ssl( url ) )
  {
    // For SSL connections (typically port 443), don't include the port
    snprintf( host_header_buf, buf_size, "Host: %.*s\r\n", (int) host.len, host.buf );
  }
  else
  {
    // For non-SSL connections, include the port
    snprintf( host_header_buf, buf_size, "Host: %.*s:%d\r\n", (int) host.len, host.buf, port );
  }
}

/* HTTP request handling -----------------------------------------------------*/
static void _http_event_handler( struct mg_connection* c, int ev, void* ev_data )
{
  struct mg_http_message* hm = (struct mg_http_message*) ev_data;
  const char* url = (const char*) c->fn_data;
  LOG( PRINT_DEBUG, "Event: %d", ev );

  switch ( ev )
  {
    case MG_EV_OPEN:
      _setup_tls_connection( c, url );
      break;

    case MG_EV_HTTP_MSG:
      response_code = mg_http_status( hm );
      LOG( PRINT_DEBUG, "HTTP response: %d %.*s", response_code, (int) hm->body.len, hm->body.buf );

      if ( response_code == 200 )
      {
        size_t copy_len = MIN( sizeof( response_buffer ) - 1, hm->body.len );
        strncpy( response_buffer, hm->body.buf, copy_len );
        response_buffer[copy_len] = '\0';
      }

      c->is_closing = 1;
      xSemaphoreGive( http_semaphore );
      break;

    case MG_EV_ERROR:
      LOG( PRINT_ERROR, "HTTP request failed: %s", (char*) ev_data );
      c->is_closing = 1;
      response_code = -1;
      xSemaphoreGive( http_semaphore );
      break;

    default:
      break;
  }
}

static bool _http_request( const char* url, const char* method, const char* data, const char* accept )
{
  client_conn = mg_http_connect( &mgr, url, _http_event_handler, (void*) url );
  if ( client_conn == NULL )
  {
    LOG( PRINT_ERROR, "Failed to initialize HTTP client" );
    return false;
  }

  char request_header[512] = {};
  _prepare_request_headers( request_header, sizeof( request_header ),
                            url, method, accept, data, NULL );

  LOG( PRINT_DEBUG, "HTTP request: %s", request_header );

  if ( strcmp( method, "GET" ) == 0 )
  {
    mg_printf( client_conn, "%s", request_header );
  }
  else if ( strcmp( method, "POST" ) == 0 || strcmp( method, "PUT" ) == 0 )
  {
    mg_printf( client_conn, "%s%s", request_header, data ? data : "" );
  }

  // Wait for the response
  if ( xSemaphoreTake( http_semaphore, pdMS_TO_TICKS( HTTP_REQUEST_TIMEOUT_MS ) ) != pdTRUE )
  {
    LOG( PRINT_ERROR, "HTTP request timeout" );
    return false;
  }

  return response_code == 200;
}

/* Poll server handling -----------------------------------------------------*/
static const char* _get_poll_address( void )
{
  HAWKBITConfig_GetBool( &ctx.use_tls, HAWKBIT_CONFIG_VALUE_TLS );
  HAWKBITConfig_GetString( ctx.address, HAWKBIT_CONFIG_VALUE_ADDRESS, sizeof( ctx.address ) );
  HAWKBITConfig_GetString( ctx.tenant, HAWKBIT_CONFIG_VALUE_TENANT, sizeof( ctx.tenant ) );

  const char* device_id = DevConfig_GetSerialNumber();
  snprintf( ctx.url, sizeof( ctx.url ), "%s/%s/controller/v1/%s",
            ctx.address, ctx.tenant, device_id );

  return ctx.url;
}

static void _format_feedback_url( bool is_cancel_action )
{
  const char* device_id = DevConfig_GetSerialNumber();

  if ( is_cancel_action )
  {
    snprintf( url_deployment_feedback, sizeof( url_deployment_feedback ) - 1,
              "%s/%s/controller/v1/%s/cancelAction/%" PRIu32 "/feedback",
              ctx.address, ctx.tenant, device_id, ctx.action_id );
  }
  else
  {
    snprintf( url_deployment_feedback, sizeof( url_deployment_feedback ) - 1,
              "%s/deploymentBase/%" PRIu32 "/feedback",
              ctx.url, ctx.action_id );
  }
}

static bool _post_hawkbit_result( const char* result, const char* execution, const char* details, bool is_cancel_action, int cnt )
{
  char post_data[512] = {};

  // Format the JSON payload
  snprintf( post_data, sizeof( post_data ) - 1,
            "{\"status\":{"
            "\"execution\":\"%s\","
            "\"result\":{\"finished\":\"%s\",\"progress\":{\"cnt\":%d,\"of\":5}},"
            "\"code\":200,\"details\":[\"%s\"]},"
            "\"timestamp\":%lld}",
            execution, result, cnt, details, (long long) ( time( NULL ) * 1000 ) );

  // Log the JSON payload for debugging
  LOG( PRINT_DEBUG, "POST JSON Payload: %s", post_data );

  // Format the feedback URL
  _format_feedback_url( is_cancel_action );

  // Send the HTTP POST request
  return _http_request( url_deployment_feedback, "POST", post_data, "application/json" );
}

/* OTA download handling -----------------------------------------------------*/
static void _head_ev_handler( struct mg_connection* c, int ev, void* ev_data )
{
  ota_download_ctx_t* ctx = (ota_download_ctx_t*) c->fn_data;
  struct mg_http_message* hm = (struct mg_http_message*) ev_data;
  LOG( PRINT_DEBUG, "HEAD event: %d", ev );

  switch ( ev )
  {
    case MG_EV_OPEN:
      _setup_tls_connection( c, ctx->url );
      break;

    case MG_EV_HTTP_HDRS:
      // Log received headers for debugging
      for ( size_t i = 0; i < sizeof( hm->headers ) / sizeof( hm->headers[0] ); i++ )
      {
        if ( hm->headers[i].name.len > 0 )
        {
          LOG( PRINT_DEBUG, "Header: %.*s: %.*s",
               (int) hm->headers[i].name.len, hm->headers[i].name.buf,
               (int) hm->headers[i].value.len, hm->headers[i].value.buf );
        }
      }
      struct mg_str* content_length = mg_http_get_header( hm, "Content-Length" );
      if ( content_length && mg_str_to_num( *content_length, 10, &ctx->total_size, sizeof( ctx->total_size ) ) && ctx->total_size > 0 )
      {
        LOG( PRINT_DEBUG, "Content length: %zu bytes", ctx->total_size );
        c->is_closing = 1;
      }
      else
      {
        LOG( PRINT_WARNING, "Invalid or missing Content-Length in HEAD response" );
        ctx->total_size = 0;
      }
      break;

    case MG_EV_ERROR:
      LOG( PRINT_ERROR, "HEAD request error: %s", (char*) ev_data );
      ctx->total_size = 0;
      c->is_closing = 1;
      break;

    case MG_EV_CLOSE:
      LOG( PRINT_DEBUG, "HEAD request closed" );
      xSemaphoreGive( ctx->semaphore );
      break;

    default:
      break;
  }
}

static void _download_ev_handler( struct mg_connection* c, int ev, void* ev_data )
{
  ota_download_ctx_t* ctx = (ota_download_ctx_t*) c->fn_data;
  struct mg_http_message* hm = (struct mg_http_message*) ev_data;
  int status_code = 0;
  LOG( PRINT_DEBUG, "Download event: %d", ev );

  switch ( ev )
  {
    case MG_EV_OPEN:
      _setup_tls_connection( c, ctx->url );
      break;

    case MG_EV_HTTP_MSG:
      status_code = mg_http_status( hm );
      LOG( PRINT_DEBUG, "Download response: %d %.*s", status_code, (int) hm->body.len, hm->body.buf );
      if ( status_code == 200 || status_code == 206 )
      {
        size_t chunk_size = MIN( hm->body.len, MAX_CHUNK_SIZE );
        LOG( PRINT_DEBUG, "Received chunk of size: %zu bytes", chunk_size );

        if ( ctx->total_written == 0 && !OTA_Begin( ctx->total_size ) )
        {
          LOG( PRINT_ERROR, "OTA begin failed" );
          c->is_closing = 1;
          return;
        }

        if ( !OTA_Write( hm->body.buf, chunk_size ) )
        {
          LOG( PRINT_ERROR, "OTA write failed" );
          c->is_closing = 1;
          return;
        }

        ctx->total_written += chunk_size;
        LOG( PRINT_DEBUG, "Total written: %zu bytes", ctx->total_written );

        if ( ctx->total_written >= ctx->total_size )
        {
          if ( !OTA_End() )
          {
            LOG( PRINT_ERROR, "OTA end failed" );
            c->is_closing = 1;
            return;
          }
          LOG( PRINT_INFO, "OTA update successful" );
          c->is_closing = 1;
        }
        else
        {
          char range_header[64];
          char request_header[512];

          snprintf( range_header, sizeof( range_header ), "Range: bytes=%zu-%zu\r\n",
                    ctx->total_written, ctx->total_written + MAX_CHUNK_SIZE - 1 );

          _prepare_request_headers( request_header, sizeof( request_header ),
                                    ctx->url, "GET", "*/*", NULL, range_header );

          mg_printf( c, "%s", request_header );
          LOG( PRINT_DEBUG, "Requesting next chunk with range: %s", range_header );
        }
      }
      else
      {
        LOG( PRINT_ERROR, "Download failed with status: %d", mg_http_status( hm ) );
        c->is_closing = 1;
      }
      break;

    case MG_EV_ERROR:
      LOG( PRINT_ERROR, "Download error: %s", (char*) ev_data );
      c->is_closing = 1;
      break;

    case MG_EV_CLOSE:
      LOG( PRINT_DEBUG, "Connection closed" );
      xSemaphoreGive( ctx->semaphore );
      break;

    default:
      break;
  }
}

static bool _get_file_size( const char* url, size_t* file_size )
{
  ota_download_ctx_t download_ctx = {
    .url = url,
    .ca = NULL,
    .total_size = 0,
    .total_written = 0,
    .semaphore = xSemaphoreCreateBinary() };

  if ( download_ctx.semaphore == NULL )
  {
    LOG( PRINT_ERROR, "Failed to create semaphore" );
    return false;
  }

  struct mg_connection* c = mg_http_connect( &mgr, url, _head_ev_handler, &download_ctx );
  if ( c == NULL )
  {
    LOG( PRINT_ERROR, "Failed to create connection for HEAD request" );
    vSemaphoreDelete( download_ctx.semaphore );
    return false;
  }

  char request_header[512];
  _prepare_request_headers( request_header, sizeof( request_header ),
                            url, "GET", "*/*", NULL, NULL );

  LOG( PRINT_DEBUG, ">%s", request_header );
  mg_printf( c, "%s", request_header );

  bool success = ( xSemaphoreTake( download_ctx.semaphore, pdMS_TO_TICKS( HTTP_REQUEST_TIMEOUT_MS ) ) == pdTRUE && download_ctx.total_size > 0 );

  if ( success )
  {
    *file_size = download_ctx.total_size;
  }

  vSemaphoreDelete( download_ctx.semaphore );
  return success;
}

static bool _download_firmware( const char* url, const char* ca )
{
  size_t file_size = 0;
  if ( !_get_file_size( url, &file_size ) || file_size == 0 )
  {
    LOG( PRINT_ERROR, "Failed to determine file size or file is empty" );
    return false;
  }

  ota_download_ctx_t download_ctx = {
    .url = url,
    .ca = ca,
    .total_size = file_size,
    .total_written = 0,
    .semaphore = xSemaphoreCreateBinary() };

  if ( download_ctx.semaphore == NULL )
  {
    LOG( PRINT_ERROR, "Failed to create semaphore" );
    return false;
  }

  struct mg_connection* c = mg_http_connect( &mgr, url, _download_ev_handler, &download_ctx );
  if ( c == NULL )
  {
    LOG( PRINT_ERROR, "Failed to create connection for download" );
    vSemaphoreDelete( download_ctx.semaphore );
    return false;
  }

  char range_header[64];
  char request_header[512];

  snprintf( range_header, sizeof( range_header ), "Range: bytes=0-%zu\r\n",
            MIN( download_ctx.total_size, MAX_CHUNK_SIZE ) - 1 );

  _prepare_request_headers( request_header, sizeof( request_header ),
                            url, "GET", "*/*", NULL, range_header );

  LOG( PRINT_DEBUG, ">%s", request_header );
  mg_printf( c, "%s", request_header );

  bool success = ( xSemaphoreTake( download_ctx.semaphore, pdMS_TO_TICKS( DOWNLOAD_TIMEOUT_MS ) ) == pdTRUE && download_ctx.total_written == download_ctx.total_size );

  vSemaphoreDelete( download_ctx.semaphore );
  return success;
}

/* Event handlers ------------------------------------------------------------*/
static void _process_deployment_urls( void )
{
  // Handle config data URL if present
  if ( strlen( url_config_data ) > 0 )
  {
    _send_event( HAWKBIT_POST_CONFIG_DATA );
  }

  // Handle deployment base URL if present
  if ( strlen( url_deployment_base ) > 0 )
  {
    // Extract action ID
    if ( !_find_action_id( url_deployment_base, &ctx.action_id ) )
    {
      LOG( PRINT_ERROR, "Failed to find action ID" );
      return;
    }

    // Check if this action was already processed
    if ( ctx.action_id == hawkbit_data.action_id )
    {
      LOG( PRINT_INFO, "Action ID %u already processed", ctx.action_id );
      switch ( hawkbit_data.update_status )
      {
        case HAWKBIT_STATUS_WAIT_REBOOT:
          LOG( PRINT_INFO, "Waiting for reboot" );
          break;

        case HAWKBIT_UPDATE_SUCCESS:
          _post_hawkbit_result( "success", "closed",
                                DEFAULT_SUCCESS_MSG, false, 5 );
          break;

        case HAWKBIT_STATUS_FAIL:
          _post_hawkbit_result( "failure", "closed",
                                DEFAULT_FAILURE_MSG, false, 5 );
          break;

        default:
          LOG( PRINT_WARNING, "Unknown update status: %d", hawkbit_data.update_status );
          break;
      }
    }
    else
    {
      _send_event( HAWKBIT_DOWNLOAD_IMAGE );
    }
  }
}

static void _handle_poll_server( void )
{
  // Restart the polling timer
  xTimerStart( polling_timer, 0 );

  // Get server poll address and send request
  const char* url = _get_poll_address();
  if ( !_http_request( url, "GET", NULL, "application/hal+json" ) )
  {
    LOG( PRINT_ERROR, "HTTP GET request failed" );
    return;
  }

  // Parse URLs from response
  if ( !HAWKBITParser_ParseUrl( response_buffer,
                                url_config_data, sizeof( url_config_data ),
                                url_deployment_base, sizeof( url_deployment_base ),
                                url_cancel_action, sizeof( url_cancel_action ) ) )
  {
    LOG( PRINT_ERROR, "Failed to parse URLs from response" );
    return;
  }

  _process_deployment_urls();

  // Handle cancel action URL if present
  if ( strlen( url_cancel_action ) > 0 )
  {
    LOG( PRINT_INFO, "Cancel action URL found: %s", url_cancel_action );
    _send_event( HAWKBIT_STOP_ACTION_ID );
  }
}

static void _handle_post_config( void )
{
  LOG( PRINT_INFO, "Posting config data: %s", url_config_data );
  /* ToDo: fix this hardcode message */
  const char* post_data = "{\"mode\":\"merge\","
                          "\"data\":{\"VIN\":\"JH4TB2H26CC000001\",\"hwRevision\":\"1\"},"
                          "\"status\":{\"result\":{\"finished\":\"success\"},\"execution\":\"closed\",\"details\":[]}}";

  if ( !_http_request( url_config_data, "PUT", post_data, "application/hal+json" ) )
  {
    LOG( PRINT_ERROR, "HTTP PUT request failed" );
  }
  else
  {
    LOG( PRINT_INFO, "Config data posted successfully" );
  }
}

static bool _process_artifact( hawkbit_artifacts_t* artifact )
{
  // Only process main.bin files
  if ( strcmp( artifact->filename, "main.bin" ) != 0 )
  {
    LOG( PRINT_INFO, "Skipping non-main.bin artifact: %s", artifact->filename );
    return true;
  }

  // Post download start feedback
  LOG( PRINT_DEBUG, "Starting download: %s", artifact->download_http );
  _post_hawkbit_result( "none", "download", "download", false, 1 );

  // Perform download
  bool download_success = _download_firmware( artifact->download_http, NULL );

  if ( !download_success )
  {
    LOG( PRINT_ERROR, "Download failed" );
    _post_hawkbit_result( "none", "download",
                          "Failed download. Try again after polling", false, 1 );
    return false;
  }

  // Post download success feedback
  LOG( PRINT_DEBUG, "Download successful" );
  _post_hawkbit_result( "none", "downloaded", "downloaded", false, 2 );
  _post_hawkbit_result( "none", "proceeding", "install", false, 3 );
  _post_hawkbit_result( "none", "proceeding", "reboot", false, 4 );

  return true;
}

static void _handle_download_image( void )
{
  LOG( PRINT_INFO, "Downloading image: %s", url_deployment_base );

  // Clear deployment info
  memset( &hawkbit_deployment, 0, sizeof( hawkbit_deployment ) );

  // Get deployment details
  if ( !_http_request( url_deployment_base, "GET", NULL, "application/hal+json" ) )
  {
    LOG( PRINT_ERROR, "Failed to get deployment details" );
    return;
  }

  // Parse deployment
  if ( !HAWKBITParse_ParseDeployment( response_buffer, &hawkbit_deployment ) )
  {
    LOG( PRINT_ERROR, "Failed to parse deployment" );
    return;
  }

  // Check for chunks
  if ( hawkbit_deployment.chunkSize == 0 )
  {
    LOG( PRINT_INFO, "No chunks found" );
    return;
  }

  // Stop polling timer during download
  xTimerStop( polling_timer, 0 );

  // Process chunks
  LOG( PRINT_INFO, "Processing %d chunks", hawkbit_deployment.chunkSize );
  for ( int chunk = 0; chunk < hawkbit_deployment.chunkSize; chunk++ )
  {
    bool chunk_success = true;

    // Process artifacts in chunk
    for ( int art = 0; art < hawkbit_deployment.chunk[chunk].artifactsSize; art++ )
    {
      hawkbit_artifacts_t* artifact = &hawkbit_deployment.chunk[chunk].artifacts[art];
      LOG( PRINT_INFO, "Processing chunk %d artifact %d: %s",
           chunk, art, artifact->filename );

      chunk_success = _process_artifact( artifact );
      if ( !chunk_success )
      {
        break;
      }
    }

    // Update hawkbit data if chunk was successful
    if ( chunk_success )
    {
      LOG( PRINT_INFO, "Chunk %d processed successfully", chunk );
      strcpy( hawkbit_data.version, hawkbit_deployment.chunk[chunk].version );
      hawkbit_data.update_status = HAWKBIT_STATUS_WAIT_REBOOT;
      hawkbit_data.action_id = ctx.action_id;

      if ( !HawkbitData_Write( &hawkbit_data ) )
      {
        LOG( PRINT_ERROR, "Failed to write hawkbit data" );
      }
    }
    else
    {
      LOG( PRINT_ERROR, "Chunk %d processing failed", chunk );
      break;
    }
  }

  // Restart polling timer
  xTimerStart( polling_timer, 0 );
}

static void _handle_post_result( void )
{
  bool post_result = false;
  const char* details = NULL;

  // Use action ID from hawkbit data
  ctx.action_id = hawkbit_data.action_id;

  switch ( ctx.update_result )
  {
    case HAWKBIT_UPDATE_RESULT_SUCCESS:
      details = strlen( ctx.update_result_details ) > 0 ?
                  ctx.update_result_details :
                  DEFAULT_SUCCESS_MSG;
      post_result = _post_hawkbit_result( "success", "closed", details, false, 5 );
      break;

    case HAWKBIT_UPDATE_RESULT_FAILED:
      details = strlen( ctx.update_result_details ) > 0 ?
                  ctx.update_result_details :
                  DEFAULT_FAILURE_MSG;
      post_result = _post_hawkbit_result( "failure", "closed", details, false, 5 );
      break;

    default:
      LOG( PRINT_ERROR, "Invalid update result" );
      return;
  }

  // Clear result details
  memset( ctx.update_result_details, 0, sizeof( ctx.update_result_details ) );

  // Reset update result if post was successful
  if ( post_result )
  {
    ctx.update_result = HAWKBIT_UPDATE_RESULT_NONE;
  }
}

static void _handle_cancel_action( void )
{
  if ( !_http_request( url_cancel_action, "GET", NULL, "application/hal+json" ) )
  {
    LOG( PRINT_ERROR, "Failed to get cancel action" );
    return;
  }

  LOG( PRINT_INFO, "Cancel action response: %s", response_buffer );

  int action_id;
  if ( HAWKBITParser_ParseCancelAction( response_buffer, &action_id ) )
  {
    if ( ctx.action_id == (uint32_t) action_id )
    {
      LOG( PRINT_INFO, "Canceling action ID: %u", ctx.action_id );
      ctx.update_result = HAWKBIT_UPDATE_RESULT_FAILED;
      // OTA_Stop(); // Uncomment when implemented
    }
    else
    {
      ctx.action_id = (uint32_t) action_id;
    }

    // Send cancellation feedback
    _post_hawkbit_result( "success", "closed", DEFAULT_CANCEL_MSG, true, 0 );
  }
  else
  {
    LOG( PRINT_ERROR, "Failed to parse cancel action" );
  }
}

/* Status and initialization -------------------------------------------------*/
static void _update_status_from_ota( void )
{
  ota_drv_status_t status = OTA_GetStatus();
  LOG( PRINT_INFO, "OTA status: %d", status );

  if ( status == OTA_DRV_APP_VALID_AFTER_UPDATE )
  {
    // Update was successful
    hawkbit_data.update_status = HAWKBIT_UPDATE_SUCCESS;
    ctx.update_result = HAWKBIT_UPDATE_RESULT_SUCCESS;
    LOG( PRINT_INFO, "Update successful after reboot (Action ID: %lu)", hawkbit_data.action_id );
  }
  else
  {
    // Update failed
    hawkbit_data.update_status = HAWKBIT_STATUS_FAIL;
    ctx.update_result = HAWKBIT_UPDATE_RESULT_FAILED;
    LOG( PRINT_INFO, "Update failed after reboot (Action ID: %lu)", hawkbit_data.action_id );
  }
}

static void _init_update_status( void )
{
  // Initialize update result
  ctx.update_result = HAWKBIT_UPDATE_RESULT_NONE;

  // Read hawkbit data
  HawkbitData_Read( &hawkbit_data );
  LOG( PRINT_INFO, "HAWKBIT data read: Action ID: %lu, Status: %d",
       hawkbit_data.action_id, hawkbit_data.update_status );

  // Check if we're waiting for reboot confirmation
  if ( hawkbit_data.update_status == HAWKBIT_STATUS_WAIT_REBOOT )
  {
    _update_status_from_ota();

    // Save updated status - critical to persist this state
    if ( !HawkbitData_Write( &hawkbit_data ) )
    {
      LOG( PRINT_ERROR, "Failed to write hawkbit data after status update" );
    }

    // Set the action ID to ensure proper reporting
    ctx.action_id = hawkbit_data.action_id;
  }
  else
  {
    LOG( PRINT_INFO, "No pending update confirmation (Status: %d, Action ID: %lu)",
         hawkbit_data.update_status, hawkbit_data.action_id );
  }
}

static void _hawkbit_apply_callback( void )
{
  _send_event( HAWKBIT_POLL_SERVER );
}

static void _process_task( void* parameters )
{
  LOG( PRINT_INFO, "Starting HAWKBIT task" );

  // Initialize and start polling
  _send_event( HAWKBIT_POLL_SERVER );

  while ( 1 )
  {
    hawkbit_event_t event;
    if ( xQueueReceive( ctx.queue, &event, portMAX_DELAY ) == pdPASS )
    {
      switch ( event )
      {
        case HAWKBIT_POLL_SERVER:
          _handle_poll_server();
          break;

        case HAWKBIT_POST_CONFIG_DATA:
          _handle_post_config();
          break;

        case HAWKBIT_DOWNLOAD_IMAGE:
          _handle_download_image();
          break;

        case HAWKBIT_POST_HAWKBIT_RESULT:
          _handle_post_result();
          break;

        case HAWKBIT_STOP_ACTION_ID:
          _handle_cancel_action();
          break;

        default:
          LOG( PRINT_ERROR, "Unknown event: %d", event );
          break;
      }
    }
  }
}

/* Public functions ----------------------------------------------------------*/
void HawkbitProcess_Init( void )
{
  // Initialize components
  HAWKBITConfig_Init();
  HAWKBITConfig_SetCallback( _hawkbit_apply_callback );
  HawkbitData_Init();

  // Create queue for events
  ctx.queue = xQueueCreate( 8, sizeof( hawkbit_event_t ) );
  assert( ctx.queue != NULL );

  // Initialize update status
  _init_update_status();

  // Create polling timer
  polling_timer = xTimerCreate( "HawkbitPoll", pdMS_TO_TICKS( POLLING_INTERVAL ),
                                pdTRUE, NULL, _timer_polling_callback );
  assert( polling_timer != NULL );

  // Create HTTP semaphore
  http_semaphore = xSemaphoreCreateBinary();
  assert( http_semaphore != NULL );

  // Create task
  xTaskCreate( _process_task, "hawkbit_task", 1024 * 6, NULL, 5, &hawkbit_task_handle );
}

void HawkbitProcess_Deinit( void )
{
  // Stop and delete timer
  if ( polling_timer != NULL )
  {
    xTimerStop( polling_timer, 0 );
    xTimerDelete( polling_timer, 0 );
    polling_timer = NULL;
  }

  // Delete queue
  if ( ctx.queue != NULL )
  {
    vQueueDelete( ctx.queue );
    ctx.queue = NULL;
  }

  // Delete task
  if ( hawkbit_task_handle != NULL )
  {
    vTaskDelete( hawkbit_task_handle );
    hawkbit_task_handle = NULL;
  }

  // Close connection
  if ( client_conn != NULL )
  {
    client_conn->is_closing = 1;
    client_conn = NULL;
  }

  // Delete semaphore
  if ( http_semaphore != NULL )
  {
    vSemaphoreDelete( http_semaphore );
    http_semaphore = NULL;
  }

  // Clear context
  memset( &ctx, 0, sizeof( ctx ) );
}
