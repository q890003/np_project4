all: 	socks_server.cpp
	g++ -o socks_server socks_server.cpp -std=c++14 -Wall -pedantic -pthread -lboost_system
	g++ -o hw4.cgi console.cpp -std=c++14 -Wall -pedantic -pthread -lboost_system