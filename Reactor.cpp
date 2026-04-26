#include "Reactor.h"
#include <chrono>
#include <cstring>

std::mutex Reactor::file_mutex;

static std::string aes_decrypt(const std::string& ciphertext,
                               const std::string& key,
                               const std::string& iv) {
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr,
        reinterpret_cast<const unsigned char*>(key.c_str()),
        reinterpret_cast<const unsigned char*>(iv.c_str()));

    std::string plaintext(ciphertext.size(), '\0');
    int len = 0, total = 0;

    EVP_DecryptUpdate(ctx,
        reinterpret_cast<unsigned char*>(&plaintext[0]), &len,
        reinterpret_cast<const unsigned char*>(ciphertext.data()), ciphertext.size());
    total += len;

    EVP_DecryptFinal_ex(ctx,
        reinterpret_cast<unsigned char*>(&plaintext[total]), &len);
    total += len;

    EVP_CIPHER_CTX_free(ctx);
    plaintext.resize(total);
    return plaintext;
}

// Constructor 
Reactor::Reactor(int worker_id) {
    this->password = getenv("HERMES_PASSWORD");
    this->key = getenv("HERMES_AES_KEY");
    this->iv = getenv("HERMES_AES_IV");

    if (this->password.empty() || this->key.empty() || this->iv.empty()) {
        fprintf(stderr, "missing env vars");
        exit(EXIT_FAILURE);
    }

    char name[16];
    snprintf(name, sizeof(name), "Reactor-%d", worker_id);
    prctl(PR_SET_NAME, name, 0, 0, 0);

    this->listenfd = init_listenfd();
    this->epollfd  = init_epollfd();
}

Reactor::~Reactor() {
    close(this->listenfd);
    close(this->epollfd);
}

void Reactor::run() {
    struct epoll_event events[MAX_EVENTS];
    int nfds{0};

    while (true) {
        nfds = epoll_wait(this->epollfd, events, MAX_EVENTS, -1);
        if (nfds == -1) {
            perror("epoll_wait");
            exit(EXIT_FAILURE);
        }

        for (int n = 0; n < nfds; ++n) {
            if (events[n].data.fd == this->listenfd) {
                handle_new_connection();
            } else {
                process_requests(events[n].data.fd);
            }
        }
    }
}

void Reactor::process_requests(int sockfd) {
    char buffer[BUFFER_SIZE];

    while (true) {
        ssize_t nbytes = recv(sockfd, buffer, BUFFER_SIZE, MSG_DONTWAIT);

        if (nbytes > 0) {
            conn_buffers[sockfd].append(buffer, nbytes);
        }
        else if (nbytes == 0) { // connection closed: save file
            std::string& buf = conn_buffers[sockfd];

            {
                // decrypt entire buffer first
                std::string decrypted;
                try {
                    decrypted = aes_decrypt(buf, this->key, this->iv);
                } catch (...) {
                    printf("rejected fd %d: decryption failed\n", sockfd);
                    goto error_cleanup;
                }

                // decrypted format: filename\password\content
                size_t first  = decrypted.find('\n');
                size_t second = decrypted.find('\n', first + 1);

                if (first == std::string::npos || second == std::string::npos) {
                    printf("rejected fd %d: malformed payload\n", sockfd);
                    goto error_cleanup;
                }

                std::string fname    = decrypted.substr(0, first);
                std::string fpassword = decrypted.substr(first + 1, second - first - 1);

                if (fpassword != this->password) {
                    printf("rejected fd %d: wrong password\n", sockfd);
                    goto error_cleanup;
                }

                std::string content = decrypted.substr(second + 1);

                // unique filename: timestamp + original filename
                auto now = std::chrono::system_clock::now();
                auto ts  = std::chrono::duration_cast<std::chrono::seconds>(
                               now.time_since_epoch()).count();
                std::string filename = fname + "_received_" + std::to_string(ts);

                std::lock_guard<std::mutex> lock(file_mutex);
                std::ofstream f(filename, std::ios::binary);
                if (f.is_open()) {
                    f << content;
                    f.close();
                    printf("saved: %s\n", filename.c_str());
                } else {
                    fprintf(stderr, "could not open %s\n", filename.c_str());
                }
            }
            goto error_cleanup;
        }
        else {
            if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
                rearm(sockfd);
                return;
            }
            goto error_cleanup;
        }
    }
    return;

error_cleanup:
    epoll_ctl(this->epollfd, EPOLL_CTL_DEL, sockfd, nullptr);
    conn_buffers.erase(sockfd);
    close(sockfd);
}


void Reactor::rearm(int sockfd) {
    struct epoll_event ev;
    ev.events  = EPOLLIN | EPOLLET | EPOLLONESHOT;
    ev.data.fd = sockfd;
    epoll_ctl(this->epollfd, EPOLL_CTL_MOD, sockfd, &ev);
}


void Reactor::handle_new_connection() {
    thread_local static struct sockaddr_storage remote_storage;
    thread_local static socklen_t addrlen;

    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET | EPOLLONESHOT;

    while (true) {
        addrlen    = sizeof(remote_storage);
        int connfd = accept4(this->listenfd,
                             reinterpret_cast<sockaddr*>(&remote_storage),
                             &addrlen, SOCK_NONBLOCK | SOCK_CLOEXEC);

        if (connfd == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            perror("accept4");
            break;
        }

        ev.data.fd = connfd;
        if (__builtin_expect(epoll_ctl(this->epollfd, EPOLL_CTL_ADD, connfd, &ev) == -1, 0)) {
            perror("epoll_ctl: connfd");
            close(connfd);
        }
    }
}

int Reactor::init_listenfd() {
    struct addrinfo hints, *p, *result;
    int listenfd{0};
    const int enable = 1;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags    = AI_PASSIVE;

    if (int err = getaddrinfo(NULL, std::to_string(PORT).c_str(), &hints, &result); err != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(err));
        exit(EXIT_FAILURE);
    }

    for (p = result; p != NULL; p = p->ai_next) {
        listenfd = socket(p->ai_family,
                          p->ai_socktype | SOCK_NONBLOCK | SOCK_CLOEXEC,
                          p->ai_protocol);
        if (listenfd == -1) continue;

        setsockopt(listenfd, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(int));
        setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int));

        int cpu = sched_getcpu();
        setsockopt(listenfd, SOL_SOCKET, SO_INCOMING_CPU, &cpu, sizeof(cpu));

        if (bind(listenfd, p->ai_addr, p->ai_addrlen) == 0) break;

        close(listenfd);
        listenfd = -1;
    }

    freeaddrinfo(result);

    if (listenfd == -1 || p == NULL) {
        perror("could not bind");
        exit(EXIT_FAILURE);
    }

    if (listen(listenfd, BACKLOG) == -1) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    return listenfd;
}

int Reactor::init_epollfd() {
    int epollfd = epoll_create1(EPOLL_CLOEXEC);
    if (epollfd == -1) {
        perror("epoll_create1");
        exit(EXIT_FAILURE);
    }

    struct epoll_event ev;
    ev.events  = EPOLLIN;
    ev.data.fd = this->listenfd;

    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, this->listenfd, &ev) == -1) {
        perror("epoll_ctl: listenfd");
        exit(EXIT_FAILURE);
    }

    return epollfd;
}
