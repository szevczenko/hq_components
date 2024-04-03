/**
 *******************************************************************************
 * @file    ota.c
 * @author  Dmytro Shevchenko
 * @brief   OTA source file
 *******************************************************************************
 */

/* Includes ------------------------------------------------------------------*/

#include "ota_drv.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>

#include "app_config.h"
#include "dev_config.h"
#include "esp_crt_bundle.h"
#include "esp_efuse.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_tls.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* Private macros ------------------------------------------------------------*/
#define MODULE_NAME "[OTA] "
#define DEBUG_LVL   PRINT_DEBUG

#if CONFIG_DEBUG_WIFI
#define LOG( _lvl, ... ) \
  debug_printf( DEBUG_LVL, _lvl, MODULE_NAME __VA_ARGS__ )
#else
#define LOG( PRINT_INFO, ... )
#endif

/* Extern variables ----------------------------------------------------------*/
extern const uint8_t server_cert_pem_start[] asm( "_binary_ca_cert_pem_start" );
extern const uint8_t server_cert_pem_end[] asm( "_binary_ca_cert_pem_end" );

/* Private variables ---------------------------------------------------------*/
static char localResponseBuffer[2048];

/* Private functions ---------------------------------------------------------*/

esp_err_t ota_bundle_attach( void* conf )
{
  mbedtls_ssl_config* ssl_conf = (mbedtls_ssl_config*) conf;
  if ( ssl_conf != NULL )
  {
    mbedtls_ssl_conf_authmode( ssl_conf, MBEDTLS_SSL_VERIFY_OPTIONAL );
  }
  return esp_crt_bundle_attach( ssl_conf );
}

esp_err_t _http_event_handler( esp_http_client_event_t* evt )
{
  static char* output_buffer;    // Buffer to store response of http request from event handler
  static int output_len;    // Stores number of bytes read
  switch ( evt->event_id )
  {
    case HTTP_EVENT_ON_HEADER:
      LOG( PRINT_DEBUG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value );
      break;
    case HTTP_EVENT_ON_DATA:
      LOG( PRINT_DEBUG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len );
      if ( !esp_http_client_is_chunked_response( evt->client ) )
      {
        // If user_data buffer is configured, copy the response into the buffer
        int copy_len = 0;
        if ( evt->user_data )
        {
          copy_len = MIN( evt->data_len, ( sizeof( localResponseBuffer ) - output_len ) );
          if ( copy_len )
          {
            memcpy( evt->user_data + output_len, evt->data, copy_len );
          }
        }
        else
        {
          const int buffer_len = esp_http_client_get_content_length( evt->client );
          if ( output_buffer == NULL )
          {
            output_buffer = (char*) malloc( buffer_len );
            output_len = 0;
            if ( output_buffer == NULL )
            {
              LOG( PRINT_ERROR, "Failed to allocate memory for output buffer" );
              return ESP_FAIL;
            }
          }
          copy_len = MIN( evt->data_len, ( buffer_len - output_len ) );
          if ( copy_len )
          {
            memcpy( output_buffer + output_len, evt->data, copy_len );
          }
        }
        output_len += copy_len;
      }
      break;
    case HTTP_EVENT_ON_FINISH:
      LOG( PRINT_DEBUG, "HTTP_EVENT_ON_FINISH" );
      if ( output_buffer != NULL )
      {
        free( output_buffer );
        output_buffer = NULL;
      }
      output_len = 0;
      break;
    case HTTP_EVENT_DISCONNECTED:
      LOG( PRINT_INFO, "HTTP_EVENT_DISCONNECTED" );
      int mbedtls_err = 0;
      esp_err_t err = esp_tls_get_and_clear_last_error( (esp_tls_error_handle_t) evt->data, &mbedtls_err, NULL );
      if ( err != 0 )
      {
        LOG( PRINT_INFO, "Last esp error code: 0x%x", err );
        LOG( PRINT_INFO, "Last mbedtls failure: 0x%x", mbedtls_err );
      }
      if ( output_buffer != NULL )
      {
        free( output_buffer );
        output_buffer = NULL;
      }
      output_len = 0;
      break;
    case HTTP_EVENT_REDIRECT:
      LOG( PRINT_DEBUG, "HTTP_EVENT_REDIRECT" );
      esp_http_client_set_header( evt->client, "From", "user@example.com" );
      esp_http_client_set_header( evt->client, "Accept", "text/html" );
      esp_http_client_set_redirection( evt->client );
      break;

    default:
      break;
  }
  return ESP_OK;
}

static void _ota_event_handler( void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data )
{
  if ( event_base == ESP_HTTPS_OTA_EVENT )
  {
    switch ( event_id )
    {
      case ESP_HTTPS_OTA_START:
        LOG( PRINT_INFO, "OTA started" );
        break;
      case ESP_HTTPS_OTA_CONNECTED:
        LOG( PRINT_INFO, "Connected to server" );
        break;
      case ESP_HTTPS_OTA_GET_IMG_DESC:
        LOG( PRINT_INFO, "Reading Image Description" );
        break;
      case ESP_HTTPS_OTA_VERIFY_CHIP_ID:
        LOG( PRINT_INFO, "Verifying chip id of new image: %d", *(esp_chip_id_t*) event_data );
        break;
      case ESP_HTTPS_OTA_DECRYPT_CB:
        LOG( PRINT_INFO, "Callback to decrypt function" );
        break;
      case ESP_HTTPS_OTA_WRITE_FLASH:
        LOG( PRINT_DEBUG, "Writing to flash: %d written", *(int*) event_data );

        break;
      case ESP_HTTPS_OTA_UPDATE_BOOT_PARTITION:
        LOG( PRINT_INFO, "Boot partition updated. Next Partition: %d", *(esp_partition_subtype_t*) event_data );
        break;
      case ESP_HTTPS_OTA_FINISH:
        LOG( PRINT_INFO, "OTA finish" );
        /*post result*/
        break;
      case ESP_HTTPS_OTA_ABORT:
        LOG( PRINT_INFO, "OTA abort" );
        /*post result*/
        break;
    }
  }
}

static esp_err_t _validate_image_header( esp_app_desc_t* new_app_info )
{
  if ( new_app_info == NULL )
  {
    return ESP_ERR_INVALID_ARG;
  }

  const esp_partition_t* running = esp_ota_get_running_partition();
  esp_app_desc_t running_app_info;
  if ( esp_ota_get_partition_description( running, &running_app_info ) == ESP_OK )
  {
    LOG( PRINT_INFO, "Running firmware version: %s", running_app_info.version );
  }

  // if ( memcmp( new_app_info->version, running_app_info.version, sizeof( new_app_info->version ) ) == 0 )
  // {
  //   LOG( PRINT_WARNING, "Current running version is the same as a new. We will not continue the update." );
  //   return ESP_FAIL;
  // }

  const uint32_t hw_sec_version = esp_efuse_read_secure_version();
  if ( new_app_info->secure_version < hw_sec_version )
  {
    LOG( PRINT_WARNING, "New firmware security version is less than eFuse programmed, %" PRIu32 " < %" PRIu32, new_app_info->secure_version, hw_sec_version );
    return ESP_FAIL;
  }

  return ESP_OK;
}

static esp_err_t _http_client_init_cb( esp_http_client_handle_t http_client )
{
  return ESP_OK;
}

static bool _download_and_update_firmware( const char* url )
{
  esp_err_t ota_finish_err = ESP_OK;
  esp_http_client_config_t config = {
    .url = url,
    // .cert_pem = (char*) server_cert_pem_start,
    .timeout_ms = 3000,
    .keep_alive_enable = true,
    .crt_bundle_attach = ota_bundle_attach,
  };

  esp_https_ota_config_t ota_config = {
    .http_config = &config,
    .http_client_init_cb = _http_client_init_cb,    // Register a callback to be invoked after esp_http_client is initialized
    .partial_http_download = true,
    .max_http_request_size = 8 * 1024,
  };

  esp_https_ota_handle_t https_ota_handle = NULL;
  esp_err_t err = esp_https_ota_begin( &ota_config, &https_ota_handle );
  if ( err != ESP_OK )
  {
    goto ota_end;
  }

  esp_app_desc_t app_desc;
  err = esp_https_ota_get_img_desc( https_ota_handle, &app_desc );
  if ( err != ESP_OK )
  {
    goto ota_end;
  }
  err = _validate_image_header( &app_desc );
  if ( err != ESP_OK )
  {
    goto ota_end;
  }

  int file_size = esp_https_ota_get_image_size( https_ota_handle );
  while ( 1 )
  {
    err = esp_https_ota_perform( https_ota_handle );
    if ( err != ESP_ERR_HTTPS_OTA_IN_PROGRESS )
    {
      break;
    }
    int download = esp_https_ota_get_image_len_read( https_ota_handle );
    LOG( PRINT_DEBUG, "Image bytes read: %d from %d", download, file_size );
  }

  if ( esp_https_ota_is_complete_data_received( https_ota_handle ) != true )
  {
    // the OTA image was not completely received and user can customize the response to this situation.
    LOG( PRINT_ERROR, "Complete data was not received." );
  }
  else
  {
    ota_finish_err = esp_https_ota_finish( https_ota_handle );
    if ( ( err == ESP_OK ) && ( ota_finish_err == ESP_OK ) )
    {
      LOG( PRINT_INFO, "ESP_HTTPS_OTA upgrade successful. Wait rebooting ..." );
      return true;
    }
  }

ota_end:
  esp_https_ota_abort( https_ota_handle );
  LOG( PRINT_ERROR, "ESP_HTTPS_OTA upgrade failed" );
  return false;
}

/* State machine functions -----------------------------------------------------*/

static void _init( void )
{
  esp_event_handler_register( ESP_HTTPS_OTA_EVENT, ESP_EVENT_ANY_ID, &_ota_event_handler, NULL );
  const esp_partition_t* running = esp_ota_get_running_partition();
  esp_ota_img_states_t ota_state;
  if ( esp_ota_get_state_partition( running, &ota_state ) == ESP_OK )
  {
    if ( ota_state == ESP_OTA_IMG_PENDING_VERIFY )
    {
      if ( esp_ota_mark_app_valid_cancel_rollback() == ESP_OK )
      {
        LOG( PRINT_INFO, "App is valid, rollback cancelled successfully" );
      }
      else
      {
        LOG( PRINT_ERROR, "Failed to cancel rollback" );
      }
    }
  }
}

bool OTA_Download( const char* url )
{
  return _download_and_update_firmware( url );
}

void OTA_Init( void )
{
  _init();
}
