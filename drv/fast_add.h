#ifndef _FAST_ADD_H
#define _FAST_ADD_H

#include "app_config.h"

typedef enum
{
  FP_MINUS,
  FP_PLUS,
  FP_PLUS_10,
  FP_MINUS_10,
} fast_process_sign;

typedef struct
{
  uint32_t counter;
  uint32_t delay;
  uint32_t* value;
  uint32_t max, min, sign;
  void ( *func )( uint32_t );
} fast_add_t;

void fastProcessStart( uint32_t* value, uint32_t max, uint32_t min, fast_process_sign sign, void ( *func )( uint32_t ) );
void fastProcessStop( uint32_t* value );
void fastProcessDeInit( void );
void fastProcessStartTask( void );

#endif