#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>

// ML types mapping
typedef int32_t i32;
typedef int64_t i64;
typedef uint8_t u8;

i64 ml_socket_create(i32 domain, i32 type) {
    int s = socket(domain, type, 0);
    if (s < 0) return -1;
    return (i64)s;
}

i32 ml_socket_bind(i64 handle, const char* ip, i32 port) {
    int s = (int)handle;
    struct sockaddr_in addr;

    // Simplification for MVP
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(ip);
    addr.sin_port = htons((uint16_t)port);

    if (bind(s, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        return -1;
    }
    return 0;
}

i32 ml_socket_listen(i64 handle, i32 backlog) {
    if (listen((int)handle, backlog) < 0) return -1;
    return 0;
}

i64 ml_socket_accept(i64 handle) {
    int client = accept((int)handle, NULL, NULL);
    if (client < 0) return -1;
    return (i64)client;
}

i32 ml_socket_connect(i64 handle, const char* ip, i32 port) {
    int s = (int)handle;
    struct sockaddr_in addr;

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(ip);
    addr.sin_port = htons((uint16_t)port);

    if (connect(s, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        return -1;
    }
    return 0;
}

i32 ml_socket_send(i64 handle, u8* data, i32 len) {
    int sent = send((int)handle, data, len, 0);
    return sent;
}

i32 ml_socket_recv(i64 handle, u8* buffer, i32 len) {
    int received = recv((int)handle, buffer, len, 0);
    return received;
}

i32 ml_socket_close(i64 handle) {
    close((int)handle);
    return 0;
}
