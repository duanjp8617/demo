#include <iostream>

#include<arpa/inet.h>
#include<sys/socket.h>
#include <sys/ioctl.h>
#include<stdlib.h> //exit(0);
#include<stdio.h> //printf
#include<string.h> //memset
#include <fcntl.h>

#include "ikcp.c"
#include "test.h"
#include "common.h"

#define BUFLEN 4096  //Max length of buffer

static struct sockaddr_in si_server;
static struct sockaddr_in si_client;
static socklen_t slen;


int udp_output(const char *buf, int len, ikcpcb *kcp, void *user) {
    int s = reinterpret_cast<long>(user);
    int res = sendto(s, buf, len, 0, (struct sockaddr*) &si_server, slen);
    if(res != len) {
        printf("Error sending data out.\n");
        exit(-1);
    }
    printf("Client sends packet with length %d\n", len);

    return 0;
}

int main(int argc, char **argv) {
    // Create UDP socket
    struct sockaddr_in si_received;
    int s, i, recv_len;
    char buf[BUFLEN];
    slen = sizeof(si_server);

    if ((s=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
        printf("Fail to create a UDP socket.\n");
        exit(-1);
    }
    int flags = fcntl(s,F_GETFL);
    flags |= O_NONBLOCK;
    fcntl(s, F_SETFL, flags);

    memset((char *) &si_server, 0, sizeof(si_server));
    si_server.sin_family = AF_INET;
    si_server.sin_port = htons(SERVER_PORT);
    if (inet_aton(SERVER_ADDR , &si_server.sin_addr) == 0) {
        printf("server inet_aton() failed\n");
        exit(-1);
    }

    memset((char *) &si_client, 0, sizeof(si_client));
    si_client.sin_family = AF_INET;
    si_client.sin_port = htons(CLIENT_PORT);
    if (inet_aton(CLIENT_ADDR , &si_client.sin_addr) == 0) {
        printf("client inet_aton() failed\n");
        exit(-1);
    }
    if( bind(s , (struct sockaddr*)&si_client, sizeof(si_client) ) == -1) {
        printf("Fail to bind the address to the socket\n");
        exit(-1);
    }
    
    // Create client_kcp
    long ls = s; 
    ikcpcb* client_kcp = ikcp_create(0x11223344, reinterpret_cast<void*>(ls));
    client_kcp->output = udp_output;
    ikcp_wndsize(client_kcp, 128, 128);
    ikcp_nodelay(client_kcp, 1, 10, 2, 1);

    IUINT32 current = iclock();
    int hr;
    int total = 0;

    int err_code = 0;
    while (1) {
        isleep(1);
        current = iclock();
        ikcp_update(client_kcp, iclock());

        if(total < MAX_SIZE) {
            int qsize = ikcp_waitsnd(client_kcp);
            if(qsize < 2*128) {
                hr = ikcp_send(client_kcp, buf, 1024);
                if(hr < 0) {
                    err_code = -1; 
                    break;
                }
                total += 1024;
            }
        }
        else {
            if(ikcp_waitsnd(client_kcp) == 0) {
                break;
            }
            printf("Size of waitsnd %d\n", ikcp_waitsnd(client_kcp));
        }

        recv_len = recvfrom(s, buf, BUFLEN, 0, (struct sockaddr *) &si_received, &slen);
        if(recv_len == -1) {
            if(errno != EAGAIN) {
                err_code = -1;
                break;
            }
        }

        if(recv_len >= 0) {
            if(si_received.sin_addr.s_addr != si_server.sin_addr.s_addr && 
                si_received.sin_family != si_server.sin_family &&
                si_received.sin_port != si_server.sin_port) {
                continue;
            }
            printf("Client receives packet with length %d\n", recv_len);
            ikcp_input(client_kcp, buf, recv_len);
        }
    }

    if(err_code == 0) {
        std::cout<<"Done!"<<std::endl;
    }
    else {
        std::cout<<"Transmission Error!"<<std::endl;
    }

}