#define SERVER_PORT 8888    //The port on which to listen for incoming data
#define SERVER_ADDR "192.168.122.11"

#define CLIENT_PORT 9999
#define CLIENT_ADDR "192.168.122.12"

#define MSG_SIZE 2000
#define BUFLEN 4096  //Max length of buffer

#define WND_SIZE 128

// KCP_MODE = 0 fast
// KCP_MODE > 0 ordinary
#define KCP_MODE 0

// G: 1073741824
// M: 1048576
// K: 1024
static constexpr long max_size = 1048576*10;