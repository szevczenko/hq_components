/**
 *******************************************************************************
 * @file    dev_config.c
 * @author  Dmytro Shevchenko
 * @brief   Device config
 *******************************************************************************
 */

/* Includes ------------------------------------------------------------------*/
#include "dev_config.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

/* Private macros ------------------------------------------------------------*/

#define PARTITION_NAME     "dev_config"
#define STORAGE_NAMESPACE  "config"
#define DEV_CONFIG_SN_SIZE 32

/* Private types -------------------------------------------------------------*/

typedef struct
{
  char serial_number[DEV_CONFIG_SN_SIZE];
} config_data_t;

/* Private variables ---------------------------------------------------------*/
static config_data_t config_data;
static const char* error_lvl_str[] =
  {
    [PRINT_DEBUG] = "DEBUG: ",
    [PRINT_INFO] = "INFO: ",
    [PRINT_WARNING] = "WARNING: ",
    [PRINT_ERROR] = "ERROR: " };

static SemaphoreHandle_t mutexSemaphore;

static bool _read_data( void )
{
  nvs_handle_t nvs;
  esp_err_t err;
  size_t len;

  err = nvs_open_from_partition( PARTITION_NAME, STORAGE_NAMESPACE, NVS_READONLY, &nvs );
  if ( err != ESP_OK )
  {
    printf( "Error read " STORAGE_NAMESPACE " %d\n\r", err );
    return false;
  }

  memset( config_data.serial_number, 0, sizeof( config_data.serial_number ) );
  err = nvs_get_str( nvs, "SN", config_data.serial_number, &len );

  nvs_close( nvs );
  if ( err != ESP_OK )
  {
    printf( "Error nvs_get_u32 %d\n\r", err );
    return false;
  }
  return true;
}

static bool _save_data( void )
{
  nvs_handle_t nvs;
  esp_err_t err;

  err = nvs_open_from_partition( PARTITION_NAME, STORAGE_NAMESPACE, NVS_READWRITE, &nvs );
  if ( err != ESP_OK )
  {
    printf( "Error read " STORAGE_NAMESPACE " %d\n\r", err );
    return false;
  }

  err = nvs_set_str( nvs, "SN", (const char*) config_data.serial_number );

  if ( err == ESP_OK )
  {
    err = nvs_commit( nvs );
    if ( err != ESP_OK )
    {
      printf( "Error commit " STORAGE_NAMESPACE " %d\n\r", err );
    }
  }

  nvs_close( nvs );
  if ( err != ESP_OK )
  {
    printf( "Error set serial number %d\n\r", err );
    return false;
  }
  return true;
}

void DevConfig_Printf( enum config_print_lvl module_lvl, enum config_print_lvl msg_lvl, const char* format, ... )
{
  if ( module_lvl <= msg_lvl )
  {
    xSemaphoreTake( mutexSemaphore, 250 );
    printf( error_lvl_str[msg_lvl] );
    va_list args;
    va_start( args, format );
    vprintf( format, args );
    va_end( args );
    printf( "\n\r" );
    xSemaphoreGive( mutexSemaphore );
  }
}

static esp_err_t _nvs_part_init( const char* name )
{
#if CONFIG_NVS_ENCRYPTION
  esp_err_t ret = ESP_FAIL;
  const esp_partition_t* key_part = esp_partition_find_first(
    ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS_KEYS, NULL );
  if ( key_part == NULL )
  {
    printf( "ERROR: CONFIG_NVS_ENCRYPTION is enabled, but no partition with subtype nvs_keys found in the partition table." );
    return ret;
  }

  nvs_sec_cfg_t cfg = {};
  ret = nvs_flash_read_security_cfg( key_part, &cfg );
  if ( ret != ESP_OK )
  {
    /* We shall not generate keys here as that must have been done in default NVS partition initialization case */
    printf( "ERROR: Failed to read NVS security cfg: [0x%02X] (%s)", ret, esp_err_to_name( ret ) );
    return ret;
  }

  ret = nvs_flash_secure_init_partition( name, &cfg );
  if ( ret == ESP_OK )
  {
    printf( "NVS partition \"%s\" is encrypted.", name );
  }
  else
  {
    printf( "[CRITICAL ERROR], cannot init %s partition: %s", name, esp_err_to_name( ret ) );
  }
  return ret;
#else
  return nvs_flash_init_partition( name );
#endif
}

void DevConfig_Init( void )
{
  mutexSemaphore = xSemaphoreCreateBinary();
  xSemaphoreGive( mutexSemaphore );
  nvs_flash_init();
  _nvs_part_init( PARTITION_NAME );
  if ( false == _read_data() )
  {
    printf( "[DEVICE CONFIG] Critical error: cannot read device config\n\r" );
  }
}

const char* DevConfig_GetSerialNumber( void )
{
  return (const char*) config_data.serial_number;
}

bool DevConfig_SetSerialNumber( const char* sn )
{
  assert( strlen( sn ) < sizeof( config_data.serial_number ) );
  strcpy( config_data.serial_number, sn );
  _save_data();
  return true;
}
