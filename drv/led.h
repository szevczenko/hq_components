#ifndef _LED_H_
#define _LED_H_

#include <stdbool.h>

typedef enum
{
  LED_UPPER_RED,
  LED_UPPER_GREEN,
  LED_BOTTOM_RED,
  LED_BOTTOM_GREEN,
  LED_AMOUNT
} LEDs_t;

void set_motor_green_led( bool on_off );
void set_servo_green_led( bool on_off );
void set_motor_red_led( bool on_off );
void set_servo_red_led( bool on_off );

void init_leds( void );

#endif