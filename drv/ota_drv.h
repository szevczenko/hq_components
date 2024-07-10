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
#include <stddef.h>

/* Public types -------------------------------------------------------------*/

typedef enum
{
  OTA_DRIVER_STATE_IDLE,
  OTA_DRIVER_STATE_DOWNLOAD,
  OTA_DRIVER_STATE_ERROR,
  OTA_DRIVER_STATE_DONWLOAD_FINISHED,
  OTA_DERIVER_LAST
} ota_driver_state_t;

/* Public functions ----------------------------------------------------------*/

/**
 * @brief   Init ota driver.
 */
void OTA_Init( void );

/**
 * @brief   Download image.
 */
bool OTA_Download( const char* url );

/**
 * @brief   Get OTA state.
 */
ota_driver_state_t OTA_GetState( void );

/**
 * @brief   Get OTA downloaded percentage.
 */
size_t OTA_GetDownloadPercentage( void );

#endif