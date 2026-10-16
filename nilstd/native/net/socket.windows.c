#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdint.h>
#include <stdio.h>

#pragma comment(lib, "Ws2_32.lib")

// ML types mapping
typedef int32_t i32;
typedef int64_t i64;
typedef uint8_t u8;

static int wsa_init = 0;

void ensure_wsa() {
    if (!wsa_init) {
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
        wsa_init = 1;
    }
}

i64 ml_socket_create(i32 domain, i32 type) {
    ensure_wsa();
    SOCKET s = socket(domain, type, 0);
    if (s == INVALID_SOCKET) return -1;
    return (i64)s;
}

i32 ml_socket_bind(i64 handle, const char* ip, i32 port) {
    SOCKET s = (SOCKET)handle;
    struct sockaddr_in addr;

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(ip);
    addr.sin_port = htons((u_short)port);

    if (bind(s, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        return -1;
    }
    return 0;
}

i32 ml_socket_listen(i64 handle, i32 backlog) {
    SOCKET s = (SOCKET)handle;
    if (listen(s, backlog) == SOCKET_ERROR) return -1;
    return 0;
}

i64 ml_socket_accept(i64 handle) {
    SOCKET s = (SOCKET)handle;
    SOCKET client = accept(s, NULL, NULL);
    if (client == INVALID_SOCKET) return -1;
    return (i64)client;
}

i32 ml_socket_connect(i64 handle, const char* ip, i32 port) {
    SOCKET s = (SOCKET)handle;
    struct sockaddr_in addr;

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(ip);
    addr.sin_port = htons((u_short)port);

    if (connect(s, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        return -1;
    }
    return 0;
}

i32 ml_socket_send(i64 handle, u8* data, i32 len) {
    SOCKET s = (SOCKET)handle;
    int sent = send(s, (const char*)data, len, 0);
    if (sent == SOCKET_ERROR) return -1;
    return sent;
}

i32 ml_socket_recv(i64 handle, u8* buffer, i32 len) {
    SOCKET s = (SOCKET)handle;
    int received = recv(s, (char*)buffer, len, 0);
    if (received == SOCKET_ERROR) return -1;
    return received;
}

i32 ml_socket_close(i64 handle) {
    closesocket((SOCKET)handle);
    return 0;
}
