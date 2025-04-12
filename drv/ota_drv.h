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
  OTA_DRV_OK,
  OTA_DRV_FAIL,
  OTA_DRV_APP_VALID_AFTER_UPDATE
} ota_drv_status_t;

/* Public functions ----------------------------------------------------------*/

/**
 * @brief   Init ota driver.
 * @return  OTA_DRV_OK if initialization is successful,
 *          OTA_DRV_APP_VALID_AFTER_UPDATE if app is valid after update,
 *          OTA_DRV_FAIL if initialization fails.
 */
ota_drv_status_t OTA_Init( void );

/**
 * @brief   Download image.
 */
bool OTA_Download( const char* url );

/**
 * @brief   Get the OTA initialization status.
 * @return  The status of the OTA initialization.
 */
ota_drv_status_t OTA_GetStatus( void );

/**
 * @brief   Begin OTA update.
 * @param   new_firmware_size - Size of the new firmware.
 * @return  true if successful, false otherwise.
 */
bool OTA_Begin( size_t new_firmware_size );

/**
 * @brief   Write OTA data.
 * @param   buf - Buffer containing the data to write.
 * @param   len - Length of the data to write.
 * @return  true if successful, false otherwise.
 */
bool OTA_Write( const void* buf, size_t len );

/**
 * @brief   End OTA update.
 * @return  true if successful, false otherwise.
 */
bool OTA_End( void );

#endif