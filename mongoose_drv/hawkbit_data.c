#include "hawkbit_data.h"

#include <string.h>

#include "app_config.h"
#include "nvs.h"
#include "nvs_flash.h"

/* Private macros ------------------------------------------------------------*/

#define PARTITION_NAME    "dev_config"
#define STORAGE_NAMESPACE "hawkbit_data"

#define MODULE_NAME "[HAWK_DATA] "
#define DEBUG_LVL   PRINT_DEBUG

#if CONFIG_DEBUG_HTTP_HAWKBIT
#define LOG( _lvl, ... ) \
  debug_printf( DEBUG_LVL, _lvl, MODULE_NAME __VA_ARGS__ )
#else
#define LOG( PRINT_INFO, ... )
#endif

static hawkbit_data_t actual_data;

void HawkbitData_Init( void )
{
  // Initialize NVS
  esp_err_t err = ESP_OK;
  // Open NVS handle
  nvs_handle_t my_handle;
  err = nvs_open_from_partition( PARTITION_NAME, STORAGE_NAMESPACE, NVS_READONLY, &my_handle );
  if ( err != ESP_OK )
  {
    // If opening NVS handle fails, set default values
    actual_data.update_status = HAWKBIT_STATUS_FAIL;
    actual_data.action_id = 0;
    strcpy( actual_data.version, "0.0.0" );
    return;
  }

  // Read update_status
  uint32_t update_status;
  err = nvs_get_u32( my_handle, "update_status", &update_status );
  if ( err != ESP_OK )
  {
    actual_data.update_status = HAWKBIT_STATUS_FAIL;
  }
  else
  {
    actual_data.update_status = (update_status_t) update_status;
  }

  // Read action_id
  err = nvs_get_u32( my_handle, "action_id", &actual_data.action_id );
  if ( err != ESP_OK )
  {
    actual_data.action_id = 0;
  }

  // Read version
  size_t required_size = sizeof( actual_data.version );
  err = nvs_get_str( my_handle, "version", actual_data.version, &required_size );
  if ( err != ESP_OK )
  {
    strcpy( actual_data.version, "0.0.0" );
  }

  // Close NVS handle
  nvs_close( my_handle );
}

void HawkbitData_Read( hawkbit_data_t* read_data )
{
  if ( read_data != NULL )
  {
    memcpy( read_data, &actual_data, sizeof( hawkbit_data_t ) );
  }
}

bool HawkbitData_Write( const hawkbit_data_t* save_data )
{
  if ( save_data == NULL )
  {
    return false;
  }

  // Initialize NVS
  esp_err_t err = ESP_OK;
  // Open NVS handle
  nvs_handle_t my_handle;
  err = nvs_open_from_partition( PARTITION_NAME, STORAGE_NAMESPACE, NVS_READWRITE, &my_handle );
  if ( err != ESP_OK )
  {
    return false;
  }

  // Write update_status
  err = nvs_set_u32( my_handle, "update_status", (uint32_t) save_data->update_status );
  if ( err != ESP_OK )
  {
    LOG( PRINT_ERROR, "Failed to write update_status" );
    nvs_close( my_handle );
    return false;
  }

  // Write action_id
  err = nvs_set_u32( my_handle, "action_id", save_data->action_id );
  if ( err != ESP_OK )
  {
    LOG( PRINT_ERROR, "Failed to write action_id" );
    nvs_close( my_handle );
    return false;
  }

  // Write version
  err = nvs_set_str( my_handle, "version", save_data->version );
  if ( err != ESP_OK )
  {
    LOG( PRINT_ERROR, "Failed to write version" );
    nvs_close( my_handle );
    return false;
  }

  // Commit written value.
  err = nvs_commit( my_handle );
  if ( err != ESP_OK )
  {
    LOG( PRINT_ERROR, "Failed to commit NVS changes" );
    nvs_close( my_handle );
    return false;
  }

  // Close NVS handle
  nvs_close( my_handle );

  // Copy save_data to actual_data
  memcpy( &actual_data, save_data, sizeof( hawkbit_data_t ) );

  return true;
}
