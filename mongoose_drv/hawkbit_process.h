/**
 *******************************************************************************
 * @file    hawkbit_process.h
 * @author  Dmytro Shevchenko
 * @brief   Hawkbit modules header file
 *******************************************************************************
 */

/* Define to prevent recursive inclusion ------------------------------------*/

#ifndef _HAWKBIT_PROCESS_H
#define _HAWKBIT_PROCESS_H

#include <stdbool.h>

#include "app_events.h"

/* Public functions ----------------------------------------------------------*/

/**
 * @brief   Init hawkbit task.
 */
void HawkbitProcess_Init( void );

/**
 * @brief   Sends event to hawkbit task.
 */
void HawkbitProcess_PostMsg( app_event_t* event );

/**
 * @brief   Deinit hawkbit task.
 */
void HawkbitProcess_Deinit( void );

#endif