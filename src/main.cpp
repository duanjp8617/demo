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
#include <set>

#define NB_MBUF   8192
#define MEMPOOL_CACHE_SIZE 256
#define RTE_TEST_RX_DESC_DEFAULT 128
#define RTE_TEST_TX_DESC_DEFAULT 512

static uint16_t nb_rxd = RTE_TEST_RX_DESC_DEFAULT;
static uint16_t nb_txd = RTE_TEST_TX_DESC_DEFAULT;

static volatile bool force_quit;

struct rte_mempool * l2fwd_pktmbuf_pool = NULL;

static volatile uint32_t port_mask = 0;

static void
check_all_ports_link_status(uint16_t port_num, uint8_t* all_ports_up)
{
#define CHECK_INTERVAL 100 /* 100ms */
#define MAX_CHECK_TIME 90 /* 9s (90 * 100ms) in total */
	uint16_t portid;
	uint8_t count, print_flag = 0;
	struct rte_eth_link link;

	printf("\nChecking link status");
	fflush(stdout);
	for (count = 0; count <= MAX_CHECK_TIME; count++) {
		if (force_quit)
			return;
		*all_ports_up = 1;
		for (portid = 0; portid < port_num; portid++) {
			if (force_quit)
				return;
			if ((port_mask & (1 << portid)) == 0)
				continue;
			memset(&link, 0, sizeof(link));
			rte_eth_link_get_nowait(portid, &link);
			/* print link status if flag set */
			if (print_flag == 1) {
				if (link.link_status)
					printf(
					"Port %d Link Up. Speed %u Mbps - %s\n",
						portid, link.link_speed,
				(link.link_duplex == ETH_LINK_FULL_DUPLEX) ?
					("full-duplex") : ("half-duplex\n"));
				else
					printf("Port %d Link Down\n", portid);
				continue;
			}
			/* clear all_ports_up flag if any link down */
			if (link.link_status == ETH_LINK_DOWN) {
				*all_ports_up = 0;
				break;
			}
		}
		/* after finally printing all link status, get out */
		if (print_flag == 1)
			break;

		if (*all_ports_up == 0) {
			printf(".");
			fflush(stdout);
			rte_delay_ms(CHECK_INTERVAL);
		}

		/* set the print_flag if all ports up or timeout */
		if (*all_ports_up == 1 || count == (MAX_CHECK_TIME - 1)) {
			print_flag = 1;
			printf("done\n");
		}
	}
}

static void
signal_handler(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        printf("\n\nSignal %d received, preparing to exit...\n",
               signum);
        force_quit = true;
    }
}

static int
lsi_event_callback(uint16_t port_id, enum rte_eth_event_type type, void *param,
		    void *ret_param)
{
	struct rte_eth_link link;

	RTE_SET_USED(param);
	RTE_SET_USED(ret_param);

	// printf("\n\nIn registered callback...\n");
	// printf("Event type: %s\n", type == RTE_ETH_EVENT_INTR_LSC ? "LSC interrupt" : "unknown event");
	rte_eth_link_get_nowait(port_id, &link);
	if (link.link_status) {
		printf("Port %d Link Up - speed %u Mbps - %s\n\n",
				port_id, (unsigned)link.link_speed,
			(link.link_duplex == ETH_LINK_FULL_DUPLEX) ?
				("full-duplex") : ("half-duplex"));
		port_mask = port_mask | (1 << port_id);
	} else {
		port_mask = port_mask & (~(1 << port_id));
		printf("Port %d Link Down\n\n", port_id);
	}

	return 0;
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
                if(_ingress_side_port_id < 0) {
                		_ingress_side_port_id = -1;
                }
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

public:
    inline int ingress_side_port_id() {
    		return _ingress_side_port_id;
    }
    inline int egress_side_port_id_count() {
    		return _egress_side_port_id.size();
    }
    inline int egress_side_port_id(int index) {
    		return _egress_side_port_id.at(index);
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

void ingress_pipeline(demo_option_paser& opt_parser, std::vector<uint64_t>& counters) {

}

int main(int argc, char **argv) {
	struct rte_eth_conf port_conf;

	port_conf.rxmode.split_hdr_size = 0;
	port_conf.rxmode.header_split   = 0; /**< Header Split disabled */
	port_conf.rxmode.hw_ip_checksum = 0; /**< IP checksum offload disabled */
	port_conf.rxmode.hw_vlan_filter = 0; /**< VLAN filtering disabled */
	port_conf.rxmode.jumbo_frame    = 0; /**< Jumbo Frame Support disabled */
	port_conf.rxmode.hw_strip_crc   = 1; /**< CRC stripped by hardware */

	port_conf.txmode.mq_mode = ETH_MQ_TX_NONE;

	port_conf.intr_conf.lsc = 1;

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

	l2fwd_pktmbuf_pool = rte_pktmbuf_pool_create("mbuf_pool", NB_MBUF,
		MEMPOOL_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE,
		rte_socket_id());
	if (l2fwd_pktmbuf_pool == NULL)
		rte_exit(EXIT_FAILURE, "Cannot init mbuf pool\n");
	else
		std::cout << "Finish creating packet memory buffer pool." <<std::endl;

	int max_port_id = opt_parser.ingress_side_port_id();
	std::set<int> port_id_holder;
	port_id_holder.insert(max_port_id);
	int count = opt_parser.egress_side_port_id_count();
	for(int i=0; i<count; i++) {
		auto res = port_id_holder.insert(opt_parser.egress_side_port_id(i));
		if(res.second == false) {
			rte_exit(EXIT_FAILURE, "Invalid ingress/egress port id.\n");
		}
		if(opt_parser.egress_side_port_id(i) > max_port_id) {
			max_port_id = opt_parser.egress_side_port_id(i);
		}
	}

	uint16_t nb_ports = rte_eth_dev_count();
	if (nb_ports == 0)
		rte_exit(EXIT_FAILURE, "No Ethernet ports - bye\n");
	else
		std::cout<<(int)nb_ports<<" port available on this machine."<<std::endl;

	if(max_port_id >= nb_ports) {
		rte_exit(EXIT_FAILURE, "Invalid ingress/egress port id.\n");
	}

	port_mask = 0;
	for(auto port_id : port_id_holder) {
		port_mask = port_mask | (1 << port_id);
	}

	for(auto id : port_id_holder) {
		uint16_t portid = id;

		/* init port */
		printf("Initializing port %u... ", portid);
		fflush(stdout);
		ret = rte_eth_dev_configure(portid, 1, 1, &port_conf);
		if (ret < 0)
			rte_exit(EXIT_FAILURE, "Cannot configure device: err=%d, port=%u\n",
				  ret, portid);

		ret = rte_eth_dev_adjust_nb_rx_tx_desc(portid, &nb_rxd,
								       &nb_txd);
		if (ret < 0)
			rte_exit(EXIT_FAILURE,
				 "Cannot adjust number of descriptors: err=%d, port=%u\n",
				 ret, portid);

		/* register lsi interrupt callback, need to be after
		 * rte_eth_dev_configure(). if (intr_conf.lsc == 0), no
		 * lsc interrupt will be present, and below callback to
		 * be registered will never be called.
		 */
		rte_eth_dev_callback_register(portid,
			RTE_ETH_EVENT_INTR_LSC, lsi_event_callback, NULL);

		/* init one RX queue */
		fflush(stdout);
		ret = rte_eth_rx_queue_setup(portid, 0, nb_rxd,
						 rte_eth_dev_socket_id(portid),
						 NULL,
						 l2fwd_pktmbuf_pool);
		if (ret < 0)
			rte_exit(EXIT_FAILURE, "rte_eth_rx_queue_setup:err=%d, port=%u\n",
				  ret, portid);

		/* init one TX queue on each port */
		fflush(stdout);
		ret = rte_eth_tx_queue_setup(portid, 0, nb_txd,
				rte_eth_dev_socket_id(portid),
				NULL);
		if (ret < 0)
			rte_exit(EXIT_FAILURE, "rte_eth_tx_queue_setup:err=%d, port=%u\n",
				ret, portid);

		/* Start device */
		ret = rte_eth_dev_start(portid);
		if (ret < 0)
			rte_exit(EXIT_FAILURE, "rte_eth_dev_start:err=%d, port=%u\n",
				  ret, portid);

		printf("done. \n");
		rte_eth_promiscuous_enable(portid);
	}

	uint8_t all_ports_up = 0;
	check_all_ports_link_status(nb_ports, &all_ports_up);
	if(all_ports_up == 0) {
		rte_exit(EXIT_FAILURE, "Some links are down, exit.\n");
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
