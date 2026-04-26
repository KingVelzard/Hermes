#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <fstream>
#include <string>
#include <filesystem>
#include <openssl/evp.h>

constexpr int PORT    = 3940;

static std::string aes_encrypt(const std::string& plaintext,
                               const std::string& key,
                               const std::string& iv) {
    static EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr,
        reinterpret_cast<const unsigned char*>(key.c_str()),
        reinterpret_cast<const unsigned char*>(iv.c_str()));

    std::string ciphertext(plaintext.size() + 16, '\0');
    int len = 0, total = 0;

    EVP_EncryptUpdate(ctx,
        reinterpret_cast<unsigned char*>(&ciphertext[0]), &len,
        reinterpret_cast<const unsigned char*>(plaintext.data()), plaintext.size());
    total += len;

    EVP_EncryptFinal_ex(ctx,
        reinterpret_cast<unsigned char*>(&ciphertext[total]), &len);
    total += len;

    ciphertext.resize(total);
    return ciphertext;
}

static int send_file(const std::string& ip, 
                     const std::string& password, 
                     const std::string& key, 
                     const std::string& iv,
                     const std::string& filepath) {
    // read file
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        fprintf(stderr, "could not open: %s\n", filepath.c_str());
        return 1;
    }
    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());

    // encrypt entire payload: filename\npassword\ncontent
    std::string fname   = std::filesystem::path(filepath).filename().string();
    std::string payload = aes_encrypt(fname + "\n" + std::string(password) + "\n" + content,
                                      key,
                                      iv);

    struct addrinfo hints, *result;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (int err = getaddrinfo(ip.c_str(), std::to_string(PORT).c_str(), &hints, &result); err != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(err));
        return 1;
    }

    int sockfd = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (sockfd == -1) { perror("socket"); freeaddrinfo(result); return 1; }

    if (connect(sockfd, result->ai_addr, result->ai_addrlen) == -1) {
        perror("connect"); freeaddrinfo(result); close(sockfd); return 1;
    }
    freeaddrinfo(result);

    // send all bytes
    const char* ptr   = payload.data();
    ssize_t remaining = payload.size();
    while (remaining > 0) {
        ssize_t n = send(sockfd, ptr, remaining, 0);
        if (n == -1) { perror("send"); close(sockfd); return 1; }
        ptr       += n;
        remaining -= n;
    }

    printf("sent: %s\n", filepath.c_str());
    close(sockfd);
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc < 6) {
        fprintf(stderr, "Usage: %s <server_ip> <password> <aes_key> <aes_iv> <file1> [file2 ...]\n", argv[0]);
        return 1;
    }

    std::string ip       = argv[1];
    std::string password = argv[2];
    std::string key      = argv[3];
    std::string iv       = argv[4];

    int failed = 0;
    for (int i = 5; i < argc; ++i) {
        failed += send_file(ip, password, key, iv, argv[i]);
    }
    return failed > 0 ? 1 : 0;
}
