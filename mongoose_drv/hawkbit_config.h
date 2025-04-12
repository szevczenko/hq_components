/**
 *******************************************************************************
 * @file    hawkbit_config.h
 * @author  Dmytro Shevchenko
 * @brief   HAWKBIT modules configuration header file
 *******************************************************************************
 */

/* Define to prevent recursive inclusion ------------------------------------*/

#ifndef _HAWKBIT_CONFIG_H
#define _HAWKBIT_CONFIG_H

#include <stdbool.h>
#include <stddef.h>

#include "app_events.h"

/* Public macros -------------------------------------------------------------*/

#define HAWKBIT_CONFIG_STR_SIZE 64

/* Public types --------------------------------------------------------------*/

typedef enum
{
  HAWKBIT_CONFIG_VALUE_ADDRESS,
  HAWKBIT_CONFIG_VALUE_TLS,
  HAWKBIT_CONFIG_VALUE_TENANT,
  HAWKBIT_CONFIG_VALUE_POLLING_TIME,
  HAWKBIT_CONFIG_VALUE_TOKEN,
  HAWKBIT_CONFIG_VALUE_LAST
} hawkbit_config_value_t;

typedef void ( *hawkbit_apply_config_cb )( void );

/* Public functions ----------------------------------------------------------*/

/**
 * @brief   Init hawkbit config.
 */
void HAWKBITConfig_Init( void );

bool HAWKBITConfig_SetInt( int value, hawkbit_config_value_t config_value );
bool HAWKBITConfig_SetBool( bool value, hawkbit_config_value_t config_value );
bool HAWKBITConfig_SetString( const char* string, hawkbit_config_value_t config_value );
bool HAWKBITConfig_GetInt( int* value, hawkbit_config_value_t config_value );
bool HAWKBITConfig_GetBool( bool* value, hawkbit_config_value_t config_value );
bool HAWKBITConfig_GetString( char* string, hawkbit_config_value_t config_value, size_t string_len );
bool HAWKBITConfig_Save( void );
void HAWKBITConfig_SetCallback( hawkbit_apply_config_cb cb );

#endif