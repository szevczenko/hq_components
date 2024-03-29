#ifndef _CMD_SERVER_H
#define _CMD_SERVER_H

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

#include "keepalive.h"
#include "parameters.h"
#include "parse_cmd.h"

#ifndef CONFIG_USE_CMD_SERVER
#define CONFIG_USE_CMD_SERVER TRUE
#endif

#define NUMBER_CLIENT 1
#define PORT          8080
#define MAXLINE       1024

#ifndef MSG_OK
#define MSG_OK 1
#endif

#ifndef MSG_ERROR
#define MSG_ERROR 0
#endif

#ifndef MSG_TIMEOUT
#define MSG_TIMEOUT -1
#endif

#define BUFFER_CMD 1024

struct client_network
{
  int client_socket;
  struct sockaddr_in cliaddr;
  keepAlive_t keepAlive;
};

struct server_network
{
  int socket_tcp;
  int count_clients;
  uint8_t* buffer;
  struct sockaddr_in servaddr;
  struct client_network* clients;
};

void cmdServerStartTask( void );
void cmdServerStart( void );
void cmdServerStop( void );
int cmdServerSendData( uint8_t* buff, uint8_t len );
int cmdServerAnswerData( uint8_t* buff, uint32_t len );
int cmdServerSendDataWaitResp( uint8_t* buff, uint32_t len, uint8_t* buff_rx, uint32_t* rx_len, uint32_t timeout );
int cmdServerSetValueWithoutResp( parameter_value_t val, uint32_t value );
int cmdServerSetValueWithoutRespI( parameter_value_t val, uint32_t value );
int cmdServerGetValue( parameter_value_t val, uint32_t* value, uint32_t timeout );
bool cmdServerIsWorking( void );

#endif