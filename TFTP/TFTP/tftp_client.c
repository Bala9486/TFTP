#include "tftp.h"


/*========================================================
                RECEIVE ERROR PACKET
========================================================*/

void handle_error_packet(char *packet, int packet_len)
{
    unsigned short error_code;

    if (packet_len < 5)
    {
        printf("Invalid ERROR packet\n");
        return;
    }

    memcpy(&error_code,
           packet + 2,
           2);

    error_code = ntohs(error_code);

    printf("\n========== TFTP ERROR ==========\n");
    printf("Error Code    : %d\n", error_code);
    printf("Error Message : %s\n", packet + 4);
    printf("================================\n");
}


/*========================================================
                    SEND REQUEST
========================================================*/

int send_request(int sock_fd,
                 struct sockaddr_in *serv_addr,
                 socklen_t serv_len,
                 unsigned short opcode,
                 char *filename)
{
    char request[516];

    int request_len;

    unsigned short opcode_net;

    opcode_net = htons(opcode);

    /*----------------------------------------
                Opcode
    ----------------------------------------*/

    memcpy(request,
           &opcode_net,
           2);


    /*----------------------------------------
                Filename
    ----------------------------------------*/

    strcpy(request + 2,
           filename);


    /*----------------------------------------
                Mode
    ----------------------------------------*/

    strcpy(request + 2 + strlen(filename) + 1,
           "octet");


    /*----------------------------------------
                Packet length
    ----------------------------------------*/

    request_len = 2
               + strlen(filename)
               + 1
               + strlen("octet")
               + 1;


    /*----------------------------------------
                Send request
    ----------------------------------------*/

    if (sendto(sock_fd,
               request,
               request_len,
               0,
               (struct sockaddr *)serv_addr,
               serv_len) < 0)
    {
        perror("sendto");
        return -1;
    }

    return 0;
}


/*========================================================
                    RRQ - DOWNLOAD
========================================================*/

int download_file(int sock_fd,
                  struct sockaddr_in *serv_addr,
                  socklen_t serv_len,
                  char *filename)
{
    char request[516];
    char data_packet[PACKET_SIZE];

    int request_len;

    unsigned short block = 1;

    struct timeval timeout;


    printf("\n========== RRQ ==========\n");
    printf("Downloading: %s\n", filename);


    /*----------------------------------------
                Create RRQ packet
    ----------------------------------------*/

    unsigned short opcode_net;

    opcode_net = htons(RRQ);

    memcpy(request,
           &opcode_net,
           2);

    strcpy(request + 2,
           filename);

    strcpy(request + 2 + strlen(filename) + 1,
           "octet");

    request_len = 2
                + strlen(filename)
                + 1
                + strlen("octet")
                + 1;


    /*----------------------------------------
                Set timeout
    ----------------------------------------*/

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


    /*----------------------------------------
                Send RRQ
    ----------------------------------------*/

    if (sendto(sock_fd,
               request,
               request_len,
               0,
               (struct sockaddr *)serv_addr,
               serv_len) < 0)
    {
        perror("sendto");

        return -1;
    }

    printf("RRQ sent\n");


    /*----------------------------------------
        IMPORTANT:
        Do NOT open/truncate local file yet.
    ----------------------------------------*/

    int fd = -1;


    /*----------------------------------------
                Receive DATA blocks
    ----------------------------------------*/

    while (1)
    {
        int attempts;
        int data_received = 0;


        for (attempts = 0;
             attempts < 3;
             attempts++)
        {
            int recv_len;

            socklen_t temp_len = serv_len;


            recv_len = recvfrom(sock_fd,
                                data_packet,
                                sizeof(data_packet),
                                0,
                                (struct sockaddr *)serv_addr,
                                &temp_len);


            /*--------------------------------
                    Timeout
            --------------------------------*/

            if (recv_len < 0)
            {
                if (errno == EAGAIN ||
                    errno == EWOULDBLOCK)
                {
                    printf("Timeout waiting for DATA block %d\n",
                           block);

                    /*
                     * Retransmit RRQ only while
                     * waiting for the first DATA.
                     */

                    if (block == 1)
                    {
                        sendto(sock_fd,
                               request,
                               request_len,
                               0,
                               (struct sockaddr *)serv_addr,
                               serv_len);

                        printf("RRQ retransmitted\n");
                    }

                    continue;
                }

                perror("recvfrom");

                if (fd >= 0)
                    close(fd);

                return -1;
            }


            /*--------------------------------
                    Check minimum size
            --------------------------------*/

            if (recv_len < 4)
            {
                printf("Invalid TFTP packet\n");
                continue;
            }


            /*--------------------------------
                    Extract opcode
            --------------------------------*/

            unsigned short received_opcode;

            memcpy(&received_opcode,
                   data_packet,
                   2);

            received_opcode = ntohs(received_opcode);


            /*--------------------------------
                    ERROR packet
            --------------------------------*/

            if (received_opcode == ERROR)
            {
                handle_error_packet(data_packet,
                                    recv_len);

                if (fd >= 0)
                    close(fd);

                return -1;
            }


            /*--------------------------------
                    Check DATA opcode
            --------------------------------*/

            if (received_opcode != DATA)
            {
                printf("Expected DATA packet\n");
                continue;
            }


            /*--------------------------------
                    Extract block number
            --------------------------------*/

            unsigned short data_block;

            memcpy(&data_block,
                   data_packet + 2,
                   2);

            data_block = ntohs(data_block);


            /*--------------------------------
                    Correct block
            --------------------------------*/

            if (data_block == block)
            {
                int data_size;

                data_size = recv_len - 4;


                /*--------------------------------
                    OPEN LOCAL FILE ONLY NOW
                --------------------------------*/

                if (fd < 0)
                {
                    fd = open(filename,
                              O_WRONLY | O_CREAT | O_TRUNC,
                              0666);

                    if (fd < 0)
                    {
                        perror("open");

                        return -1;
                    }

                    printf("Local file opened successfully\n");
                }


                /*--------------------------------
                        Write DATA
                --------------------------------*/

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

                        close(fd);

                        return -1;
                    }

                    total_written += bytes_written;
                }


                printf("DATA block %d received\n",
                       block);


                /*--------------------------------
                        Send ACK
                --------------------------------*/

                if (send_ack(sock_fd,
                             serv_addr,
                             serv_len,
                             block) < 0)
                {
                    close(fd);

                    return -1;
                }

                printf("ACK block %d sent\n",
                       block);


                data_received = 1;


                /*--------------------------------
                        Last block?
                --------------------------------*/

                if (data_size < BLOCK_SIZE)
                {
                    printf("\nDownload completed\n");

                    close(fd);

                    return 0;
                }


                /* Next block */

                block++;

                break;
            }


            /*--------------------------------
                    Duplicate DATA
            --------------------------------*/

            else if (data_block < block)
            {
                printf("Duplicate DATA block %d received\n",
                       data_block);

                send_ack(sock_fd,
                         serv_addr,
                         serv_len,
                         data_block);

                printf("ACK block %d retransmitted\n",
                       data_block);
            }
        }


        /*--------------------------------
                Maximum attempts
        --------------------------------*/

        if (!data_received)
        {
            printf("Maximum retransmission attempts reached\n");

            if (fd >= 0)
                close(fd);

            return -1;
        }
    }
}


/*========================================================
                    WRQ - UPLOAD
========================================================*/

int upload_file(int sock_fd,
                struct sockaddr_in *serv_addr,
                socklen_t serv_len,
                char *filename)
{
    int fd;

    char request[516];

    int request_len;

    char ack[4];

    unsigned short block = 0;

    struct timeval timeout;


    printf("\n========== WRQ ==========\n");
    printf("Uploading: %s\n", filename);


    /*----------------------------------------
                Open local file
    ----------------------------------------*/

    fd = open(filename,
              O_RDONLY);

    if (fd < 0)
    {
        perror("open");

        return -1;
    }


    /*----------------------------------------
                Create WRQ
    ----------------------------------------*/

    unsigned short opcode_net;

    opcode_net = htons(WRQ);

    memcpy(request,
           &opcode_net,
           2);

    strcpy(request + 2,
           filename);

    strcpy(request + 2 + strlen(filename) + 1,
           "octet");

    request_len = 2
                + strlen(filename)
                + 1
                + strlen("octet")
                + 1;


    /*----------------------------------------
                Set timeout
    ----------------------------------------*/

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

        return -1;
    }


    /*----------------------------------------
                Send WRQ
    ----------------------------------------*/

    if (sendto(sock_fd,
               request,
               request_len,
               0,
               (struct sockaddr *)serv_addr,
               serv_len) < 0)
    {
        perror("sendto");

        close(fd);

        return -1;
    }

    printf("WRQ sent\n");


    /*----------------------------------------
                Receive ACK 0
    ----------------------------------------*/

    int ack_received = 0;

    int attempts;


    for (attempts = 0;
         attempts < 3;
         attempts++)
    {
        int ack_len;

        socklen_t temp_len = serv_len;


        ack_len = recvfrom(sock_fd,
                           ack,
                           sizeof(ack),
                           0,
                           (struct sockaddr *)serv_addr,
                           &temp_len);


        if (ack_len < 0)
        {
            if (errno == EAGAIN ||
                errno == EWOULDBLOCK)
            {
                printf("Timeout waiting for ACK 0\n");

                /* Resend WRQ */

                sendto(sock_fd,
                       request,
                       request_len,
                       0,
                       (struct sockaddr *)serv_addr,
                       serv_len);

                printf("WRQ retransmitted\n");

                continue;
            }

            perror("recvfrom");

            close(fd);

            return -1;
        }


        /* Check ERROR */

        if (ack_len >= 4)
        {
            unsigned short received_opcode;

            memcpy(&received_opcode,
                   ack,
                   2);

            received_opcode = ntohs(received_opcode);

            if (received_opcode == ERROR)
            {
                handle_error_packet(ack,
                                    ack_len);

                close(fd);

                return -1;
            }
        }


        /* Validate ACK */

        if (ack_len != 4)
        {
            printf("Invalid ACK packet\n");
            continue;
        }


        unsigned short ack_opcode;
        unsigned short ack_block;


        memcpy(&ack_opcode,
               ack,
               2);

        memcpy(&ack_block,
               ack + 2,
               2);


        ack_opcode = ntohs(ack_opcode);
        ack_block = ntohs(ack_block);


        if (ack_opcode == ACK &&
            ack_block == 0)
        {
            printf("ACK block 0 received\n");

            ack_received = 1;

            break;
        }
    }


    if (!ack_received)
    {
        printf("Maximum retransmission attempts reached\n");

        close(fd);

        return -1;
    }


    /*----------------------------------------
                Send DATA blocks
    ----------------------------------------*/

    block = 1;


    while (1)
    {
        char data[BLOCK_SIZE];

        char data_packet[PACKET_SIZE];

        int bytes_read;


        /*--------------------------------
                Read file
        --------------------------------*/

        bytes_read = read(fd,
                          data,
                          sizeof(data));

        if (bytes_read < 0)
        {
            perror("read");

            close(fd);

            return -1;
        }


        /*--------------------------------
                Create DATA packet
        --------------------------------*/

        unsigned short data_opcode;
        unsigned short block_net;


        data_opcode = htons(DATA);
        block_net = htons(block);


        memcpy(data_packet,
               &data_opcode,
               2);

        memcpy(data_packet + 2,
               &block_net,
               2);


        if (bytes_read > 0)
        {
            memcpy(data_packet + 4,
                   data,
                   bytes_read);
        }


        int packet_len;

        packet_len = 4 + bytes_read;


        /*--------------------------------
                Send DATA and wait ACK
        --------------------------------*/

        ack_received = 0;


        for (attempts = 0;
             attempts < 3;
             attempts++)
        {
            int sent_len;


            /* Send DATA */

            sent_len = sendto(sock_fd,
                              data_packet,
                              packet_len,
                              0,
                              (struct sockaddr *)serv_addr,
                              serv_len);


            if (sent_len < 0)
            {
                perror("sendto");

                close(fd);

                return -1;
            }


            printf("DATA block %d sent (attempt %d)\n",
                   block,
                   attempts + 1);


            /* Receive ACK */

            int ack_len;

            socklen_t temp_len = serv_len;


            ack_len = recvfrom(sock_fd,
                               ack,
                               sizeof(ack),
                               0,
                               (struct sockaddr *)serv_addr,
                               &temp_len);


            /* Timeout */

            if (ack_len < 0)
            {
                if (errno == EAGAIN ||
                    errno == EWOULDBLOCK)
                {
                    printf("Timeout waiting for ACK block %d\n",
                           block);

                    continue;
                }

                perror("recvfrom");

                close(fd);

                return -1;
            }


            /* ERROR packet */

            if (ack_len >= 4)
            {
                unsigned short received_opcode;

                memcpy(&received_opcode,
                       ack,
                       2);

                received_opcode = ntohs(received_opcode);

                if (received_opcode == ERROR)
                {
                    handle_error_packet(ack,
                                        ack_len);

                    close(fd);

                    return -1;
                }
            }


            /* Validate ACK */

            if (ack_len != 4)
            {
                printf("Invalid ACK packet\n");
                continue;
            }


            unsigned short received_ack_opcode;
            unsigned short received_ack_block;


            memcpy(&received_ack_opcode,
                   ack,
                   2);

            memcpy(&received_ack_block,
                   ack + 2,
                   2);


            received_ack_opcode =
                ntohs(received_ack_opcode);

            received_ack_block =
                ntohs(received_ack_block);


            /* Correct ACK */

            if (received_ack_opcode == ACK &&
                received_ack_block == block)
            {
                printf("ACK block %d received\n",
                       block);

                ack_received = 1;

                break;
            }


            /* Duplicate ACK */

            printf("Unexpected ACK block %d\n",
                   received_ack_block);
        }


        /* Maximum attempts */

        if (!ack_received)
        {
            printf("Maximum retransmission attempts reached\n");

            close(fd);

            return -1;
        }


        /*--------------------------------
                Last DATA block?
        --------------------------------*/

        if (bytes_read < BLOCK_SIZE)
        {
            printf("\nUpload completed\n");

            close(fd);

            return 0;
        }


        block++;
    }
}


/*========================================================
                    SEND ACK
========================================================*/

int send_ack(int sock_fd,
             struct sockaddr_in *serv_addr,
             socklen_t serv_len,
             unsigned short block)
{
    char ack[4];

    unsigned short opcode;
    unsigned short block_net;


    opcode = htons(ACK);
    block_net = htons(block);


    memcpy(ack,
           &opcode,
           2);

    memcpy(ack + 2,
           &block_net,
           2);


    if (sendto(sock_fd,
               ack,
               sizeof(ack),
               0,
               (struct sockaddr *)serv_addr,
               serv_len) < 0)
    {
        perror("sendto");

        return -1;
    }


    return 0;
}


/*========================================================
                    MAIN
========================================================*/

int main(int argc, char *argv[])
{
    int sock_fd;

    struct sockaddr_in serv_addr;

    socklen_t serv_len;


    /*----------------------------------------
                Check arguments
    ----------------------------------------*/

    if (argc != 4)
    {
        printf("\nUsage:\n");

        printf("Download:\n");
        printf("%s get <server-ip> <filename>\n",
               argv[0]);

        printf("\nUpload:\n");
        printf("%s put <server-ip> <filename>\n",
               argv[0]);

        return 1;
    }


    /*----------------------------------------
                Create socket
    ----------------------------------------*/

    sock_fd = socket(AF_INET,
                     SOCK_DGRAM,
                     0);

    if (sock_fd < 0)
    {
        perror("socket");

        return 1;
    }


    /*----------------------------------------
                Initialize server address
    ----------------------------------------*/

    memset(&serv_addr,
           0,
           sizeof(serv_addr));


    serv_addr.sin_family = AF_INET;

    serv_addr.sin_port =
        htons(SERVER_PORT);


    /*----------------------------------------
                Convert IP address
    ----------------------------------------*/

    if (inet_pton(AF_INET,
                  argv[2],
                  &serv_addr.sin_addr) <= 0)
    {
        printf("Invalid server IP address\n");

        close(sock_fd);

        return 1;
    }


    serv_len = sizeof(serv_addr);


    /*----------------------------------------
                GET
    ----------------------------------------*/

    if (strcmp(argv[1], "get") == 0)
    {
        if (download_file(sock_fd,
                          &serv_addr,
                          serv_len,
                          argv[3]) < 0)
        {
            printf("Download failed\n");

            close(sock_fd);

            return 1;
        }
    }


    /*----------------------------------------
                PUT
    ----------------------------------------*/

    else if (strcmp(argv[1], "put") == 0)
    {
        if (upload_file(sock_fd,
                        &serv_addr,
                        serv_len,
                        argv[3]) < 0)
        {
            printf("Upload failed\n");

            close(sock_fd);

            return 1;
        }
    }


    /*----------------------------------------
                Invalid command
    ----------------------------------------*/

    else
    {
        printf("Invalid command: %s\n",
               argv[1]);

        printf("Use 'get' or 'put'\n");

        close(sock_fd);

        return 1;
    }


    /*----------------------------------------
                Cleanup
    ----------------------------------------*/

    close(sock_fd);

    return 0;
}
