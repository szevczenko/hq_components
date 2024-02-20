/**
 *******************************************************************************
 * @file    mongoose_drv.c
 * @author  Dmytro Shevchenko
 * @brief   Mongoose component implementation source file
 *******************************************************************************
 */

/* Includes ------------------------------------------------------------------*/

#include "mongoose_drv.h"

#include "app_config.h"
#include "esp_spiffs.h"
#include "mongoose.h"
#include "net.h"

/* Private macros ------------------------------------------------------------*/
#define MODULE_NAME "[Mon] "
#define DEBUG_LVL   PRINT_DEBUG

#if CONFIG_DEBUG_MQTT_APP
#define LOG( _lvl, ... ) \
  debug_printf( DEBUG_LVL, _lvl, MODULE_NAME __VA_ARGS__ )
#else
#define LOG( PRINT_INFO, ... )
#endif

#define ARRAY_SIZE( _array ) sizeof( _array ) / sizeof( _array[0] )
#define FS_ROOT              "/spiffs"

/* Private functions declaration ---------------------------------------------*/

/* Private variables ---------------------------------------------------------*/

/* Private functions ---------------------------------------------------------*/

static void _task( void* arg )
{
  // Mount filesystem
  esp_vfs_spiffs_conf_t conf = {
    .base_path = FS_ROOT, .max_files = 20, .format_if_mount_failed = true };
  int res = esp_vfs_spiffs_register( &conf );
  MG_INFO( ( "FS %s, %d", conf.base_path, res ) );
  mg_file_printf( &mg_fs_posix, FS_ROOT "/hello.txt", "%s", "hello from ESP" );

  // Connected to WiFi, now start HTTP server
  struct mg_mgr mgr;
  mg_log_set( MG_LL_DEBUG );    // Set log level
  mg_mgr_init( &mgr );
  MG_INFO( ( "Mongoose version : v%s", MG_VERSION ) );
  MG_INFO( ( "Listening on     : %s", HTTP_URL ) );
#if MG_ENABLE_MBEDTLS
  MG_INFO( ( "Listening on     : %s", HTTPS_URL ) );
#endif

  web_init( &mgr );
  for ( ;; )
    mg_mgr_poll( &mgr, 1000 );    // Infinite event loop
}

/* Public functions ---------------------------------------------------------*/

void MongooseDrv_Init( void )
{
  xTaskCreate( _task, "mongoose", 4096, NULL, 13, NULL );
}
