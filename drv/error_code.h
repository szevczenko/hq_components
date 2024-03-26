/**
 *******************************************************************************
 * @file    error_code.h
 * @author  Dmytro Shevchenko
 * @brief   JSON parser
 *******************************************************************************
 */

/* Define to prevent recursive inclusion ------------------------------------*/

#ifndef _ERROR_CODE_H_
#define _ERROR_CODE_H_

#include <stdbool.h>
#include <stddef.h>

/* Public macro --------------------------------------------------------------*/

/* Public types --------------------------------------------------------------*/
typedef enum
{
  ERROR_CODE_OK,
  ERROR_CODE_OK_NO_ACK,
  ERROR_CODE_FAIL,
  ERROR_CODE_TIMEOUT,
  ERROR_CODE_ERROR_PARSING,
  ERROR_CODE_UNKNOWN_MQTT_TOPIC_TYPE,
  ERROR_CODE_LAST
} error_code_t;

/* Public functions ----------------------------------------------------------*/

const char* ErrorCode_GetStr( error_code_t code );
#endif
