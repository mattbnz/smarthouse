// Signaller
// 
// Copyright (C) 2016 - Matt Brown
//
// All rights reserved.
//
// Simple TCP socket -> serial port bridge with a basic level of sanity
// filtering for safety.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>
#include <syslog.h>
#include <termios.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

void Accept(int listenfd, int *client, int *maxfd,
            int *max_client, fd_set *rfds) {

    struct sockaddr_in cliaddr;
    socklen_t clilen = sizeof(cliaddr);
    int connfd = accept(listenfd, (struct sockaddr *)&cliaddr,
                         &clilen);
    if (connfd < 0) {
        syslog(LOG_ERR, "Failed to accept client");
        return;
    }
    char *ip_str = inet_ntoa(cliaddr.sin_addr);
    if (ip_str == NULL) {
        syslog(LOG_ERR, "inet_ntoa failed on new client");
        close(connfd);
        return;
    }
    syslog(LOG_INFO, "New connection from %s:%d on fd %d", ip_str,
           ntohs(cliaddr.sin_port), connfd);
    // Find open slot for client add to listening set
    int i;
    for (i=0; i<FD_SETSIZE; i++) {
        if (client[i] < 0) {
            client[i] = connfd;
            break;
        }
    }
    if (i == FD_SETSIZE) {
        syslog(LOG_ERR, "No space for new client!");
        close(connfd);
        return;
    }
    FD_SET(connfd, rfds);
    if (connfd > *maxfd)
        *maxfd = connfd;
    if (i > *max_client)
        *max_client = i;
}

int main(int argc, char **argv) {
    printf("hi");
    openlog("signaller", LOG_CONS | LOG_PID, LOG_DAEMON);
    if (argc != 3) {
        fprintf(stdout, "Usage: %s SERIAL_PORT LISTEN_PORT\n", argv[0]);
        return 1;
    }
    syslog(LOG_INFO, "Started");

    /* Open the serial port and set to 115200 */
    int serialfd = open(argv[1], O_RDWR | O_NOCTTY);
    if (serialfd == -1) {
        syslog(LOG_CRIT, "Unable to open serial port %s", argv[1]);
        return 1;
    }
    struct termios options;
    tcgetattr(serialfd, &options);
    cfsetispeed(&options, B115200);
    cfsetospeed(&options, B115200);
    options.c_cflag |= (CLOCAL | CREAD);
    tcsetattr(serialfd, TCSANOW, &options);

    /* Open a listening TCP socket, bind to the port and start listening */
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) {
        syslog(LOG_CRIT, "Could not open listening socket!");
        return 1;
    }
    int optval = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, 
               (const void *)&optval , sizeof(int));
    struct sockaddr_in listenaddr;
    bzero((char *)&listenaddr, sizeof(listenaddr));
    listenaddr.sin_family = AF_INET;
    listenaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    listenaddr.sin_port = htons((unsigned short)atoi(argv[2]));
    if (bind(listenfd, (struct sockaddr *)&listenaddr,
              sizeof(listenaddr)) < 0) {
        syslog(LOG_CRIT, "Could not bind to port %s", argv[2]);
        return 1;
    }
    if (listen(listenfd, 2) < 0) {
        syslog(LOG_CRIT, "Could not listen!");
        return 1;
    }

    // Loop reading the TCP port, validate any input and then echo it 
    // to the serial port.
    int client[FD_SETSIZE];
    int max_client, maxfd;

    for (int i = 0; i < FD_SETSIZE; i++) {
        client[i] = -1;
    }

    fd_set rfds, readyfds;
    FD_ZERO(&rfds);
    FD_SET(serialfd, &rfds);
    FD_SET(listenfd, &rfds);

    maxfd = listenfd;
    max_client = -1;

    int rv;
    char line[1024];
    char *linep = (char *)&line;
    char buf[1024];
    syslog(LOG_INFO, "Entering main read loop");
    while (1) {
        readyfds = rfds;
        rv = select(maxfd+1, &readyfds, NULL, NULL, NULL);
        if (!rv) {
            continue;
        }
        if (FD_ISSET(listenfd, &readyfds)) {
            // New client, accept and welcome
            Accept(listenfd, client, &maxfd, &max_client, &rfds);
        }
        if (FD_ISSET(serialfd, &readyfds)) {
            // Data ready on serial port, copy to clients
            char buf[1024] = {'\0'};
            int n;
            n = read(serialfd, &buf, sizeof(buf));
            if (n > 0) {
                syslog(LOG_INFO, "Copying %d bytes from serial to clients.",
                        n);
                for (int i=0; i<=max_client; i++) {
                    if (client[i] < 0)
                        continue;
                    write(client[i], buf, n);
                }
                syslog(LOG_INFO, "Serial says: %s", buf);
            }
        }
        // Check clients for data.
        for (int i=0; i<=max_client; i++) {
            int sockfd = client[i];
            if (sockfd < 0) {
                continue;
            }
            if (FD_ISSET(sockfd, &readyfds)) {
                // Data from client, copy to serial
                char buf[1024] = {'\0'};
                int n;
                n = read(sockfd, &buf, sizeof(buf));
                if (n > 0) {
                    syslog(LOG_INFO, "Copying %d bytes from %d to serial.",
                            n, i);
                    syslog(LOG_INFO, "Client %d says: %s", i, buf);
                    write(serialfd, buf, n);
                } else if (n == 0) {
                    syslog(LOG_INFO, "Client %d on fd %d disconnected.",
                           i, sockfd);
                    // Client disconnected.
                    close(sockfd);
                    FD_CLR(sockfd, &rfds);
                    client[i] = -1;
                }
            }
        }
    }
    close(serialfd);
    close(listenfd);
    syslog(LOG_INFO, "Exiting");
}
