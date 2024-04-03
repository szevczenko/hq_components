/**
 *******************************************************************************
 * @file    ota.h
 * @author  Dmytro Shevchenko
 * @brief   OTA modules header file
 *******************************************************************************
 */

/* Define to prevent recursive inclusion ------------------------------------*/

#ifndef _OTA_DRV_H
#define _OTA_DRV_H

#include <stdbool.h>

/* Public functions ----------------------------------------------------------*/

/**
 * @brief   Init ota driver.
 */
void OTA_Init( void );

/**
 * @brief   Download image.
 */
bool OTA_Download( const char* url );

#endif