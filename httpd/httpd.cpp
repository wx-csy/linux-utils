#include <iostream>
#include <string>
#include <exception>
#include <vector>
#include <queue>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <thread>
#include <csignal>

#include "tcp.h"
#include "http.h"

using namespace TCP;
using namespace HTTP;

TCPListener listener;

std::mutex mut;
std::condition_variable cv;
volatile std::sig_atomic_t term_flag = 0;
std::queue<TCPStream> requests;
std::vector<std::thread> thread_pool;

void worker() {
  std::unique_lock<std::mutex> lk(mut, std::defer_lock);
  while (true) {
    lk.lock();
    cv.wait(lk, [] { return requests.size() || term_flag; });
    if (term_flag) break;
    TCPStream tcp = std::move(requests.front());
    requests.pop();
    lk.unlock();
    HTTPHandler(std::move(tcp));
  }
}

extern "C" void sigint_handler(int signum) {
  term_flag = 1;
}

[[noreturn]] void usage() {
  std::cout << 
    "Usage: httpd [ -p port ] dir\n"
    "A simple http server.\n"
    "\n"
    "  -p, --port   specify port number\n" << std::endl;
  exit(0);
}

std::string site_path;

int main(int argc, char *argv[]) {
  struct sigaction act, oldact;
  act.sa_handler = sigint_handler;
  act.sa_flags = SA_NODEFER | SA_RESETHAND;
  sigaction(SIGINT, &act, &oldact);

  // std::signal(SIGINT, sigint_handler);
  int port = 80;
  if (argc < 2) usage(); 
  for (int i = 1; i < argc; i++) {
    if (argv[i][0] != '-') {
      site_path = argv[i];
      continue;
    }
    if (argv[i] == std::string("-p") || argv[i] == std::string("--port")) {
      i++;
      if (i >= argc - 1) usage();
      port = atoi(argv[i]);
      if (port == 0) usage();
    } else {
      usage();    
    }
  }
  if (site_path == "") usage();

  try {
    listener.listen(port);
  } catch (std::exception& ex) {
    std::clog << "Failed to start server: " << ex.what() << std::endl;
    exit(0);
  }
  
  std::clog << "The server has been successfully started." << std::endl;
  std::clog << "tid: " << std::this_thread::get_id() << std::endl;

  try {
    int num_of_threads = std::thread::hardware_concurrency();
    if (num_of_threads == 0) num_of_threads = 4;
    else num_of_threads *= 4; 
    try {
      for (int i = 0; i < num_of_threads; i++) {
        thread_pool.push_back(std::thread(worker));
        std::clog << "Worker (" << thread_pool.rbegin()->get_id() << 
          ") started!" << std::endl;
      }
    } catch (std::exception& ex) {
      std::clog << "Failed to create more threads: " << ex.what() << std::endl;
    }

    while (term_flag == 0) {
      TCPStream tcp(listener.accept());
      mut.lock();
      requests.push(std::move(tcp));
      mut.unlock();
      cv.notify_one();
    }
  
  } catch (std::exception& ex) {
    std::clog << "Exception caught: " << ex.what() << std::endl;
  }
  
  cv.notify_all();

  for (auto& th : thread_pool) {
    std::clog << "Joining " << th.get_id() << std::endl;
    th.join();
  }
  
  std::clog << "The server has been gracefully shut down :)" << std::endl;
  return 0;
}
