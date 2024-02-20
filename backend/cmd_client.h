#ifndef _CMD_CLIENT_H
#define _CMD_CLIENT_H

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "parameters.h"
#include "parse_cmd.h"

#define PORT    8080
#define MAXLINE 1024

#define MSG_OK      1
#define MSG_ERROR   0
#define MSG_TIMEOUT -1

#define BUFFER_CMD 1024

typedef struct
{
  int socket;
  struct sockaddr_in servaddr;
} cmd_client_network_t;

void cmdClientStartTask( void );
void cmdClientStart( void );
void cmdClientStop( void );

int cmdClientSend( uint8_t* buffer, uint32_t len );
int cmdClientRead( uint8_t* buffer, uint32_t len, uint32_t timeout_ms );

void cmdClientDisconnect( void );
int cmdClientTryConnect( uint32_t timeout );
int cmdClientIsConnected( void );
int cmdClientSendDataWaitResp( uint8_t* buff, uint32_t len, uint8_t* buff_rx, uint32_t* rx_len, uint32_t timeout );
int cmdClientAnswerData( uint8_t* buff, uint32_t len );

int cmdClientGetAllValue( uint32_t timeout );
int cmdClientSetAllValue( void );
int cmdClientSetValue( parameter_value_t val, uint32_t value, uint32_t timeout_ms );
int cmdClientSetValueWithoutResp( parameter_value_t val, uint32_t value );
int cmdClientGetValue( parameter_value_t val, uint32_t* value, uint32_t timeout );
int cmdClientSendCmd( parseCmd_t cmd );

#endif