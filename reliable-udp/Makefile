all: client server

client: client.cpp ikcp.c test.h common.h 
	g++ client.cpp -o client -std=c++14 -g

server: server.cpp ikcp.c test.h common.h 
	g++ server.cpp -o server -std=c++14 -g

clean: 
	-rm -f client
	-rm -f server
