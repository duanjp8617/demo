#include <rte_common.h>
#include <rte_log.h>
#include <rte_malloc.h>
#include <rte_memory.h>
#include <rte_memcpy.h>
#include <rte_eal.h>
#include <rte_launch.h>
#include <rte_atomic.h>
#include <rte_cycles.h>
#include <rte_prefetch.h>
#include <rte_lcore.h>
#include <rte_per_lcore.h>
#include <rte_branch_prediction.h>
#include <rte_interrupts.h>
#include <rte_random.h>
#include <rte_debug.h>
#include <rte_ether.h>
#include <rte_ethdev.h>
#include <rte_mempool.h>
#include <rte_mbuf.h>

#include <signal.h>
#include <getopt.h>

#include <string>
#include <vector>
#include <iostream>
#include <sstream>
#include <array>
#include <cstdint>

static volatile bool force_quit;

static void
signal_handler(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        printf("\n\nSignal %d received, preparing to exit...\n",
               signum);
        force_quit = true;
    }
}

class demo_option_paser {
public:
    demo_option_paser ()
    : _ingress_side_server_mac()
    , _ingress_mac()
    , _ingress_side_port_id(-1)
    , _egress_side_server_mac()
    , _egress_mac()
    , _egress_side_port_id() {
    }

    bool parser_init(int argc, char **argv) {
        bool succeed = true;

        for(int i=1; i<argc; i=i+2) {
            std::string command(argv[i]);
            if(i+1>=argc) {
                break;
            }
            std::string command_val(argv[i+1]);

            if (command == "--ingress-side-server-mac") {
                // The command_val should be have this format:
                // AA:BB:CC:DD:EE:FF,11:22:33:44:55.
                auto mac_addr_vec = split(command_val, ',');
                for(auto& v : mac_addr_vec) {
                    auto mac_byte_val_vec = split(v, ':');
                    if(mac_byte_val_vec.size() != 6) {
                        _ingress_side_server_mac.clear();
                        _ingress_mac.clear();
                        break;
                    }
                    _ingress_side_server_mac.push_back(v);
                    std::array<uint8_t, 6> arr;
                    for(int i=0; i<6; i++) {
                        arr[i] = std::stoi(mac_byte_val_vec[i], nullptr, 16);
                    }
                    _ingress_mac.push_back(std::move(arr));
                }
            }
            else if(command == "--ingress-side-port-id") {
                // The command_val should be an integer
                _ingress_side_port_id = std::stoi(command_val);
            }
            else if(command == "--egress-side-server-mac") {
                // The command_val should be have this format:
                // AA:BB:CC:DD:EE:FF,11:22:33:44:55.
                auto mac_addr_vec = split(command_val, ',');
                for(auto& v : mac_addr_vec) {
                    auto mac_byte_val_vec = split(v, ':');
                    if(mac_byte_val_vec.size() != 6) {
                        _egress_side_server_mac.clear();
                        _egress_mac.clear();
                        break;
                    }
                    _egress_side_server_mac.push_back(v);
                    std::array<uint8_t, 6> arr;
                    for(int i=0; i<6; i++) {
                        arr[i] = std::stoi(mac_byte_val_vec[i], nullptr, 16);
                    }
                    _egress_mac.push_back(std::move(arr));
                }
            }
            else if(command == "--egress-side-port-id") {
                // The command_val should have this format:
                // 12,2,3,4
                auto int_vec = split(command_val, ',');
                for(auto& i : int_vec) {
                    auto res = stoi(i);
                    if(res < 0) {
                        _egress_side_port_id.clear();
                        break;
                    }
                    _egress_side_port_id.push_back(res);
                }
            }
            else {
                break;
            }
        }

        if (_ingress_side_server_mac.size() == 0 || 
            _ingress_mac.size() == 0 || 
            _ingress_side_port_id == -1 || 
            _egress_side_server_mac.size() == 0 ||
            _egress_mac.size() == 0 ||
            _egress_side_port_id.size() == 0) {
            succeed = false;
        }

        if(succeed == false) {
            usage(argv[0]);
        }
        else {
            std::cout<<"Parsing succeed!"<<std::endl;

            std::cout<<"Ingress-side MAC addresses: ";
            for(auto& str : _ingress_side_server_mac) {
                std::cout<<str<<", ";
            }
            std::cout<<std::endl;

            std::cout<<"Ingress-side port id: "<<_ingress_side_port_id<<std::endl;

            std::cout<<"Egress-side MAC addresses: ";
            for(auto& str : _egress_side_server_mac) {
                std::cout<<str<<", ";
            }
            std::cout<<std::endl;

            std::cout<<"Egress-side port ids: ";
            for(auto id : _egress_side_port_id) {
                std::cout<<id<<", ";
            }
            std::cout<<std::endl;

            for(auto& arr : _ingress_mac) {
                for(auto v : arr) {
                    std::cout << ((int)v) << ", ";
                }
                std::cout<<std::endl;
            }

            for(auto& arr : _egress_mac) {
                for(auto v : arr) {
                    std::cout << ((int)v) << ", ";
                }
                std::cout<<std::endl;
            }
        }

        return succeed;
    }

private:
     void usage(const char *prgname) {
        printf("%s [EAL options] -- --ingress-side-server-mac MACLIST --ingress-side-port-id PORTNUM \n"
               "--egress-side-server-mac MACLIST --egress-side-port-id PORTNUMLIST\n"
               "  --ingress-side-server-mac MACLIS: Mac addresses of ingress-side servers, separated by colon\n"
               "  --ingress-side-port-id PORTNUM: Ingress-side port number\n"
               "  --egress-side-server-mac MACLIS: Mac addresses of egress-side servers, separated by colon\n"
               "  --egress-side-port-id PORTNUMLIST: A list of egress-side port number, separated by colon\n",
            prgname);
    }

    std::vector<std::string> split(std::string& str, char delimiter) {
        std::vector<std::string> vec(0);
        std::stringstream ss(str);
        std::string item;
        while(std::getline(ss, item, delimiter)){
            if(!item.empty()) {
                vec.push_back(item);
            }
        }
        return vec;    
    }

private:
    std::vector<std::string> _ingress_side_server_mac;
    std::vector<std::array<uint8_t, 6>> _ingress_mac;
    int _ingress_side_port_id;
    std::vector<std::string> _egress_side_server_mac;
    std::vector<std::array<uint8_t, 6>> _egress_mac;
    std::vector<int> _egress_side_port_id;
};

int main(int argc, char **argv) {

    /* init EAL */
    int ret = rte_eal_init(argc, argv);
    if (ret < 0)
        rte_exit(EXIT_FAILURE, "Invalid EAL arguments\n");
    argc -= ret;
    argv += ret;

    /* set up the signal handler */
    force_quit = false;
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    demo_option_paser opt_parser;
    bool succeed = opt_parser.parser_init(argc, argv);
    if(succeed == false) {
        rte_exit(EXIT_FAILURE, "Invalid demo arguments\n");
    }
}

// Processs input arguments
// ....
// ingress-side-server-mac = xx:xx:xx, xx:xx:xx,
// ingress-side-port-id = 0, 1, 2
// egress-side-server-mac = xx:xx:xx, xx:xx:xx
// egress-side-port-id = 0,1,2

// RTE initialization

// launch worker thread, but only keep one worker thread alive for printing

// Enter the main worker function

// void main_worker_function (...) {
// A big loop {
// Poll the ingress port, feed the traffic into ingress pipeline
// Poll each of the egress port, feed the traffic into egress pipeline
// }
// }

// void ingress_pipeline (...) {
// For all the packets
// Parse the packet
// If the packet is arp, do nothing
// If the packet's source mac address does not match ingress-side-server-mac, drop
// If the packet's source mac address matches ingress-side-server-mac, modify the source mac into an id and a counter

// Send the packet batch out from each of the port.
// Keep the maximum number of packets that are sent.
// Increase the counter according to the maximum number of packets that are sent.
// Free unsent packets.

// }

// void egress_pipeline () {

// }
