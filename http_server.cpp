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

using boost::asio::ip::tcp;
using namespace std;
boost::asio::io_service io_service;


class server {
 public:
  explicit server(uint16_t port)        ////////////////////////////////////////////forget what expicit stands for
      : _acceptor(io_service,{tcp::v4(), port}){
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
    _acceptor.async_accept(
        [this](boost::system::error_code ec, tcp::socket new_socket) {
          if (!ec) {
            _socket_browser = std::move(new_socket);
            _io_service.notify_fork(boost::asio::io_service::fork_prepare);
            if (fork()==0) {
              _io_service.notify_fork(boost::asio::io_service::fork_child);
              _acceptor.close();
              _signal.cancel();   ///////////////////////////////////////////////////////////////////////
              Parse_http_request();
            } 
            else{
              _io_service.notify_fork(boost::asio::io_service::fork_parent);
              _socket_browser.close();
              do_accept();
            }
          } 
    
          else {
            cerr << "Accept error: " << ec.message() << endl;
            do_accept();
          }
        });
  }

  void Parse_http_request(){
     _socket.async_read_some(boost::asio::buffer(_data),
                            [this](boost::system::error_code ec,
                                   std::size_t length) {
                              if (!ec) {
                                vector<std::string> request_vec;
                                vector<std::string> parse_request_line;
                                vector<std::string> querry_line;
                                map<string, string> client_env;
                                auto result = string(_data.begin(), _data.begin() + length);
                                boost::split( request_vec, result, boost::is_any_of( "\n" ), boost::token_compress_on );
                                for(auto &it : request_vec){
                                  if(it.back()== '\r')
                                    it.pop_back();
                                }
                                //it'll delete "?" which is key to tell if there is query string.   gboost::split( msg1, request_vec.at(0), boost::is_any_of( ":? " ), boost::token_compress_on );
                                boost::split( parse_request_line, request_vec.at(0), boost::is_any_of( " " ), boost::token_compress_on );
                                boost::split( querry_line, parse_request_line.at(1), boost::is_any_of( "?" ), boost::token_compress_on );

                                if(querry_line.size() <2){ //no "?"
                                  client_env["QUERY_STRING"] = "";
                                }else{
                                  client_env["QUERY_STRING"] = querry_line.at(1);
                                }

                                auto method = parse_request_line.at(0);
                                auto program_cgi = querry_line.at(0);
                                auto protocol = parse_request_line.at(2);
                                auto host = request_vec.at(1).substr(request_vec.at(1).find(":")+2 ,request_vec.at(1).rfind(":") - (request_vec.at(1).find(":")+2));
                                

                                client_env["REQUEST_METHOD"] = method;
                                client_env["REQUEST_URI"] = program_cgi;
                                //query is set.
                                client_env["SERVER_PROTOCOL"] = protocol;
                                client_env["HTTP_HOST"] = host;
                                

                                setenv("REQUEST_METHOD", client_env["REQUEST_METHOD"].c_str(), 1);
                                setenv("REQUEST_URI", client_env["REQUEST_URI"].c_str(), 1);
                                setenv("QUERY_STRING", client_env["QUERY_STRING"].c_str(), 1);
                                setenv("SERVER_PROTOCOL", client_env["SERVER_PROTOCOL"].c_str(), 1);
                                setenv("HTTP_HOST", client_env["HTTP_HOST"].c_str(), 1);
   
                                // ------------------------------------- dup -------------------------------------
                                
                                dup2(_socket.native_handle(), 0);
                                dup2(_socket.native_handle(), 1);
                                dup2(_socket.native_handle(), 2);
 
                                // ------------------------------------- exec -------------------------------------
                                
                                std::cout << "HTTP/1.1" << " 200 OK\r\n" << flush;
                                auto program_path = client_env["REQUEST_URI"];
                                program_path = program_path.substr(1);
                                cout << program_path << endl;
                                char *argv[] = {nullptr};
                                if (execv(program_path.c_str(), argv)==-1) {
                                  perror("execv error");
                                  exit(-1);
                                }
                                
                                
                              }
                            });
  }
  boost::asio::signal_set _signal{io_service, SIGCHLD};       
  tcp::resolver _resolverio{io_service};
  tcp::acceptor _acceptor{io_service};
  tcp::socket _socket_browser{io_service};
  tcp::socket _socket_destination{io_service};
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