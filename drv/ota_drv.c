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
#define MODULE_NAME "[OTA Drv] "
#define DEBUG_LVL   PRINT_DEBUG

#if CONFIG_DEBUG_OTA
#define LOG( _lvl, ... ) \
  debug_printf( DEBUG_LVL, _lvl, MODULE_NAME __VA_ARGS__ )
#else
#define LOG( PRINT_INFO, ... )
#endif

// Those empty macros do nothing, but mark places in the code which could
// potentially trigger a watchdog reboot due to the log flash erase operation
#define disable_wdt()
#define enable_wdt()

/* Private variables ----------------------------------------------------------*/

static const esp_partition_t* s_ota_update_partition;
static esp_ota_handle_t s_ota_update_handle;
static bool s_ota_success;
static ota_drv_status_t ota_drv_init_status;

/* Private functions ----------------------------------------------------------*/

static ota_drv_status_t _init( void )
{
  const esp_partition_t* running = esp_ota_get_running_partition();
  if ( running == NULL )
  {
    LOG( PRINT_ERROR, "Fail get running partition" );
    return OTA_DRV_FAIL;
  }
  esp_ota_img_states_t ota_state;
  if ( esp_ota_get_state_partition( running, &ota_state ) == ESP_OK )
  {
    if ( ota_state == ESP_OTA_IMG_PENDING_VERIFY )
    {
      if ( esp_ota_mark_app_valid_cancel_rollback() == ESP_OK )
      {
        LOG( PRINT_INFO, "App is valid, rollback cancelled successfully" );
        return OTA_DRV_APP_VALID_AFTER_UPDATE;
      }
      else
      {
        LOG( PRINT_ERROR, "Failed to cancel rollback" );
        return OTA_DRV_FAIL;
      }
    }
  }
  else
  {
    LOG( PRINT_ERROR, "Fail get state partition" );
    return OTA_DRV_FAIL;
  }
  return OTA_DRV_OK;
}

/* Public functions -----------------------------------------------------*/

ota_drv_status_t OTA_Init( void )
{
  ota_drv_init_status = _init();
  return ota_drv_init_status;
}

ota_drv_status_t OTA_GetStatus( void )
{
  return ota_drv_init_status;
}

bool OTA_Begin( size_t new_firmware_size )
{
  if ( s_ota_update_partition != NULL )
  {
    LOG( PRINT_ERROR, "Update in progress. Call mg_ota_end() ?" );
    return false;
  }
  else
  {
    s_ota_success = false;
    disable_wdt();
    s_ota_update_partition = esp_ota_get_next_update_partition( NULL );
    esp_err_t err = esp_ota_begin( s_ota_update_partition, new_firmware_size,
                                   &s_ota_update_handle );
    enable_wdt();
    LOG( PRINT_DEBUG, "esp_ota_begin(): %d", err );
    s_ota_success = ( err == ESP_OK );
  }
  return s_ota_success;
}

bool OTA_Write( const void* buf, size_t len )
{
  disable_wdt();
  esp_err_t err = esp_ota_write( s_ota_update_handle, buf, len );
  enable_wdt();
  LOG( PRINT_DEBUG, "esp_ota_write(): %d", err );
  s_ota_success = err == ESP_OK;
  return s_ota_success;
}

bool OTA_End( void )
{
  esp_err_t err = esp_ota_end( s_ota_update_handle );
  LOG( PRINT_DEBUG, "esp_ota_end(%p): %d", s_ota_update_handle, err );
  if ( s_ota_success && err == ESP_OK )
  {
    err = esp_ota_set_boot_partition( s_ota_update_partition );
    s_ota_success = ( err == ESP_OK );
  }
  LOG( PRINT_INFO, "Finished ESP32 OTA, success: %d", s_ota_success );
  s_ota_update_partition = NULL;
  return s_ota_success;
}
