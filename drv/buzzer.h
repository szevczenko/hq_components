#ifndef _BUZZER_H_
#define _BUZZER_H_

#define BUZZER_PIN   4
#define BUZZER_ON()  gpio_set_level( BUZZER_PIN, 1 );
#define BUZZER_OFF() gpio_set_level( BUZZER_PIN, 0 );

void buzzer_init( void );
void buzzer_click( void );

#endif