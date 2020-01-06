#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/format.hpp>
#include <cstdlib>
#include <iostream>
#include <string>
#include <sys/types.h>
#include <map>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <boost/algorithm/string.hpp>
#include "socks4.hpp"

#include <boost/asio/ip/address_v4.hpp>
#include <boost/asio/ip/network_v4.hpp>
#include <fstream>

using boost::asio::ip::tcp;
using namespace std;
boost::asio::io_service io_service;


class server {
 public:
  server(unsigned short port) : _acceptor(io_service,tcp::endpoint(tcp::v4(), port) ){
    newSignalHandler();
    do_accept();
  }

 private:
  void newSignalHandler() {
    _signal.async_wait([this](boost::system::error_code /*ec*/, int /*signo*/) {
                          if (_acceptor.is_open()) {
                            int status = 0;
                            while (waitpid(-1, &status, WNOHANG) > 0) {}
                            newSignalHandler();
                          }
                       });
  }

  void do_accept() {
    _acceptor.async_accept(_socket_requester, 
        [this](boost::system::error_code ec) {  ////save bind for latter study
                if (!ec) {
                  io_service.notify_fork(boost::asio::io_service::fork_prepare);
                  if (fork()==0) {
                    io_service.notify_fork(boost::asio::io_service::fork_child);
                    _acceptor.close();
                    _signal.cancel();   ///////////////////////////////////////////////////////////////////////
                    receive_socks4_package();
                    //dont sure the purpose of error catch here.
                  } 
                  else{
                    io_service.notify_fork(boost::asio::io_service::fork_parent);
                    _socket_requester.close();
                    do_accept();
                  }
                } 
                else {
                  cerr << "Accept error: " << ec.message() << endl;
                  do_accept();
                }
        });
  }
  void receive_socks4_package(){
    boost::asio::read(_socket_requester, socks4_req.mbuffers());  //mbuffers is mutiable buffer  //dont know how it assign to corresponding vaiables
    string D_IP, D_PORT, Command;
    tie(D_IP, D_PORT, Command) = socks4_req.get_socks_status();


    /*Fire wall*/
    if(pass_firewall() == false){
      cout << get_format() % _socket_requester.remote_endpoint().address().to_string()%
                          _socket_requester.remote_endpoint().port()%
                          D_IP % D_PORT % Command %
                          "Reject";
      socks4::reply reply(socks4_req, socks4::reply::status_type::request_failed);
      boost::asio::write(_socket_requester, reply.buffers());
      return;
    }

    /*pass firewall*/
    cout << get_format() % _socket_requester.remote_endpoint().address().to_string()%
                          _socket_requester.remote_endpoint().port()%
                          D_IP % D_PORT % Command %
                          "Accept";
    
    if (socks4_req.command_ == socks4::request::command_type::connect){
      resolve(tcp::resolver::query(socks4_req.getAddress(),  to_string(socks4_req.getPort())));   // able to declare a query as a input of resolve? 
    }   
                                                                                                //but query constructure doesn't has return value. 
    if (socks4_req.command_ == socks4::request::command_type::bind){
      tcp::acceptor acceptor_bind(io_service, tcp::endpoint(tcp::v4(), 0));  

      //making FTP
      socks4_req.port_high_byte_ = acceptor_bind.local_endpoint().port() >> 8;
      socks4_req.port_low_byte_  = acceptor_bind.local_endpoint().port();

      socks4_req.address_ = boost::asio::ip::make_address_v4("0.0.0.0").to_bytes();
      socks4::reply reply(socks4_req, socks4::reply::status_type::request_granted);

      boost::asio::write(_socket_requester, reply.buffers());
      acceptor_bind.accept(_socket_destination);
      boost::asio::write(_socket_requester, reply.buffers());

      async_package_Dest2Requester();
      async_package_Requester2Dest();
    }
    
  }                                                                                               
  void resolve(const tcp::resolver::query& q) {
      _resolver.async_resolve(q, [this](const boost::system::error_code& ec,
                                        tcp::resolver::iterator it) {
        if (!ec) {
          connect(it);
        }
      });
  }
  void connect(const tcp::resolver::iterator& it) {
    _socket_destination.async_connect(*it, 
            [this](const boost::system::error_code& ec) {
                    if (!ec) {         
                      socks4::reply reply(socks4_req, socks4::reply::status_type::request_granted);
                      boost::asio::write(_socket_requester, reply.buffers());
                      async_package_Dest2Requester();
                      async_package_Requester2Dest();
                    }
                  });
  }
  void async_package_Dest2Requester(){
    _socket_destination.async_receive( boost::asio::buffer(destination_buffer_),
        [this](boost::system::error_code ec, std::size_t length) {
          if (!ec) {
            boost::asio::async_write( _socket_requester, boost::asio::buffer(destination_buffer_, length),  
                                [this](boost::system::error_code ec, std::size_t length) {
                                  if (!ec) {
                                    async_package_Dest2Requester();
                                  } else {
                                    throw system_error{ec};
                                  }
                                });
          } else {
            throw system_error{ec};
          }
        });
  }
  
  void async_package_Requester2Dest(){
    _socket_requester.async_receive( boost::asio::buffer(requester_buffer_),  // buffer!!!!! not define
        [this](boost::system::error_code ec, std::size_t length) {
          if (!ec) {
            //write_to_dest(length);
            boost::asio::async_write( _socket_destination, boost::asio::buffer(requester_buffer_, length),   //destination buffer!!!!! not define
                                [this](boost::system::error_code ec, std::size_t length) {
                                  if (!ec) {
                                    async_package_Requester2Dest();
                                  } else {
                                    throw system_error{ec};
                                  }
                                });
          } 
          else {
            throw system_error{ec};
          }
        });
  }
  
  boost::format get_format() {
    return boost::format(
        "<S_IP>:%1%\n"
        "<S_PORT>:%2%\n"
        "<D_IP>:%3%\n"
        "<D_PORT>:%4%\n"
        "<Command>:%5%\n"
        "<Reply>:%6%\n\n");
  }
  bool pass_firewall(){

    ifstream white_list("./socks.conf");     /////////////////////////////////////////////////////////////////////// difference against open???
    string line_of_conf;
    vector<string> white_list_vec;
    string criteria = "permit c ";

    if (socks4_req.command_ == socks4::request::command_type::connect) {
      criteria = "permit c ";
    } else if (socks4_req.command_ == socks4::request::command_type::bind) {
      criteria = "permit b ";
    }

    while (getline(white_list, line_of_conf)) {
      if (line_of_conf.substr(0, criteria.size()) == criteria) {
        white_list_vec.push_back(line_of_conf.substr(criteria.size()));
      }
    }

    for (const string& ip : white_list_vec) {
      auto prefix = ip.substr(0, ip.find('*'));   
      if (socks4_req.getAddress().substr(0, prefix.size()) == prefix) {
        return true;
      }
    }
    return false;
  }
  
  boost::asio::signal_set _signal{io_service, SIGCHLD};       
  tcp::resolver _resolver{io_service};
  tcp::acceptor _acceptor;      //not sure why cant initial it
  tcp::socket _socket_requester{io_service};
  tcp::socket _socket_destination{io_service};


  array<unsigned char, 65536> requester_buffer_{};
  array<unsigned char, 65536> destination_buffer_{};
  socks4::request socks4_req;
  std::array<char, 1024> _data;
};

int main(int argc, char *argv[]) {
  signal(SIGCHLD, SIG_IGN);
  using namespace std;
  try {
    if (argc!=2) {
      std::cerr << "Usage: process_per_connection <port>\n";
      return 1;
    }
    
    server s(stoi(string(argv[1])));
    io_service.run();
  }
  catch (exception &e) {
    cerr << "Exception: " << e.what() << endl;
  }
}