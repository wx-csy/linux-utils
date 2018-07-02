#ifndef __TCP_H__
#define __TCP_H__

#include <streambuf>
#include <iostream>
#include <memory>

namespace TCP {

  class TCPBuf : public std::streambuf {
    static constexpr size_t bufsize = 8192;

    int sfd;
    std::unique_ptr<char[]> ibuf {new char[bufsize]}, obuf {new char[bufsize]};

    friend class TCPStream;

  private:
    TCPBuf(int sfd) : sfd(sfd) {
      char* buf = obuf.get();
      setp(buf, buf + bufsize);
    }

  protected:
    int_type overflow(int_type ch) override;
    int_type underflow() override;
    int sync() override;

    operator bool() {
      return sfd >= 0; 
    }

  public:
    TCPBuf(const TCPBuf&) = delete;
    TCPBuf& operator = (const TCPBuf&) = delete;
    TCPBuf& operator = (TCPBuf&&) = delete;
    
    TCPBuf(TCPBuf&& other) : std::streambuf(std::move(other)), sfd(other.sfd), 
        ibuf(std::move(other.ibuf)), obuf(std::move(other.obuf)) {
      other.sfd = -1;
    }

    ~TCPBuf();
  };

  class TCPStream : public std::iostream {
    TCPBuf tcpbuf;
    
    friend class TCPListener;

  private:
    TCPStream(int sfd) : tcpbuf(sfd) {
      rdbuf(&tcpbuf);
    }

  public:
    TCPStream(const TCPStream&) = delete;
    TCPStream& operator = (const TCPStream&) = delete;
    TCPStream& operator = (TCPStream&&) = delete;

    TCPStream(TCPStream&& other) : tcpbuf(std::move(other.tcpbuf)) {
      rdbuf(&tcpbuf);
    }
    
    ~TCPStream() {
      flush();
    }

    operator bool() {
      return tcpbuf; 
    }
  };

  class TCPListener {
    int sfd;

  public:
    TCPListener();
    void listen(int port);
    TCPStream accept();
    ~TCPListener();
  };

}

#endif

