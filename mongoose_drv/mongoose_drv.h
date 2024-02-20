/**
 *******************************************************************************
 * @file    mongoose_drv.h
 * @author  Dmytro Shevchenko
 * @brief   Mongoose component implementation header file
 *******************************************************************************
 */

/* Define to prevent recursive inclusion ------------------------------------*/

#ifndef _MONGOOSE_DRV_H_
#define _MONGOOSE_DRV_H_

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

/* Public types --------------------------------------------------------------*/

/* Public functions ----------------------------------------------------------*/

/**
 * @brief   Init mongoose driver.
 */
void MongooseDrv_Init( void );

#endif