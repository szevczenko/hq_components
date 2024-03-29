#ifndef MENU_DRV_H
#define MENU_DRV_H
#include <stdint.h>

#include "oled.h"

#define LINE_HEIGHT      11
#define MENU_HEIGHT      16
#define MAX_LINE         ( SSD1306_HEIGHT - MENU_HEIGHT ) / LINE_HEIGHT
#define NULL_ERROR_MSG() LOG( PRINT_ERROR, "Error menu pointer list is NULL (%s)\n\r", __func__ );

#define LAST_BUTTON_UP   true
#define LAST_BUTTON_DOWN false

typedef enum
{
  T_ARG_TYPE_BOOL,
  T_ARG_TYPE_VALUE,
  T_ARG_TYPE_MENU,
  T_ARG_TYPE_PARAMETERS
} menu_token_type_t;

typedef enum
{
  MENU_DRV_MSG_WAIT_TO_INIT,
  MENU_DRV_MSG_IDLE_STATE,
  MENU_DRV_MSG_MENU_STOP,
  MENU_DRV_MSG_POWER_OFF,
  MENU_DRV_MSG_LAST
} menuDrvMsg_t;

typedef enum
{
  MENU_DRV_NORMAL_INIT,
  MENU_DRV_LOW_BATTERY_INIT,
} menu_drv_init_t;

typedef void ( *menuDrvDrawBatteryCb_t )( uint8_t x, uint8_t y, float accum_voltage, bool is_charging );
typedef void ( *menuDrvDrawSignalCb_t )( uint8_t x, uint8_t y, uint8_t signal_lvl );
typedef const char* ( *menuDrvGetMsgCb_t )( menuDrvMsg_t msg );

typedef struct
{
  void ( *new_value )( uint32_t value );
  bool ( *enter )( void* arg );
  bool ( *exit )( void* arg );
  bool ( *process )( void* arg );
  bool ( *button_init_cb )( void* arg );
} menu_cb_t;

typedef struct
{
  void ( *rise_callback )( void* button );
  void ( *fall_callback )( void* button );
  void ( *timer_callback )( void* button );
} menu_but_cb_t;

typedef struct
{
  uint8_t start;
  uint8_t end;
} menu_line_t;

typedef struct
{
  menu_but_cb_t up;
  menu_but_cb_t down;
  menu_but_cb_t enter;
  menu_but_cb_t exit;
  menu_but_cb_t up_minus;
  menu_but_cb_t up_plus;
  menu_but_cb_t down_minus;
  menu_but_cb_t down_plus;
  menu_but_cb_t on_off;
  menu_but_cb_t motor_on;
} menu_button_t;

typedef struct menu_token
{
  int token;
  char* help;
  menu_token_type_t arg_type;
  struct menu_token** menu_list;
  uint32_t* value;

  /* For LCD position menu */
  uint8_t position;
  menu_line_t line;
  bool last_button;
  bool update_screen_req;

  /* Menu callbacks */
  menu_cb_t menu_cb;
  menu_button_t button;
  uint32_t name_dict;
} menu_token_t;

void menuDrvInit( menu_drv_init_t init_type, void ( *toggleEmergencyDisable )( void ) );
int menuDrvElementsCnt( menu_token_t* menu );
void menuEnter( menu_token_t* menu );
void menuDrv_Exit( menu_token_t* menu );
void menuSetMain( menu_token_t* menu );
void menuDrvSaveParameters( void );
void menuPrintfInfo( const char* format, ... );
void menuDrvEnterEmergencyDisable( void );
void menuDrvExitEmergencyDisable( void );

void enterMenuStart( void );
void menuDrv_EnterToParameters( void );
void menuDrvDisableSystemProcess( void );

void menuDrvSetDrawBatteryCb( menuDrvDrawBatteryCb_t cb );
void menuDrvSetDrawSignalCb( menuDrvDrawSignalCb_t cb );
void menuDrvSetGetMsgCb( menuDrvGetMsgCb_t cb );

#endif