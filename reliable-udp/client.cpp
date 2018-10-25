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
    
    sendto(s, buf, len, 0, (struct sockaddr*) &si_server, slen);

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

    // Set up the client address.
    memset((char *) &si_client, 0, sizeof(si_client));
    si_client.sin_family = AF_INET;
    si_client.sin_port = htons(CLIENT_PORT);
    if (inet_aton(CLIENT_ADDR , &si_client.sin_addr) == 0) {
        printf("client inet_aton() failed\n");
        exit(-1);
    }
    // Bind the socekt to the server address.
    if( bind(s , (struct sockaddr*)&si_client, sizeof(si_client) ) == -1) {
        printf("Fail to bind the address to the socket\n");
        exit(-1);
    }
    
    // Create client_kcp
    long ls = s; 
    ikcpcb* client_kcp = ikcp_create(0x11223344, reinterpret_cast<void*>(ls));
    client_kcp->output = udp_output;
    ikcp_wndsize(client_kcp, WND_SIZE, WND_SIZE*2);
    
    if(KCP_MODE) {
        // KCP ordinary
        ikcp_nodelay(client_kcp, 0, 40, 0, 0);
    }
    else {
        // KCP fast.
        ikcp_nodelay(client_kcp, 1, 10, 2, 1);
    }

    // Define local variables.
    IUINT32 current_time = iclock();
    IUINT32 previous_time = current_time;
    int err_code = 0;
    long current_bytes = 0;
    long previous_bytes = 0;

    while (1) {
        // Sleep for 1ms.
        // isleep(1);
        // usleep(50);
        current_time = iclock();

        if(ikcp_check(client_kcp, current_time) == current_time) {
            // Update the kcp control block, send pending packets out.
            ikcp_update(client_kcp, current_time);
        }

        // Keep calling recvfrom until the socket says EAGAIN.
        recv_len = 0;
        while(recv_len >= 0) {
            // Receive packet from the UDP socket.
            recv_len = recvfrom(s, buf, BUFLEN, 0, (struct sockaddr *) &si_received, &slen);
            if(recv_len == -1 && errno != EAGAIN) {
                err_code = -1;
            }

            // Receive new packet.
            if(recv_len >= 0) {
                // Check whether the packet comes from the server.
                if(si_received.sin_addr.s_addr != si_server.sin_addr.s_addr && 
                   si_received.sin_family != si_server.sin_family &&
                   si_received.sin_port != si_server.sin_port) {
                    printf("Received packet does not come from server.\n");
                    continue;
                }
                // Send the packet to kcp control block for processing.
                ikcp_input(client_kcp, buf, recv_len);

                // Keep pulling the received message from the receive buffer.
                int hr = 0;
                while(hr >= 0) {
                    // Try to get some application-level payload from kcp control block.
                    hr = ikcp_recv(client_kcp, buf, BUFLEN);
                    
                    if(hr >= 0) {
                        current_bytes += hr;
                    }
                }
            }
        }

        if(current_bytes < max_size) {
            while(ikcp_waitsnd(client_kcp) < WND_SIZE) {
                int hr = ikcp_send(client_kcp, buf, MSG_SIZE);
                if(hr < 0) {
                    err_code = -1; 
                    break;
                }
                current_bytes += MSG_SIZE;
            }
        }
        else {
            if(ikcp_waitsnd(client_kcp) == 0) {
                break;
            }
        }

        if(current_time - previous_time >= 1000) {
            // 1 second has passed, print statistics.
            double total = (double)current_bytes - (double)previous_bytes;
            
            previous_bytes = current_bytes;
            previous_time = current_time;
            
            total = total / 1024.0 / 1024.0;
            double left = (max_size - current_bytes)/1024.0/1024.0;
            printf("Client-side throughput: %.2fMB/s, %.2fMB left\n", total, left);
        }

        if(err_code == -1) {
            break;
        }
    }

    if(err_code == 0) {
        std::cout<<"Done!"<<std::endl;
    }
    else {
        std::cout<<"Transmission Error!"<<std::endl;
    }

}