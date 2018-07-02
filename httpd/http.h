#ifndef __HTTP_H__
#define __HTTP_H__

#include <iostream>
#include <string>
#include <map>

#include "tcp.h"

namespace HTTP {
   
  struct HTTPRequestHeader {
    std::string method;
    std::string url;
    std::string protocol;
    std::map<std::string, std::string> keys;

    HTTPRequestHeader(std::istream& is);
  };

  std::istream& operator >> (std::istream& is, 
      HTTPRequestHeader& header);

  struct HTTPResponseHeader {
    static std::map<int, std::string> status_name;

    std::string protocol = "HTTP/1.1";
    int status;
    std::map<std::string, std::string> keys;

    HTTPResponseHeader(int status, 
        std::map<std::string, std::string> keys);
  };
  
  std::ostream& operator << (std::ostream& os, 
      const HTTPResponseHeader& header);
  
  void HTTPHandler(TCP::TCPStream tcp);

}

#endif

