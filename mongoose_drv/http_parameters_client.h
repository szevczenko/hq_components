/**
 *******************************************************************************
 * @file    http_parameters_client.h
 * @author  Dmytro Shevchenko
 * @brief   HTTP parameters client application
 *******************************************************************************
 */

/* Define to prevent recursive inclusion ------------------------------------*/

#ifndef _HTTP_PARAMETERS_H_
#define _HTTP_PARAMETERS_H_

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "error_code.h"
#include "mongoose.h"
#include "parameters.h"

/* Public types --------------------------------------------------------------*/

/* Public functions ----------------------------------------------------------*/

/**
 * @brief   Init HTTP server driver.
 */
void HTTPParamClient_Init( void );

/**
 * @brief   Set U32 value.
 */
error_code_t HTTPParamClient_SetU32Value( parameter_value_t parameter, uint32_t value, uint32_t timeout );

/**
 * @brief   Get U32 value.
 */
error_code_t HTTPParamClient_GetU32Value( parameter_value_t parameter, uint32_t* value, uint32_t timeout );

/**
 * @brief   Set U32 value without waiting respose
 */
error_code_t HTTPParamClient_SetU32ValueDontWait( parameter_value_t parameter, uint32_t value );

/**
 * @brief   Set string value waiting respose
 */
error_code_t HTTPParamClient_SetStrValue( parameter_string_t parameter, const char* value, uint32_t timeout );

/**
 * @brief   Set string value waiting respose
 */
error_code_t HTTPParamClient_GetStrValue( parameter_string_t parameter, char* value, uint32_t value_len, uint32_t timeout );

/**
 * @brief   Send ping msg
 */
error_code_t HTTPParamClient_Ping( void );

#endif