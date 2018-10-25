#include <iostream>

#include<arpa/inet.h>
#include<sys/socket.h>
#include <sys/ioctl.h>
#include<stdlib.h>
#include<stdio.h> 
#include<string.h> 
#include <fcntl.h>

#include "ikcp.c"
#include "test.h"
#include "common.h"

static struct sockaddr_in si_server;
static struct sockaddr_in si_client;
static socklen_t slen;

// The kcp_output callback.
int udp_output(const char *buf, int len, ikcpcb *kcp, void *user) {
    // user is the socket id.
    int s = reinterpret_cast<long>(user);
    
    int res = sendto(s, buf, len, 0, (struct sockaddr*) &si_client, slen);
    if(res != len) {
        printf("Error sending data out.\n");
        exit(-1);
    }

    return 0;
}

int main(int argc, char **argv) {
    if(BUFLEN < MSG_SIZE) {
        printf("Invalid buffer size.\n");
        exit(-1);
    }

    // Define local variables.
    struct sockaddr_in si_received;
    int s, i, recv_len;
    char buf[BUFLEN];
    slen = sizeof(si_server);

    // Create the UDP socket.
    if ((s=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
        printf("Fail to create a UDP socket.\n");
        exit(-1);
    }
    // Make UDP socket non-blocking.
    int flags = fcntl(s,F_GETFL);
    flags |= O_NONBLOCK;
    fcntl(s, F_SETFL, flags);

    // Setup the server address.
    memset((char *) &si_server, 0, sizeof(si_server));
    si_server.sin_family = AF_INET;
    si_server.sin_port = htons(SERVER_PORT);
    if (inet_aton(SERVER_ADDR , &si_server.sin_addr) == 0) {
        printf("server inet_aton() failed\n");
        exit(-1);
    }
    // Bind the socekt to the server address.
    if( bind(s , (struct sockaddr*)&si_server, sizeof(si_server) ) == -1) {
        printf("Fail to bind the address to the socket\n");
        exit(-1);
    }

    // Set up the client address.
    memset((char *) &si_client, 0, sizeof(si_client));
    si_client.sin_family = AF_INET;
    si_client.sin_port = htons(CLIENT_PORT);
    if (inet_aton(CLIENT_ADDR , &si_client.sin_addr) == 0) {
        printf("client inet_aton() failed\n");
        exit(-1);
    }
    
    // Create server_kcp
    long ls = s; 
    ikcpcb* server_kcp = ikcp_create(0x11223344, reinterpret_cast<void*>(ls));
    server_kcp->output = udp_output;
    ikcp_wndsize(server_kcp, WND_SIZE, WND_SIZE);
    
    if(KCP_MODE) {
        // KCP ordinary
        ikcp_nodelay(server_kcp, 0, 40, 0, 0);
    }
    else {
        // KCP fast.
        ikcp_nodelay(server_kcp, 1, 10, 2, 1);
    }

    // Define local variables.
    IUINT32 current_time = iclock();
    IUINT32 previous_time = current_time;
    int hr;
    long current_bytes = 0;
    long previous_bytes = 0;

    while (1) {
        // Sleep for 1ms.
        // isleep(1);
        current_time = iclock();

        // Update the kcp control block, send pending packets out.
        ikcp_update(server_kcp, current_time);

        // Receive packet from the UDP socket.
        recv_len = recvfrom(s, buf, BUFLEN, 0, (struct sockaddr *) &si_received, &slen);
        if(recv_len == -1) {
            if(errno != EAGAIN) {
                break;
            }
        }
        
        // Receive new packet.
        if(recv_len > 0) {
            // Check whether the packet comes from the client.
            if(si_received.sin_addr.s_addr != si_client.sin_addr.s_addr && 
               si_received.sin_family != si_client.sin_family &&
               si_received.sin_port != si_client.sin_port) {
                printf("Received packet does not come from client.\n");
                continue;
            }
            // Send the packet to kcp control block for processing.
            ikcp_input(server_kcp, buf, recv_len);
        }

        // Try to get some application-level payload from kcp control block.
        hr = ikcp_recv(server_kcp, buf, BUFLEN);
        
        if(hr >= 0) {
            current_bytes += hr;
        }

        if(current_time - previous_time >= 1000) {
            // 1 second has passed, print statistics.
            double total = (double)current_bytes - (double)previous_bytes;
            
            previous_bytes = current_bytes;
            previous_time = current_time;
            
            total = total / 1024.0 / 1024.0;

            printf("Server-side throughput: %.2fMB/s\n", total);
        }
    }

    // If we come here, there must be a recvfrom error.
    printf("UDP socket error.\n");
    exit(-1);
}