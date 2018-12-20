#include <sstream>
#include <fstream>
#include <utility>
#include <vector>
#include <string>
#include <map>
#include <shared_mutex>
#include <memory>
#include <cstring>

#include "http.h"

extern std::string site_path;

namespace HTTP {
   
  // HTTPRequestHeader

  HTTPRequestHeader::HTTPRequestHeader(std::istream& is) {
    is >> *this;
  }

  std::istream& operator >> (std::istream& is, 
      HTTPRequestHeader& header) {
    std::string str;
    std::getline(is, str);
    if (str.size() && *(str.rbegin()) == '\r') str.pop_back();
     
    std::stringstream ss;
    ss << str;
    ss >> header.method >> header.url >> header.protocol;

    while (true) {
      std::getline(is, str);
      if (str.size() && *(str.rbegin()) == '\r') str.pop_back();
      if (str.size() == 0) break;
      int pos = str.find(": ");
      header.keys[str.substr(0, pos)] = str.substr(pos + 2);
    }
    return is;
  }
  

  // HTTPResponseHeader

  std::map<int, std::string> HTTPResponseHeader::status_name = {
    { 200, "OK" },
    { 400, "Bad Request" },
    { 404, "Not Found" },
    { 501, "Not Implemented" },
  };

  HTTPResponseHeader::HTTPResponseHeader(int status,
      std::map<std::string, std::string> keys) :
    status(status), keys(keys) { } 

  std::ostream& operator << (std::ostream& os, 
      const HTTPResponseHeader& header) {
    int status;
    if (HTTPResponseHeader::status_name.find(header.status) == 
        HTTPResponseHeader::status_name.end())
      status = 501;
    else 
      status = header.status;
    os << header.protocol << ' ' << header.status << ' '
      << HTTPResponseHeader::status_name[status] << "\r\n";
    for (auto& kvp : header.keys) {
      os << kvp.first << ": " << kvp.second << "\r\n";
    }
    os << "\r\n";
    return os;
  }

  // HTTPHandler
  
  using namespace TCP;

  static std::pair<std::string, bool> canonicalize_path(std::string path) {
    std::istringstream ss(path);
    std::vector<std::string> names;
    std::string result;
    for (std::string token; std::getline(ss, token, '/');) {
      if (token == "" || token == ".") {
        continue;
      } else if (token == "..") {
        if (!names.empty()) names.pop_back();
        else return {"", false};
      } else {
        names.push_back(token);
      }
    }
    for (size_t i = 0; i < names.size(); i++) {
      result += "/" + names[i];
    }
    return {result, true};
  }
  
  static void send404(TCPStream& tcp) { 
    const char* resp = "404 Not Found\n"
      "The page you requested was not found.\n";
    int len = strlen(resp);
    HTTPResponseHeader rphdr(404, {
          { "Content-Length", std::to_string(len) },
          { "Content-Type", "text/plain" },
          { "Server", "httpd" },
        });
    tcp << rphdr;
    tcp.write(resp, len);
  }

  static void get_handler(TCPStream& tcp, const HTTPRequestHeader& rqhdr) {
    static const std::map<std::string, std::string> content_type {
      { "html", "text/html" },
      { "css",  "text/css" },
      { "png",  "image/png" },
      { "ico",  "image/ico" },
    };

    bool succ;
    std::string path;
    tie(path, succ) = canonicalize_path(rqhdr.url);
//    std::clog << path << std::endl;
    if (!succ) {
      send404(tcp);
      return ;
    }
    if (path == "") path = "/index.html";
    
    auto pos = path.find_last_of('.');
    std::string suffix, cont_tp;
    if (pos != path.npos) suffix = path.substr(pos + 1);
    if (content_type.find(suffix) != content_type.end())
      cont_tp = content_type.at(suffix);
    
    
    static struct {
      bool active;
      std::string path;
      size_t size;
      char *content;
      std::shared_timed_mutex mut;
    } cache[256];

    uint8_t hash = std::hash<std::string>{}(path);
    
    cache[hash].mut.lock_shared();

    if (cache[hash].active && cache[hash].path == path) {
      // cache hit
    } else {
      // cache miss
      cache[hash].mut.unlock_shared();
      cache[hash].mut.lock();

      std::ifstream file(site_path + path, file.binary | file.in);
      std::clog << "Read file: " << site_path + path << std::endl;
      if (!file) {
        cache[hash].mut.unlock();
        send404(tcp);
        return ;
      }
      file.seekg(0, file.end);
      size_t size = file.tellg();
      file.seekg(0);
      char *buf = new char[size];
      file.read(buf, size);
      if (cache[hash].active) {
        delete[] cache[hash].content;
      }
 
      cache[hash].active = true;
      cache[hash].path = path;
      cache[hash].size = size;
      cache[hash].content = buf;

      cache[hash].mut.unlock();
      cache[hash].mut.lock_shared();
    } 
    
    int size = cache[hash].size;
    HTTPResponseHeader rphdr(200, {
          { "Content-Length", std::to_string(size) },
          { "Server", "httpd" },
        });
    if (cont_tp != "")
      rphdr.keys["Content-Type"] = cont_tp;
    tcp << rphdr;
    tcp.write(cache[hash].content, cache[hash].size);
    cache[hash].mut.unlock_shared();
  }

  static void def_handler(TCPStream& tcp, const HTTPRequestHeader& rqhdr) {
    std::string text = "The method \"" + rqhdr.method + 
      "\" you requested is not supported.\n";
    HTTPResponseHeader rphdr(400, {
          { "Content-Length", std::to_string(text.size()) },
          { "Content-Type", "text/plain" },
          { "Server", "httpd" },
        });
    tcp << rphdr;
    tcp.write(text.c_str(), text.size());
  }

  static std::map<std::string, void (*)(TCPStream&, const HTTPRequestHeader&)> 
    method_handler {
      { "GET", get_handler },
    };


  void HTTPHandler(TCPStream tcp) {
    HTTPRequestHeader rqhdr(tcp);
    
    if (method_handler.find(rqhdr.method) != method_handler.end())
      method_handler[rqhdr.method](tcp, rqhdr);
    else
      def_handler(tcp, rqhdr);
  }
}

