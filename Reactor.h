#pragma once
 
#include <netdb.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdio.h>
#include <string>
#include <fstream>
#include <unordered_map>
#include <mutex>
#include <chrono>
#include <sys/prctl.h>
#include <openssl/evp.h>
 
constexpr int PORT        = 3940;
constexpr int BACKLOG     = 256;
constexpr int MAX_EVENTS  = BACKLOG * 4;
constexpr int BUFFER_SIZE = 1024;
 
class Reactor {
public:
    Reactor(int worker_id);
    ~Reactor();
    void run();
 
private:
    void process_requests(int sockfd);
    void rearm(int sockfd);
    void handle_new_connection();
    int  init_listenfd();
    int  init_epollfd();
 
    int         listenfd{0};
    int         epollfd{0};
    std::string password;
    std::string key;
    std::string iv;
    std::unordered_map<int, std::string> conn_buffers;
 
    static std::mutex file_mutex;
};
