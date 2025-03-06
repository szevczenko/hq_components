#ifndef XKC_KL200_UART_H
#define XKC_KL200_UART_H

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Error codes for XKC-KL200 UART operations.
 */
typedef enum
{
  XKC_CODE_OK = 0,       /**< Operation successful */
  XKC_CODE_FAIL = -1,    /**< Operation failed */
  XKC_CODE_TIMEOUT = -2, /**< Operation timed out */
} xkc_error_code_t;

/**
 * @brief Upload modes for XKC-KL200.
 */
typedef enum
{
  UPLOAD_MODE_MANUAL = 0,         /**< Manual upload mode */
  UPLOAD_MODE_AUTOMATIC = 1,      /**< Automatic upload mode */
  UPLOAD_MODE_HIGH_STABILITY = 2, /**< High stability upload mode */
} xkc_upload_mode_t;

/**
 * @brief LED modes for XKC-KL200.
 */
typedef enum
{
  LED_MODE_ON_WHEN_SENSING = 0, /**< LED on when sensing */
  LED_MODE_OFF_WHEN_SENSING = 1, /**< LED off when sensing */
  LED_MODE_ALWAYS_OFF = 2,       /**< LED always off */
  LED_MODE_ALWAYS_ON = 3,        /**< LED always on */
} xkc_led_mode_t;

/**
 * @brief Relay modes for XKC-KL200.
 */
typedef enum
{
  RELAY_MODE_START_WHEN_SENSING = 0, /**< Relay starts when sensing */
  RELAY_MODE_CLOSE_WHEN_SENSING = 1, /**< Relay closes when sensing */
} xkc_relay_mode_t;

/**
 * @brief Line modes for XKC-KL200.
 */
typedef enum
{
  LINE_MODE_RELAY = 0, /**< Relay mode */
  LINE_MODE_UART = 1,  /**< UART mode */
} xkc_line_mode_t;

/**
 * @brief Baud rates for XKC-KL200.
 */
typedef enum
{
  BAUD_RATE_2400 = 0,   /**< 2400 bps */
  BAUD_RATE_4800 = 1,   /**< 4800 bps */
  BAUD_RATE_9600 = 2,   /**< 9600 bps */
  BAUD_RATE_14400 = 3,  /**< 14400 bps */
  BAUD_RATE_19200 = 4,  /**< 19200 bps */
  BAUD_RATE_38400 = 5,  /**< 38400 bps */
  BAUD_RATE_56000 = 6,  /**< 56000 bps */
  BAUD_RATE_57600 = 7,  /**< 57600 bps */
  BAUD_RATE_115200 = 8, /**< 115200 bps */
  BAUD_RATE_128000 = 9, /**< 128000 bps */
} xkc_baud_rate_t;

/**
 * @brief Configuration structure for XKC-KL200.
 */
typedef struct
{
  xkc_baud_rate_t baud_rate;          /**< Baud rate */
  uint16_t device_address;            /**< Device address */
  uint16_t calibration_distance;      /**< Calibration distance */
  xkc_upload_mode_t upload_mode;      /**< Upload mode */
  uint8_t upload_interval;            /**< Upload interval */
  xkc_led_mode_t led_mode;            /**< LED mode */
  xkc_relay_mode_t relay_mode;        /**< Relay mode */
  xkc_line_mode_t line_mode;          /**< Line mode */
} xkc_configuration_t;

/**
 * @brief Restore factory settings.
 * 
 * @return xkc_error_code_t Error code
 */
xkc_error_code_t xkc_restore_factory_settings();

/**
 * @brief Configure baud rate.
 * 
 * @param baud_rate Baud rate to set
 * @return xkc_error_code_t Error code
 */
xkc_error_code_t xkc_configure_baud_rate(xkc_baud_rate_t baud_rate);

/**
 * @brief Read current configuration.
 * 
 * @param config Pointer to configuration structure to fill
 * @return xkc_error_code_t Error code
 */
xkc_error_code_t xkc_read_current_configuration(xkc_configuration_t* config);

/**
 * @brief Configure upload mode.
 * 
 * @param mode Upload mode to set
 * @return xkc_error_code_t Error code
 */
xkc_error_code_t xkc_configure_upload_mode(xkc_upload_mode_t mode);

/**
 * @brief Configure upload interval.
 * 
 * @param interval Upload interval to set
 * @return xkc_error_code_t Error code
 */
xkc_error_code_t xkc_configure_upload_interval(uint8_t interval);

/**
 * @brief Configure LED mode.
 * 
 * @param mode LED mode to set
 * @return xkc_error_code_t Error code
 */
xkc_error_code_t xkc_configure_led_mode(xkc_led_mode_t mode);

/**
 * @brief Configure relay mode.
 * 
 * @param mode Relay mode to set
 * @return xkc_error_code_t Error code
 */
xkc_error_code_t xkc_configure_relay_mode(xkc_relay_mode_t mode);

/**
 * @brief Configure line mode.
 * 
 * @param mode Line mode to set
 * @return xkc_error_code_t Error code
 */
xkc_error_code_t xkc_configure_line_mode(xkc_line_mode_t mode);

/**
 * @brief Initialize XKC-KL200 UART.
 * 
 * @param uart_num UART number
 * @param baud_rate Baud rate
 * @param tx_pin TX pin
 * @param rx_pin RX pin
 */
void xkc_init(int uart_num, uint32_t baud_rate, uint64_t tx_pin, uint64_t rx_pin);

#endif // XKC_KL200_UART_H
