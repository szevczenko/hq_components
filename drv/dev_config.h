/**
 *******************************************************************************
 * @file    dev_config.h
 * @author  Dmytro Shevchenko
 * @brief   Device configuration
 *******************************************************************************
 */

/* Define to prevent recursive inclusion ------------------------------------*/

#ifndef _DEV_CONFIG_H
#define _DEV_CONFIG_H

#include <stddef.h>
#include <stdint.h>

/* Public types ----------------------------------------------------------*/
enum config_print_lvl
{
  PRINT_DEBUG,
  PRINT_INFO,
  PRINT_WARNING,
  PRINT_ERROR,
  PRINT_TOP,
};

/* Public functions ----------------------------------------------------------*/

/**
 * @brief   Init ota config.
 */
void DevConfig_Init( void );

/**
 * @brief   Get serial number.
 */
uint32_t DevConfig_GetSerialNumber( void );

/**
 * @brief   Print data.
 */
void DevConfig_Printf( enum config_print_lvl module_lvl, enum config_print_lvl msg_lvl, const char* format, ... );

#endif