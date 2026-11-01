// Name :BALAKUMAR H
// Batch ID : 26001_081
/* TFTP : TFTP (Trivial File Transfer Protocol) is a simple file transfer protocol 
        used to transfer files between a client and a server over a network. It 
        operates on top of UDP and provides a lightweight mechanism for transferring 
        files without the complexity of protocols such as FTP.*/
#include "tftp.h"


/*========================================================
                    SEND ERROR PACKET
========================================================*/

void send_error(int sock_fd,
                struct sockaddr_in *cli_addr,
                socklen_t cli_len,
                unsigned short error_code,
                char *error_message)
{
    char error_packet[516];

    unsigned short opcode;
    unsigned short code;

    int packet_len;

    /* ERROR opcode = 5 */
    opcode = htons(ERROR);
    code = htons(error_code);

    memcpy(error_packet, &opcode, 2);
    memcpy(error_packet + 2, &code, 2);

    strcpy(error_packet + 4, error_message);

    packet_len = 4 + strlen(error_message) + 1;

    if (sendto(sock_fd,
               error_packet,
               packet_len,
               0,
               (struct sockaddr *)cli_addr,
               cli_len) < 0)
    {
        perror("sendto");
    }
}


/*========================================================
                    SEND ACK PACKET
========================================================*/

int send_ack(int sock_fd,
             struct sockaddr_in *addr,
             socklen_t addr_len,
             unsigned short block)
{
    char ack[4];

    unsigned short opcode;
    unsigned short block_net;

    opcode = htons(ACK);
    block_net = htons(block);

    memcpy(ack, &opcode, 2);
    memcpy(ack + 2, &block_net, 2);

    if (sendto(sock_fd,
               ack,
               sizeof(ack),
               0,
               (struct sockaddr *)addr,
               addr_len) < 0)
    {
        perror("sendto");
        return -1;
    }

    return 0;
}


/*========================================================
                SEND DATA WITH RETRY
========================================================*/

int send_data_with_retry(int sock_fd,
                         struct sockaddr_in *cli_addr,
                         socklen_t cli_len,
                         char *packet,
                         int packet_len,
                         unsigned short block)
{
    struct timeval timeout;

    timeout.tv_sec = 5;
    timeout.tv_usec = 0;

    if (setsockopt(sock_fd,
                   SOL_SOCKET,
                   SO_RCVTIMEO,
                   &timeout,
                   sizeof(timeout)) < 0)
    {
        perror("setsockopt");
        return -1;
    }


    int attempts;

    for (attempts = 0; attempts < 3; attempts++)
    {
        /*----------------------------------------
                    SEND DATA
        ----------------------------------------*/

        if (sendto(sock_fd,
                   packet,
                   packet_len,
                   0,
                   (struct sockaddr *)cli_addr,
                   cli_len) < 0)
        {
            perror("sendto");
            return -1;
        }

        printf("DATA block %d sent (attempt %d)\n",
               block,
               attempts + 1);


        /*----------------------------------------
                    RECEIVE ACK
        ----------------------------------------*/

        char ack[4];

        struct sockaddr_in ack_addr;
        socklen_t ack_len = sizeof(ack_addr);

        int recv_len;

        recv_len = recvfrom(sock_fd,
                            ack,
                            sizeof(ack),
                            0,
                            (struct sockaddr *)&ack_addr,
                            &ack_len);


        /* Timeout */

        if (recv_len < 0)
        {
            if (errno == EAGAIN ||
                errno == EWOULDBLOCK)
            {
                printf("Timeout waiting for ACK block %d\n",
                       block);

                continue;
            }

            perror("recvfrom");
            return -1;
        }


        /* Check ACK size */

        if (recv_len != 4)
        {
            printf("Invalid ACK packet\n");
            continue;
        }


        unsigned short ack_opcode;
        unsigned short ack_block;

        memcpy(&ack_opcode, ack, 2);
        memcpy(&ack_block, ack + 2, 2);

        ack_opcode = ntohs(ack_opcode);
        ack_block = ntohs(ack_block);


        if (ack_opcode == ACK &&
            ack_block == block)
        {
            printf("ACK block %d received\n",
                   block);

            return 0;
        }

        printf("Unexpected ACK received\n");
    }


    printf("Maximum retransmission attempts reached\n");

    return -1;
}


/*========================================================
                    HANDLE RRQ
========================================================*/

void handle_rrq(int sock_fd,
                struct sockaddr_in *cli_addr,
                socklen_t cli_len,
                char *filename)
{
    int fd;

    char data[BLOCK_SIZE];
    char packet[PACKET_SIZE];

    unsigned short block = 1;

    int bytes_read;


    printf("\n========== RRQ ==========\n");
    printf("Filename: %s\n", filename);


    /*----------------------------------------
                OPEN FILE
    ----------------------------------------*/

    fd = open(filename, O_RDONLY);

    if (fd < 0)
    {
        printf("File not found: %s\n", filename);

        send_error(sock_fd,
                   cli_addr,
                   cli_len,
                   1,
                   "File not found");

        return;
    }

    printf("File opened successfully\n");


    /*----------------------------------------
                READ FILE
    ----------------------------------------*/

    while (1)
    {
        bytes_read = read(fd,
                          data,
                          BLOCK_SIZE);


        printf("read() returned %d bytes\n",
               bytes_read);


        /*----------------------------------------
                READ ERROR
        ----------------------------------------*/

        if (bytes_read < 0)
        {
            perror("read");

            send_error(sock_fd,
                       cli_addr,
                       cli_len,
                       0,
                       "Read error");

            close(fd);

            return;
        }


        /*----------------------------------------
                CREATE DATA PACKET
        ----------------------------------------*/

        unsigned short opcode_net;
        unsigned short block_net;

        opcode_net = htons(DATA);
        block_net = htons(block);

        memcpy(packet,
               &opcode_net,
               2);

        memcpy(packet + 2,
               &block_net,
               2);


        if (bytes_read > 0)
        {
            memcpy(packet + 4,
                   data,
                   bytes_read);
        }


        int packet_len;

        packet_len = 4 + bytes_read;


        /*----------------------------------------
                SEND DATA AND WAIT ACK
        ----------------------------------------*/

        if (send_data_with_retry(sock_fd,
                                 cli_addr,
                                 cli_len,
                                 packet,
                                 packet_len,
                                 block) < 0)
        {
            printf("RRQ transfer failed\n");

            close(fd);

            return;
        }


        /*----------------------------------------
                LAST PACKET
        ----------------------------------------*/

        if (bytes_read < BLOCK_SIZE)
        {
            printf("File transfer completed\n");

            close(fd);

            return;
        }


        /*----------------------------------------
                NEXT BLOCK
        ----------------------------------------*/

        block++;
    }
}


/*========================================================
                    HANDLE WRQ
========================================================*/

void handle_wrq(int sock_fd,
                struct sockaddr_in *cli_addr,
                socklen_t cli_len,
                char *filename)
{
    int fd;

    unsigned short expected_block = 1;


    printf("\n========== WRQ ==========\n");
    printf("Filename: %s\n", filename);


    /*----------------------------------------
                CREATE / OPEN FILE
    ----------------------------------------*/

    fd = open(filename,
              O_WRONLY | O_CREAT | O_TRUNC,
              0666);

    if (fd < 0)
    {
        perror("open");

        send_error(sock_fd,
                   cli_addr,
                   cli_len,
                   2,
                   "Access violation");

        return;
    }

    printf("File opened for writing\n");


    /*----------------------------------------
                SEND ACK 0
    ----------------------------------------*/

    if (send_ack(sock_fd,
                 cli_addr,
                 cli_len,
                 0) < 0)
    {
        close(fd);
        return;
    }

    printf("ACK block 0 sent\n");


    /*----------------------------------------
                TIMEOUT
    ----------------------------------------*/

    struct timeval timeout;

    timeout.tv_sec = 5;
    timeout.tv_usec = 0;

    if (setsockopt(sock_fd,
                   SOL_SOCKET,
                   SO_RCVTIMEO,
                   &timeout,
                   sizeof(timeout)) < 0)
    {
        perror("setsockopt");

        close(fd);

        return;
    }


    /*----------------------------------------
                RECEIVE DATA
    ----------------------------------------*/

    while (1)
    {
        char data_packet[PACKET_SIZE];

        struct sockaddr_in data_addr;

        socklen_t data_addr_len =
            sizeof(data_addr);

        int data_len;


        data_len = recvfrom(sock_fd,
                            data_packet,
                            sizeof(data_packet),
                            0,
                            (struct sockaddr *)&data_addr,
                            &data_addr_len);


        /*----------------------------------------
                TIMEOUT
        ----------------------------------------*/

        if (data_len < 0)
        {
            if (errno == EAGAIN ||
                errno == EWOULDBLOCK)
            {
                printf("Timeout waiting for DATA block %d\n",
                       expected_block);

                close(fd);

                return;
            }

            perror("recvfrom");

            close(fd);

            return;
        }


        /*----------------------------------------
                MINIMUM PACKET SIZE
        ----------------------------------------*/

        if (data_len < 4)
        {
            printf("Invalid DATA packet\n");

            send_error(sock_fd,
                       cli_addr,
                       cli_len,
                       4,
                       "Illegal TFTP operation");

            close(fd);

            return;
        }


        /*----------------------------------------
                EXTRACT OPCODE
        ----------------------------------------*/

        unsigned short opcode;

        memcpy(&opcode,
               data_packet,
               2);

        opcode = ntohs(opcode);


        if (opcode != DATA)
        {
            printf("Expected DATA packet\n");

            send_error(sock_fd,
                       cli_addr,
                       cli_len,
                       4,
                       "Illegal TFTP operation");

            close(fd);

            return;
        }


        /*----------------------------------------
                EXTRACT BLOCK NUMBER
        ----------------------------------------*/

        unsigned short data_block;

        memcpy(&data_block,
               data_packet + 2,
               2);

        data_block = ntohs(data_block);


        printf("DATA block %d received\n",
               data_block);


        /*----------------------------------------
                DUPLICATE BLOCK
        ----------------------------------------*/

        if (data_block < expected_block)
        {
            printf("Duplicate DATA block %d\n",
                   data_block);

            send_ack(sock_fd,
                     cli_addr,
                     cli_len,
                     data_block);

            continue;
        }


        /*----------------------------------------
                UNEXPECTED BLOCK
        ----------------------------------------*/

        if (data_block != expected_block)
        {
            printf("Unexpected DATA block %d\n",
                   data_block);

            send_error(sock_fd,
                       cli_addr,
                       cli_len,
                       4,
                       "Illegal TFTP operation");

            close(fd);

            return;
        }


        /*----------------------------------------
                DATA SIZE
        ----------------------------------------*/

        int data_size;

        data_size = data_len - 4;


        /*----------------------------------------
                WRITE FILE
        ----------------------------------------*/

        int total_written = 0;

        while (total_written < data_size)
        {
            int bytes_written;

            bytes_written =
                write(fd,
                      data_packet + 4 + total_written,
                      data_size - total_written);


            if (bytes_written < 0)
            {
                perror("write");

                send_error(sock_fd,
                           cli_addr,
                           cli_len,
                           3,
                           "Disk full or allocation exceeded");

                close(fd);

                return;
            }


            total_written += bytes_written;
        }


        printf("Written %d bytes to file\n",
               total_written);


        /*----------------------------------------
                SEND ACK
        ----------------------------------------*/

        if (send_ack(sock_fd,
                     cli_addr,
                     cli_len,
                     data_block) < 0)
        {
            close(fd);

            return;
        }

        printf("ACK block %d sent\n",
               data_block);


        /*----------------------------------------
                LAST BLOCK
        ----------------------------------------*/

        if (data_size < BLOCK_SIZE)
        {
            printf("File upload completed\n");

            close(fd);

            return;
        }


        expected_block++;
    }
}


/*========================================================
                    MAIN
========================================================*/

int main()
{
    int sock_fd;

    struct sockaddr_in serv_addr;
    struct sockaddr_in cli_addr;

    socklen_t cli_len;

    char buffer[PACKET_SIZE];

    int recv_len;


    /*----------------------------------------
                CREATE SOCKET
    ----------------------------------------*/

    sock_fd = socket(AF_INET,
                     SOCK_DGRAM,
                     0);

    if (sock_fd < 0)
    {
        perror("socket");

        return 1;
    }

    printf("Socket created successfully\n");


    /*----------------------------------------
                SERVER ADDRESS
    ----------------------------------------*/

    memset(&serv_addr,
           0,
           sizeof(serv_addr));

    serv_addr.sin_family = AF_INET;

    serv_addr.sin_addr.s_addr = INADDR_ANY;

    serv_addr.sin_port =
        htons(SERVER_PORT);


    /*----------------------------------------
                BIND
    ----------------------------------------*/

    if (bind(sock_fd,
             (struct sockaddr *)&serv_addr,
             sizeof(serv_addr)) < 0)
    {
        perror("bind");

        close(sock_fd);

        return 1;
    }

    printf("Server bind successful\n");
    printf("Waiting for TFTP requests...\n");


    /*----------------------------------------
                MAIN LOOP
    ----------------------------------------*/

    while (1)
    {
        cli_len = sizeof(cli_addr);


        /*----------------------------------------
                RECEIVE REQUEST
        ----------------------------------------*/

        recv_len = recvfrom(sock_fd,
                            buffer,
                            sizeof(buffer),
                            0,
                            (struct sockaddr *)&cli_addr,
                            &cli_len);


        if (recv_len < 0)
        {
            /*
             * The socket can still have a timeout
             * from a previous transfer.
             */

            if (errno == EAGAIN ||
                errno == EWOULDBLOCK)
            {
                continue;
            }

            perror("recvfrom");

            continue;
        }


        printf("\nReceived %d bytes from client\n",
               recv_len);


        /*----------------------------------------
                VALIDATE REQUEST SIZE
        ----------------------------------------*/

        if (recv_len < 4)
        {
            printf("Invalid TFTP request packet\n");

            send_error(sock_fd,
                       &cli_addr,
                       cli_len,
                       4,
                       "Illegal TFTP operation");

            continue;
        }


        /*----------------------------------------
                EXTRACT OPCODE
        ----------------------------------------*/

        unsigned short opcode;

        memcpy(&opcode,
               buffer,
               2);

        opcode = ntohs(opcode);


        printf("Opcode = %d\n",
               opcode);


        /*----------------------------------------
                VALIDATE OPCODE
        ----------------------------------------*/

        if (opcode != RRQ &&
            opcode != WRQ)
        {
            printf("Invalid TFTP opcode: %d\n",
                   opcode);

            send_error(sock_fd,
                       &cli_addr,
                       cli_len,
                       4,
                       "Illegal TFTP operation");

            continue;
        }


        /*----------------------------------------
                EXTRACT FILENAME
        ----------------------------------------*/

        char *filename_ptr;

        filename_ptr = buffer + 2;


        char *filename_end;

        filename_end =
            memchr(filename_ptr,
                   '\0',
                   recv_len - 2);


        if (filename_end == NULL)
        {
            printf("Invalid filename\n");

            send_error(sock_fd,
                       &cli_addr,
                       cli_len,
                       4,
                       "Illegal TFTP operation");

            continue;
        }


        int filename_len;

        filename_len =
            filename_end - filename_ptr;


        if (filename_len <= 0 ||
            filename_len >= 256)
        {
            printf("Invalid filename length\n");

            send_error(sock_fd,
                       &cli_addr,
                       cli_len,
                       4,
                       "Illegal TFTP operation");

            continue;
        }


        char filename[256];


        memcpy(filename,
               filename_ptr,
               filename_len);

        filename[filename_len] = '\0';


        /*----------------------------------------
                EXTRACT MODE
        ----------------------------------------*/

        char *mode_ptr;

        mode_ptr = filename_end + 1;


        if (mode_ptr >= buffer + recv_len)
        {
            printf("Mode missing\n");

            send_error(sock_fd,
                       &cli_addr,
                       cli_len,
                       4,
                       "Illegal TFTP operation");

            continue;
        }


        char *mode_end;

        mode_end =
            memchr(mode_ptr,
                   '\0',
                   (buffer + recv_len) - mode_ptr);


        if (mode_end == NULL)
        {
            printf("Invalid mode\n");

            send_error(sock_fd,
                       &cli_addr,
                       cli_len,
                       4,
                       "Illegal TFTP operation");

            continue;
        }


        int mode_len;

        mode_len = mode_end - mode_ptr;


        if (mode_len <= 0 ||
            mode_len >= 20)
        {
            printf("Invalid mode length\n");

            send_error(sock_fd,
                       &cli_addr,
                       cli_len,
                       4,
                       "Illegal TFTP operation");

            continue;
        }


        char mode[20];


        memcpy(mode,
               mode_ptr,
               mode_len);

        mode[mode_len] = '\0';


        /*----------------------------------------
                DISPLAY REQUEST
        ----------------------------------------*/

        printf("\n========== TFTP REQUEST ==========\n");

        printf("Opcode   : %d\n",
               opcode);

        printf("Filename : %s\n",
               filename);

        printf("Mode     : %s\n",
               mode);

        printf("==================================\n");


        /*----------------------------------------
                CHECK MODE
        ----------------------------------------*/

        if (strcasecmp(mode, "octet") != 0)
        {
            printf("Unsupported mode: %s\n",
                   mode);

            send_error(sock_fd,
                       &cli_addr,
                       cli_len,
                       4,
                       "Unsupported mode");

            continue;
        }


        /*----------------------------------------
                HANDLE RRQ
        ----------------------------------------*/

        if (opcode == RRQ)
        {
            handle_rrq(sock_fd,
                       &cli_addr,
                       cli_len,
                       filename);
        }


        /*----------------------------------------
                HANDLE WRQ
        ----------------------------------------*/

        else if (opcode == WRQ)
        {
            handle_wrq(sock_fd,
                       &cli_addr,
                       cli_len,
                       filename);
        }


        printf("\nWaiting for next TFTP request...\n");
    }


    close(sock_fd);

    return 0;
}
