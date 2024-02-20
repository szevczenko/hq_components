/*
pcf8574 lib 0x01

copyright (c) Davide Gironi, 2012

Released under GPLv3.
Please refer to LICENSE file for licensing information.
*/

#include "pcf8574.h"

#include "app_config.h"
#include "driver/i2c.h"

#undef LOG
#define LOG( ... )    // LOG( __VA_ARGS__)

#ifndef PCF8574_I2C_PORT
#define PCF8574_I2C_PORT I2C_NUM_0
#endif
#define ACK_CHECK_EN  0x1 /*!< I2C master will check ack from slave*/
#define LAST_NACK_VAL 0x2 /*!< I2C last_nack value */

uint8_t pcf8574_pinstatus[PCF8574_MAXDEVICES];

/*
 * initialize
 */
void pcf8574_init()
{
  // reset the pin status
  uint8_t i = 0;
  for ( i = 0; i < PCF8574_MAXDEVICES; i++ )
    pcf8574_pinstatus[i] = 0;
}

/*
 * get output status
 */
uint8_t pcf8574_getoutput( uint8_t deviceid )
{
  uint8_t data = -1;
  if ( ( deviceid < PCF8574_MAXDEVICES ) )
  {
    data = pcf8574_pinstatus[deviceid];
  }
  return data;
}

/*
 * get output pin status
 */
uint8_t pcf8574_getoutputpin( uint8_t deviceid, uint8_t pin )
{
  uint8_t data = -1;
  if ( ( deviceid < PCF8574_MAXDEVICES ) && ( pin < PCF8574_MAXPINS ) )
  {
    data = pcf8574_pinstatus[deviceid];
    data = ( data >> pin ) & 0b00000001;
  }
  return data;
}

/*
 * set output pins
 */
uint8_t pcf8574_setoutput( uint8_t deviceid, uint8_t data )
{
  if ( ( deviceid < PCF8574_MAXDEVICES ) )
  {
    pcf8574_pinstatus[deviceid] = data;
    int ret;
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start( cmd );
    i2c_master_write_byte( cmd, PCF8574_ADDRBASE | I2C_MASTER_WRITE, ACK_CHECK_EN );
    i2c_master_write_byte( cmd, data, ACK_CHECK_EN );
    i2c_master_stop( cmd );
    ret = i2c_master_cmd_begin( PCF8574_I2C_PORT, cmd, MS2ST( 1000 ) );
    i2c_cmd_link_delete( cmd );
    return ret;
  }
  return -1;
}

/*
 * set output pins, replace actual status of a device from pinstart for pinlength with data
 */
uint8_t pcf8574_setoutputpins( uint8_t deviceid, uint8_t pinstart, uint8_t pinlength, uint8_t data )
{
  // example:
  // actual data is         0b01101110
  // want to change              ---
  // pinstart                    4
  // data                        101   (pinlength 3)
  // result                 0b01110110
  if ( ( deviceid < PCF8574_MAXDEVICES ) && ( pinstart - pinlength + 1 >= 0 && pinstart - pinlength + 1 >= 0 && pinstart < PCF8574_MAXPINS && pinstart > 0 && pinlength > 0 ) )
  {
    uint8_t b = 0;
    b = pcf8574_pinstatus[deviceid];
    uint8_t mask = ( ( 1 << pinlength ) - 1 ) << ( pinstart - pinlength + 1 );
    data <<= ( pinstart - pinlength + 1 );
    data &= mask;
    b &= ~( mask );
    b |= data;
    pcf8574_pinstatus[deviceid] = b;
    // update device
    int ret;
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start( cmd );
    i2c_master_write_byte( cmd, PCF8574_ADDRBASE | I2C_MASTER_WRITE, ACK_CHECK_EN );
    i2c_master_write_byte( cmd, b, ACK_CHECK_EN );
    i2c_master_stop( cmd );
    ret = i2c_master_cmd_begin( PCF8574_I2C_PORT, cmd, MS2ST( 1000 ) );
    i2c_cmd_link_delete( cmd );
    return ret;
  }
  return -1;
}

/*
 * set output pin
 */
uint8_t pcf8574_setoutputpin( uint8_t deviceid, uint8_t pin, uint8_t data )
{
  if ( ( deviceid < PCF8574_MAXDEVICES ) && ( pin < PCF8574_MAXPINS ) )
  {
    uint8_t b = 0;
    b = pcf8574_pinstatus[deviceid];
    b = ( data != 0 ) ? ( b | ( 1 << pin ) ) : ( b & ~( 1 << pin ) );
    pcf8574_pinstatus[deviceid] = b;
    // update device
    int ret;
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start( cmd );
    i2c_master_write_byte( cmd, PCF8574_ADDRBASE | I2C_MASTER_WRITE, ACK_CHECK_EN );
    i2c_master_write_byte( cmd, b, ACK_CHECK_EN );
    i2c_master_stop( cmd );
    ret = i2c_master_cmd_begin( PCF8574_I2C_PORT, cmd, MS2ST( 1000 ) );
    i2c_cmd_link_delete( cmd );
    return ret;
  }
  return -1;
}

/*
 * set output pin high
 */
uint8_t pcf8574_setoutputpinhigh( uint8_t deviceid, uint8_t pin )
{
  return pcf8574_setoutputpin( deviceid, pin, 1 );
}

/*
 * set output pin low
 */
uint8_t pcf8574_setoutputpinlow( uint8_t deviceid, uint8_t pin )
{
  return pcf8574_setoutputpin( deviceid, pin, 0 );
}

/*
 * get input data
 */
int pcf8574_getinput( uint8_t deviceid )
{
  int data = 0;
  if ( ( deviceid < PCF8574_MAXDEVICES ) )
  {
    int ret;
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start( cmd );
    i2c_master_write_byte( cmd, PCF8574_ADDRBASE | I2C_MASTER_READ, ACK_CHECK_EN );
    i2c_master_read( cmd, (uint8_t*) &data, 1, LAST_NACK_VAL );
    i2c_master_stop( cmd );
    ret = i2c_master_cmd_begin( PCF8574_I2C_PORT, cmd, MS2ST( 1000 ) );
    i2c_cmd_link_delete( cmd );
    if ( ret != ESP_OK )
    {
      // LOG("i2c status %d\n", ret);
      return ret;
    }
  }
  return data;
}

/*
 * get input pin (up or low)
 */
uint8_t pcf8574_getinputpin( uint8_t deviceid, uint8_t pin )
{
  uint8_t data = -1;
  if ( ( deviceid < PCF8574_MAXDEVICES ) && ( pin < PCF8574_MAXPINS ) )
  {
    data = pcf8574_getinput( deviceid );
    if ( data != 255 )
    {
      data = ( data >> pin ) & 0b00000001;
    }
  }
  return data;
}
