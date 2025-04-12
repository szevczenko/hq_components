/**
 *******************************************************************************
 * @file    hawkbit_data.h
 * @brief   HAWKBIT data structures header file
 *******************************************************************************
 */

#ifndef __HAWKBIT_DATA_H__
#define __HAWKBIT_DATA_H__

#include <stdint.h>
#include <stdbool.h>

/* Public types --------------------------------------------------------------*/

typedef enum
{
  HAWKBIT_UPDATE_SUCCESS,
  HAWKBIT_STATUS_WAIT_REBOOT,
  HAWKBIT_STATUS_FAIL
} update_status_t;

typedef struct
{
  update_status_t update_status;
  uint32_t action_id;
  char version[32];
} hawkbit_data_t;

/* Public functions ----------------------------------------------------------*/

/**
 * @brief   Initialize hawkbit data by reading from NVS.
 */
void HawkbitData_Init( void );

/**
 * @brief   Read the current hawkbit data.
 * @param   read_data - Pointer to the structure where the data will be copied.
 */
void HawkbitData_Read( hawkbit_data_t* read_data );

/**
 * @brief   Write the hawkbit data to NVS.
 * @param   save_data - Pointer to the structure containing the data to be saved.
 * @return  true if the data was successfully saved, false otherwise.
 */
bool HawkbitData_Write( const hawkbit_data_t* save_data );

#endif    // __HAWKBIT_DATA_H__
