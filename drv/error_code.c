/**
 *******************************************************************************
 * @file    error_code.c
 * @author  Dmytro Shevchenko
 * @brief   Error code
 *******************************************************************************
 */

/* Includes ------------------------------------------------------------------*/

#include "error_code.h"

#include <string.h>

#include "app_config.h"

/* Private macros ------------------------------------------------------------*/
#define MODULE_NAME "[ERR Code] "
#define DEBUG_LVL   PRINT_DEBUG

#if CONFIG_DEBUG_JSON
#define LOG( _lvl, ... ) \
  debug_printf( DEBUG_LVL, _lvl, MODULE_NAME __VA_ARGS__ )
#else
#define LOG( PRINT_INFO, ... )
#endif

/* Private variables ---------------------------------------------------------*/
const char* error_codes[ERROR_CODE_LAST] =
  {
    [ERROR_CODE_OK] = "OK",
    [ERROR_CODE_OK_NO_ACK] = "NO ACK",
    [ERROR_CODE_FAIL] = "FAIL",
    [ERROR_CODE_ERROR_PARSING] = "ERROR PARSING",
    [ERROR_CODE_UNKNOWN_MQTT_TOPIC_TYPE] = "UNKNOWN_MQTT_TOPIC_TYPE",
};

/* Public functions ---------------------------------------------------------*/

const char* ErrorCode_GetStr( error_code_t code )
{
  if ( code < ERROR_CODE_LAST )
  {
    return error_codes[code];
  }
  return "UNKNOWN";
}
