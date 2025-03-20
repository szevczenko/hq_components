/**
 * @file e108-gn03.h
 * @brief Header file for E108-GN03 GNSS module driver
 */

#ifndef E108_GN03_H
#define E108_GN03_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief E108-GN03 driver error codes
 */
typedef enum {
    E108_OK                    = 0,      /* Operation completed successfully */
    E108_ERR_INVALID_ARG       = -1,     /* Invalid argument */
    E108_ERR_NOT_INITIALIZED   = -2,     /* Driver not initialized */
    E108_ERR_UART_CONFIG       = -3,     /* UART configuration error */
    E108_ERR_UART_INSTALL      = -4,     /* UART driver installation error */
    E108_ERR_UART_SET_PIN      = -5,     /* UART pin configuration error */
    E108_ERR_UART_WRITE        = -6,     /* UART write operation failed */
    E108_ERR_UART_READ         = -7,     /* UART read operation failed */
    E108_ERR_TIMEOUT           = -8,     /* Operation timed out */
    E108_ERR_NO_MEMORY         = -9,     /* Memory allocation failed */
    E108_ERR_INVALID_RESPONSE  = -10,    /* Invalid response from module */
    E108_ERR_CMD_FAILED        = -11     /* Command execution failed */
} e108_err_t;

/**
 * @brief E108-GN03 driver handle
 */
typedef struct e108_gn03_t* e108_gn03_handle_t;

/**
 * @brief E108-GN03 driver configuration
 */
typedef struct {
    int uart_port;              /* UART port number */
    uint32_t uart_baud_rate;    /* UART baud rate */
    int uart_tx_pin;            /* UART TX pin */
    int uart_rx_pin;            /* UART RX pin */
} e108_gn03_config_t;

/**
 * @brief Initialize E108-GN03 driver
 * 
 * @param config Pointer to driver configuration
 * @return e108_gn03_handle_t Driver handle, or NULL on error
 */
e108_gn03_handle_t e108_init(const e108_gn03_config_t* config);

/**
 * @brief Deinitialize E108-GN03 driver
 * 
 * @param driver Driver handle
 * @return e108_err_t E108_OK on success, or error code on failure
 */
e108_err_t e108_deinit(e108_gn03_handle_t driver);

/**
 * @brief Set the serial communication baud rate using CAS01 command
 * 
 * @param driver Driver handle
 * @param baud_rate Baud rate code:
 *                  0=4800bps, 1=9600bps, 2=19200bps,
 *                  3=38400bps, 4=57600bps, 5=115200bps
 * @return e108_err_t E108_OK on success, or error code on failure
 */
e108_err_t e108_set_baud_rate(e108_gn03_handle_t driver, int baud_rate);

/**
 * @brief Send command to the E108-GN03 module
 * 
 * @param driver Driver handle
 * @param cmd Command string to send
 * @return int Number of bytes sent, or negative value on error
 */
int e108_send_command(e108_gn03_handle_t driver, const char *cmd);

/**
 * @brief Set the positioning update rate using CAS02 command
 * 
 * @param driver Driver handle
 * @param update_interval Update interval in ms:
 *                       1000 = 1Hz (1 positioning point per second)
 *                       500 = 2Hz (2 positioning points per second)  
 *                       250 = 4Hz (4 positioning points per second)
 *                       200 = 5Hz (5 positioning points per second)
 *                       100 = 10Hz (10 positioning points per second)
 * @return e108_err_t E108_OK on success, or error code on failure
 */
e108_err_t e108_set_update_rate(e108_gn03_handle_t driver, int update_interval);

/**
 * @brief Structure to hold NMEA sentence output configuration
 * 
 * Values 0-9 mean:
 * - 0: No output
 * - 1-9: Output once every n positioning times
 * - -1: Keep existing configuration (field left blank)
 */
typedef struct {
    int nGGA;    /* GGA output frequency */
    int nGLL;    /* GLL output frequency */
    int nGSA;    /* GSA output frequency */
    int nGSV;    /* GSV output frequency */
    int nRMC;    /* RMC output frequency */
    int nVTG;    /* VTG output frequency */
    int nZDA;    /* ZDA output frequency */
    int nANT;    /* ANT output frequency */
    int nDHV;    /* DHV output frequency */
    int nLPS;    /* LPS output frequency */
    int res1;    /* Reserved field 1 */
    int res2;    /* Reserved field 2 */
    int nUTC;    /* UTC output frequency */
    int nGST;    /* GST output frequency */
    int res3;    /* Reserved field 3 */
    int res4;    /* Reserved field 4 */
    int res5;    /* Reserved field 5 */
    int nTIM;    /* TIM (PCAS60) output frequency */
} e108_nmea_config_t;

/**
 * @brief Set the NMEA sentence output configuration using CAS03 command
 * 
 * @param driver Driver handle
 * @param config Pointer to NMEA output configuration structure
 * @return e108_err_t E108_OK on success, or error code on failure
 */
e108_err_t e108_set_nmea_output(e108_gn03_handle_t driver, const e108_nmea_config_t *config);

/**
 * @brief E108-GN03 GNSS working system modes
 */
typedef enum {
    E108_MODE_GPS         = 1,  /* Single GPS working mode */
    E108_MODE_BDS         = 2,  /* Single BeiDou working mode */
    E108_MODE_GPS_BDS     = 3,  /* GPS + BeiDou dual mode */
    E108_MODE_GLONASS     = 4,  /* Single GLONASS working mode */
    E108_MODE_GPS_GLONASS = 5,  /* GPS + GLONASS dual mode */
    E108_MODE_BDS_GLONASS = 6,  /* BeiDou + GLONASS dual mode */
    E108_MODE_ALL         = 7   /* GPS + BeiDou + GLONASS triple mode */
} e108_system_mode_t;

/**
 * @brief Configure GNSS working system using CAS04 command
 * 
 * @param driver Driver handle
 * @param mode Working system mode (see e108_system_mode_t enum)
 * @return e108_err_t E108_OK on success, or error code on failure
 */
e108_err_t e108_set_system_mode(e108_gn03_handle_t driver, e108_system_mode_t mode);

/**
 * @brief E108-GN03 NMEA protocol types
 */
typedef enum {
    E108_PROTOCOL_NMEA_4_1      = 2,  /* Compatible with NMEA 4.1 and above */
    E108_PROTOCOL_BDS_GPS_DUAL  = 5,  /* BDS/GPS dual-mode protocol of China Transportation 
                                        Information Center, compatible with NMEA 2.3 and above,
                                        compatible with NMEA 4.0 Protocol */
    E108_PROTOCOL_GPS_NMEA_0183 = 9   /* Compatible with single GPS NMEA 0183 protocol, 
                                        compatible with NMEA 2.2 version */
} e108_protocol_type_t;

/**
 * @brief Set the NMEA protocol type using CAS05 command
 * 
 * @param driver Driver handle
 * @param protocol_type NMEA protocol type (see e108_protocol_type_t enum)
 * @return e108_err_t E108_OK on success, or error code on failure
 */
e108_err_t e108_set_protocol_type(e108_gn03_handle_t driver, e108_protocol_type_t protocol_type);

/**
 * @brief E108-GN03 product information types
 */
typedef enum {
    E108_INFO_FIRMWARE_VERSION   = 0,  /* Query firmware version number */
    E108_INFO_HARDWARE_MODEL     = 1,  /* Query hardware model and serial number */
    E108_INFO_WORKING_MODE       = 2,  /* Query working mode of multi-mode receiver */
    E108_INFO_CUSTOMER_NUMBER    = 3,  /* Query customer number of the product */
    E108_INFO_UPGRADE_CODE       = 5   /* Query the upgrade code information */
} e108_info_type_t;

/**
 * @brief Query product information using CAS06 command
 * 
 * @param driver Driver handle
 * @param info_type Type of information to query (see e108_info_type_t enum)
 * @return e108_err_t E108_OK on success, or error code on failure
 */
e108_err_t e108_query_product_info(e108_gn03_handle_t driver, e108_info_type_t info_type);

/**
 * @brief Structure to store GGA sentence data
 * 
 * GGA - Global Positioning System Fix Data
 * Format: $--GGA,hhmmss.ss,llll.ll,a,yyyyy.yy,a,x,xx,xx.x,x.x,M,x.x,M,x.x,xxxx*hh
 */
typedef struct {
    char message_id[6];       /* Message identifier (GPGGA, BDGGA, etc.) */
    char utc_time[11];        /* UTC time in hhmmss.sss format */
    double latitude;          /* Latitude in decimal degrees */
    char north_south;         /* N=North, S=South */
    double longitude;         /* Longitude in decimal degrees */
    char east_west;           /* E=East, W=West */
    uint8_t fix_quality;      /* 0=No fix, 1=GPS fix, 2=DGPS fix, 3=PPS fix */
    uint8_t satellites_used;  /* Number of satellites being used (0-12) */
    float hdop;               /* Horizontal Dilution of Precision */
    float altitude;           /* Altitude above mean sea level */
    char altitude_units;      /* Units for altitude (typically 'M' for meters) */
    float geoid_separation;   /* Geoidal separation */
    char geoid_units;         /* Units for geoidal separation (typically 'M' for meters) */
    float diff_time;          /* Time since last DGPS update (empty if no DGPS) */
    int diff_station_id;      /* DGPS reference station ID (empty if no DGPS) */
    bool valid;               /* True if the sentence was parsed successfully */
} e108_gga_data_t;

/**
 * @brief Parse NMEA GGA sentence
 * 
 * @param sentence The NMEA sentence string to parse
 * @param gga_data Pointer to structure to store parsed data
 * @return int 0 on success, negative value on error
 */
int e108_parse_gga(const char *sentence, e108_gga_data_t *gga_data);

/**
 * @brief Convert NMEA format latitude/longitude to decimal degrees
 * 
 * @param nmea_coord NMEA coordinate in ddmm.mmmm format
 * @param direction N/S or E/W indicator
 * @return double Coordinate in decimal degrees (negative for S/W)
 */
double e108_nmea_to_decimal_degrees(const char *nmea_coord, char direction);

/**
 * @brief Structure to store GSA sentence data
 * 
 * GSA - GNSS DOP and Active Satellites
 * Format: $--GSA,a,a,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,xx,xx,xx*hh
 */
typedef struct {
    char message_id[6];       /* Message identifier (GPGSA, BDGSA, etc.) */
    char mode1;               /* Mode: M=Manual, A=Automatic */
    uint8_t mode2;            /* Fix type: 1=No fix, 2=2D fix, 3=3D fix */
    uint8_t satellite_ids[12]; /* IDs of satellites used for fix (0 means unused slot) */
    float pdop;               /* Position dilution of precision */
    float hdop;               /* Horizontal dilution of precision */
    float vdop;               /* Vertical dilution of precision */
    bool valid;               /* True if the sentence was parsed successfully */
} e108_gsa_data_t;

/**
 * @brief Parse NMEA GSA sentence
 * 
 * @param sentence The NMEA sentence string to parse
 * @param gsa_data Pointer to structure to store parsed data
 * @return int 0 on success, negative value on error
 */
int e108_parse_gsa(const char *sentence, e108_gsa_data_t *gsa_data);

/**
 * @brief Structure to store satellite information from GSV sentence
 */
typedef struct {
    uint8_t satellite_id;     /* Satellite ID (1-32) */
    uint8_t elevation;        /* Elevation angle in degrees (0-90) */
    uint16_t azimuth;         /* Azimuth angle in degrees (0-359) */
    uint8_t snr;              /* Signal-to-Noise ratio in dBHz (0-99, 0 if not tracked) */
    bool valid;               /* True if this entry contains valid data */
} e108_satellite_info_t;

/**
 * @brief Maximum number of satellites that can be tracked
 */
#define E108_MAX_SATELLITES 32

/**
 * @brief Structure to store GSV sentence data
 * 
 * GSV - GNSS Satellites in View
 * Format: $--GSV,x,x,x,x,x,x,x,...*hh
 */
typedef struct {
    char message_id[6];                          /* Message identifier (GPGSV, BDGSV, etc.) */
    uint8_t total_messages;                      /* Total number of GSV messages (1-3) */
    uint8_t message_number;                      /* Message number (1-3) */
    uint8_t satellites_in_view;                  /* Total number of satellites in view */
    e108_satellite_info_t satellites[E108_MAX_SATELLITES]; /* Information for each satellite */
    bool valid;                                  /* True if the sentence was parsed successfully */
} e108_gsv_data_t;

/**
 * @brief Parse NMEA GSV sentence
 * 
 * @param sentence The NMEA sentence string to parse
 * @param gsv_data Pointer to structure to store parsed data
 * @return int 0 on success, negative value on error
 */
int e108_parse_gsv(const char *sentence, e108_gsv_data_t *gsv_data);

/**
 * @brief Structure to store RMC sentence data
 * 
 * RMC - Recommended Minimum Navigation Information
 * Format: $--RMC,hhmmss.ss,A,llll.ll,a,yyyyy.yy,a,xx,xx,xxxx,xx,a*hh
 */
typedef struct {
    char message_id[6];       /* Message identifier (GPRMC, BDRMC, etc.) */
    char utc_time[11];        /* UTC time in hhmmss.sss format */
    char status;              /* Status: A=Valid, V=Invalid */
    double latitude;          /* Latitude in decimal degrees */
    char north_south;         /* N=North, S=South */
    double longitude;         /* Longitude in decimal degrees */
    char east_west;           /* E=East, W=West */
    float speed;              /* Speed over ground in knots */
    float track_angle;        /* Track angle in degrees True */
    char date[7];             /* Date in ddmmyy format */
    float magnetic_variation; /* Magnetic variation in degrees */
    char mag_var_direction;   /* E/W indicator for magnetic variation */
    char mode;                /* Mode indicator: A=Autonomous, D=Differential, E=Estimated */
    bool valid;               /* True if the sentence contains valid data */
} e108_rmc_data_t;

/**
 * @brief Parse NMEA RMC sentence
 * 
 * @param sentence The NMEA sentence string to parse
 * @param rmc_data Pointer to structure to store parsed data
 * @return int 0 on success, negative value on error
 */
int e108_parse_rmc(const char *sentence, e108_rmc_data_t *rmc_data);

/**
 * @brief Structure to store VTG sentence data
 * 
 * VTG - Track Made Good and Ground Speed
 * Format: $--VTG,xx,T,xx,M,xx,N,xx,K*hh
 */
typedef struct {
    char message_id[6];       /* Message identifier (GPVTG, BDVTG, etc.) */
    float track_true;         /* Track angle in degrees True */
    float track_magnetic;     /* Track angle in degrees Magnetic */
    float speed_knots;        /* Speed over ground in knots */
    float speed_kmh;          /* Speed over ground in kilometers per hour */
    char mode;                /* Mode indicator: A=Autonomous, D=Differential, E=Estimated, etc. */
    bool valid;               /* True if the sentence contains valid data */
} e108_vtg_data_t;

/**
 * @brief Parse NMEA VTG sentence
 * 
 * @param sentence The NMEA sentence string to parse
 * @param vtg_data Pointer to structure to store parsed data
 * @return int 0 on success, negative value on error
 */
int e108_parse_vtg(const char *sentence, e108_vtg_data_t *vtg_data);

#ifdef __cplusplus
}
#endif

#endif /* E108_GN03_H */
