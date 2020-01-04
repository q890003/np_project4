all: 	socks4_server.cpp
	g++ -o socks4_server socks4_server.cpp -std=c++14 -Wall -pedantic -pthread -lboost_system
