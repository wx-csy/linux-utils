#include <system_error>
#include <iostream>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "tcp.h"

namespace TCP {
  // TCPBuf

  TCPBuf::int_type TCPBuf::overflow(int_type ch) {
    char* buf = obuf.get();
    if (write(sfd, pbase(), epptr() - pbase()) < 0) 
      throw std::runtime_error(strerror(errno));
     
    setp(buf, buf + bufsize);
    buf[0] = ch;
    pbump(1);
    return 0;
  }
    
  TCPBuf::int_type TCPBuf::underflow() {
    char* buf = ibuf.get();
    ssize_t sz = recv(sfd, buf, bufsize, 0);
    if (sz < 0)
      throw std::runtime_error(strerror(errno));
    if (sz == 0) return EOF;
    setg(buf, buf, buf + sz);
    return buf[0];
  }

  int TCPBuf::sync() {
    char* buf = obuf.get();
    if (write(sfd, pbase(), epptr() - pbase()) < 0)
      throw std::runtime_error(strerror(errno));
    setp(buf, buf + bufsize);
    return 0;
  }

  TCPBuf::~TCPBuf() {
    if (sfd >= 0) {
      close(sfd);
      sfd = -1;
//      std::clog << "socket closed!" << std::endl;
    }
  }

  // TCPStream


  // TCPListener

  TCPListener::TCPListener() : sfd(-1) { }

  void TCPListener::listen(int port) {
    sfd = socket(PF_INET, SOCK_STREAM, 0); 
    if (sfd < 0)
      throw std::runtime_error(strerror(errno));
    
    int yes = 1;
    if (setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes) < 0) 
      throw std::runtime_error(strerror(errno));

    sockaddr_in saddr;
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(port);
    saddr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sfd, reinterpret_cast<sockaddr*>(&saddr), sizeof saddr) < 0)
      throw std::runtime_error(strerror(errno));
    
    if (::listen(sfd, 512) < 0)
      throw std::runtime_error(strerror(errno));
  }

  TCPStream TCPListener::accept() {
    int cfd;
    sockaddr_in caddr;
    socklen_t length = sizeof caddr;
    cfd = ::accept(sfd, reinterpret_cast<sockaddr*>(&caddr), &length);
    if (cfd < 0) 
      throw std::runtime_error(strerror(errno));
    return TCPStream(cfd);
  }
  
  TCPListener::~TCPListener() {
    if (sfd > 0) {
      close(sfd);
      sfd = -1;
      std::clog << "Listener socket closed!" << std::endl;
    }
  }
}

