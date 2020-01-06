#include <array>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <unistd.h>
#include <boost/asio.hpp>
#include <boost/format.hpp>
#include <boost/bind.hpp>
#include <boost/algorithm/string.hpp>   //for repace_all
#include "socks4.hpp"
using namespace std;

using namespace boost::asio;
using namespace boost::asio::ip;
string socks_host;
string socks_port;
io_service service;

class console_format{
    public:
        class Npshell {
            public:
                std::string name;   //differenciate from boost::algorithm::string.hpp>
                std::string port;
                std::string file;
            }npshells[5];
        string console_response(string querry_env){   //string is an object.

            /* format for intial setting */
            std::string console_msg ="\
            <html lang=\"en\">\
                <head>\
                <meta charset=\"UTF-8\" />\
                <title>NP Project 3 Console</title>\
                <link\
                    rel=\"stylesheet\"\
                    href=\"https://stackpath.bootstrapcdn.com/bootstrap/4.1.3/css/bootstrap.min.css\"\
                    integrity=\"sha384-MCw98/SFnGE8fJT3GXwEOngsV7Zt27NXFoaoApmYm81iuXoPkFOJwJ8ERdknLPMO\"\
                    crossorigin=\"anonymous\"\
                />\
                <link\
                    href=\"https://fonts.googleapis.com/css?family=Source+Code+Pro\"\
                    rel=\"stylesheet\"\
                />\
                <link\
                    rel=\"icon\"\
                    type=\"image/png\"\
                    href=\"https://cdn0.iconfinder.com/data/icons/small-n-flat/24/678068-terminal-512.png\"\
                />\
                <style>\
                    * {\
                        font-family: 'Source Code Pro', monospace;\
                        font-size: 1rem !important;\
                    }\
                    body {\
                        background-color: #212529;\
                    }\
                    pre {\
                        color: #cccccc;\
                    }\
                    b {\
                        color: #ffffff;\
                    }\
                </style>\
            </html>";

            /* format for intial interface */
            boost::format fmt("  <body>\n"
                      "    <table class=\"table table-dark table-bordered\">\n"
                      "      <thead>\n"
                      "        <th>\n"
                      "%1%"
                      "        </th>\n"
                      "      </thead>\n"
                      "      <tbody>\n"
                      "        <th>\n"
                      "%2%"
                      "        </th>\n"
                      "      </tbody>\n"
                      "    </table>\n"
                      "  </body>\n");
            vector<string> test;
            boost::split(test, querry_env, boost::is_any_of("&"));
            std::string key;
            std::string value;
            int shell_num = 0;
            for(auto &it : test){
                key =  it.substr(0, it.find('='));
                value = it.substr(key.size() + 1);

                if (!value.empty()) {
                    if(key.front() == 'h')
                        npshells[shell_num].name = value;
                    else if(key.front() == 'p')
                        npshells[shell_num].port = value;
                    else if(key.front() == 'f'){
                        npshells[shell_num].file = value;
                        shell_num ++;
                    }
                    else if(key == "sh")
                        socks_host = value;
                    else if(key == "sp")  
                        socks_port =  value;
                }
            }

            std::string table;
            std::string session;
            int count = 0;
            for(auto npshell : npshells){
                if(!npshell.name.empty()){
                    table +="            <th scope=\"col\">" + npshell.name + ":" + npshell.port + "</th>\n";
                    session +="          <td><pre id=\"s" + std::to_string(count) + "\" class=\"mb-0\"></pre></td>";
                    count++;
                }
            }
            return console_msg + (fmt%table%session).str() + "</html>\n";
        }


};
struct Client : public std::enable_shared_from_this<Client> {         //structure default mem type is public.
  Client(std::string session, std::string file_name, tcp::resolver::query q, tcp::resolver::query dest)    
      : session(std::move(session)), q(std::move(q)) , dest(std::move(dest)){

    file.open("test_case/" + file_name, std::ios::in);
    if (!file.is_open())
      output_command("file open failed!");
    

    
  }
  void resolve() {
    tcp::resolver resolver(service);

    auto endpoints = resolver.resolve(q);

    boost::asio::connect(tcp_socket, endpoints);  

    boost::asio::ip::tcp::endpoint http_endpoint = *resolver.resolve(dest);

    socks4::request socks_request(socks4::request::connect, http_endpoint, "");
    boost::asio::write(tcp_socket, socks_request.buffers(), boost::asio::transfer_all());

    socks4::reply socks_reply;
    boost::asio::read(tcp_socket, socks_reply.buffers(), boost::asio::transfer_all());

    if (!socks_reply.success()) {
      std::cout << "Connection failed.\n";
      std::cout << "status = 0x" << std::hex << socks_reply.status();
      return;
    }
    read_handler();

  }


  void read_handler() {
    auto self(shared_from_this());
    /* format note:
      this ptr for calling receive_msg, shell_output, etc. they all non-static member only can be used after instanced.
    self is for prolonging life time of smart_ptr.
      function note: 
      async_receive, when receive msg, it's triggered. 
    */
    tcp_socket.async_receive(boost::asio::buffer(receive_msg),
                             [this, self](boost::system::error_code ec,   
                                          std::size_t bytes_transferred) {
                               if (!ec) { //if no error code.
                                 std::string data(receive_msg.begin(),receive_msg.begin() + bytes_transferred); 
                                 shell_output(data);

                                 if (data.find("% ")!=std::string::npos) {
                                   std::string command;
                                   getline(file, command);
                                   command = command +'\n';
                                   output_command(command );
                                   //tcp_socket.write_some(buffer( command));
                                   tcp_socket.async_write_some(buffer(command, command.length()),[](error_code ec, size_t transferredBytes){
                                                                                              if (!ec) {
                                                                                                  //no other operation.
                                                                                              }
                                                                                            });
                                  
                                 }
                                 read_handler(); 
                               }
                             });
  }

  

  void shell_output(std::string content) {
    encode(content);
    boost::replace_all(content, "\n", "&#13;");
    boost::replace_all(content, "\r", "");
    boost::format fmt("<script>document.getElementById('%1%').innerHTML += '%2%';</script>");
    cout << fmt%session%content << flush;
  }

  void output_command(std::string content) {
    encode(content);
    boost::replace_all(content, "\n", "&#13;");
    boost::replace_all(content, "\r", "");
    boost::format fmt("<script>document.getElementById('%1%').innerHTML += '<b>%2%</b>';</script>");
    cout << fmt%session%content << flush;
  }

  void encode(std::string &data) {
    using boost::algorithm::replace_all;   
    /*
    1. using 名稱空間::成員
    2. using namespace 名稱空間
    two ways to indicate member's namespace. 
    */
    replace_all(data, "&", "&amp;");
    replace_all(data, "\"", "&quot;");
    replace_all(data, "\'", "&apos;");
    replace_all(data, "<", "&lt;");
    replace_all(data, ">", "&gt;");
  }

  std::string session; // "s0~s4"
  tcp::resolver::query q;
  tcp::resolver::query dest;
  tcp::resolver resolver{service};    //initial value, service which is global value.
  tcp::socket tcp_socket{service};    //initial value, service which is global value.
  std::array<char, 4096> receive_msg;
  std::fstream file; // "t1~t4.txt"
};

int main() {
    /* HTTP Header needed*/
    cout << "Content-type: text/html\r\n\r\n" << flush;
  
    console_format console_me;
    cout << getenv("QUERY_STRING") << endl;
    cout  << console_me.console_response(getenv("QUERY_STRING")) << flush;
    
    tcp::resolver::query socks4(socks_host, socks_port);     //{ip, port}
    int k = 0;
    for (auto npshell : console_me.npshells) {
      if (!npshell.port.empty()) {
        tcp::resolver::query q(npshell.name, npshell.port);     //{ip, port}
        std::make_shared<Client>("s" + std::to_string(k), npshell.file, socks4, std::move(q))   //socket{1~5}, file name, query(name,port)
            ->resolve();
        k++;
      }
  }
 
  service.run();
  return 0;
}