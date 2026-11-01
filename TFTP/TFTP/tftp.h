#ifndef TFTP_H
#define TFTP_H


/*========================================================
                    HEADER FILES
========================================================*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <netinet/in.h>
#include <arpa/inet.h>


/*========================================================
                    TFTP CONSTANTS
========================================================*/

/* TFTP server port */

#define SERVER_PORT 20000


/* TFTP operation codes */

#define RRQ     1
#define WRQ     2
#define DATA    3
#define ACK     4
#define ERROR   5


/* TFTP packet sizes */

#define BLOCK_SIZE  512
#define PACKET_SIZE 516


/*========================================================
                    FUNCTION PROTOTYPES
========================================================*/

/* Send TFTP ERROR packet */

void send_error(int sock_fd,
                struct sockaddr_in *addr,
                socklen_t addr_len,
                unsigned short error_code,
                char *error_message);


/* Send TFTP ACK packet */

int send_ack(int sock_fd,
             struct sockaddr_in *addr,
             socklen_t addr_len,
             unsigned short block);


/* Handle ERROR packet at client */

void handle_error_packet(char *packet,
                         int packet_len);


/* Send RRQ / WRQ request */

int send_request(int sock_fd,
                 struct sockaddr_in *serv_addr,
                 socklen_t serv_len,
                 unsigned short opcode,
                 char *filename);


/* Download file */

int download_file(int sock_fd,
                  struct sockaddr_in *serv_addr,
                  socklen_t serv_len,
                  char *filename);


/* Upload file */

int upload_file(int sock_fd,
                struct sockaddr_in *serv_addr,
                socklen_t serv_len,
                char *filename);


/* Handle RRQ at server */

void handle_rrq(int sock_fd,
                struct sockaddr_in *cli_addr,
                socklen_t cli_len,
                char *filename);


/* Handle WRQ at server */

void handle_wrq(int sock_fd,
                struct sockaddr_in *cli_addr,
                socklen_t cli_len,
                char *filename);


/* Send DATA packet and wait for ACK */

int send_data_with_retry(int sock_fd,
                         struct sockaddr_in *cli_addr,
                         socklen_t cli_len,
                         char *packet,
                         int packet_len,
                         unsigned short block);


#endif
