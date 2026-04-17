#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>
#include <sys/socket.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

typedef enum {
    CLIENT_KERNEL_SCHEDULER = 1,
    CLIENT_CPU              = 2,
    CLIENT_MEMORY_STICK     = 3,
    CLIENT_SWAP             = 4,
    CLIENT_IO               = 5,
} t_client_type;

#define MSG_HANDSHAKE_KS      0x10
#define MSG_HANDSHAKE_CPU     0x30
#define MSG_HANDSHAKE_MS      0x40
#define MSG_HANDSHAKE_SWAP    0x50
#define MSG_HANDSHAKE_IO      0x60
#define MSG_HANDSHAKE_ACK     0x01

#define MSG_OK                0x00
#define MSG_ERROR             0xFF

#define MSG_KM_MEMORY_READY   0x20
#define MSG_KM_CORRUPTION     0x21
#define MSG_KM_COMPACT_REQ    0x22
#define MSG_KM_COMPACT_DONE   0x23

#define MSG_CPU_GET_CONTEXT   0x31
#define MSG_CPU_SET_CONTEXT   0x32
#define MSG_CPU_GET_INSTR     0x33
#define MSG_CPU_MEM_READ      0x34
#define MSG_CPU_MEM_WRITE     0x35

#define MSG_KS_CREATE_PROC    0x11
#define MSG_KS_CREATE_SEG     0x12
#define MSG_KS_DELETE_SEG     0x13
#define MSG_KS_SUSPEND_PROC   0x14
#define MSG_KS_RESUME_PROC    0x15
#define MSG_KS_EXIT_PROC      0x16
#define MSG_KS_MEM_READ       0x17
#define MSG_KS_MEM_WRITE      0x18
#define MSG_KS_COMPACT_OK     0x19

#define MSG_SWAP_WRITE        0x51
#define MSG_SWAP_READ         0x52

#define MSG_MS_WRITE          0x41
#define MSG_MS_READ           0x42

static inline int send_all(int fd, const void *buf, size_t size) {
    size_t total = 0;
    const uint8_t *ptr = buf;

    while (total < size) {
        ssize_t sent = send(fd, ptr + total, size - total, MSG_NOSIGNAL);
        if (sent <= 0) return -1;
        total += sent;
    }
    return 0;
}

static inline int recv_all(int fd, void *buf, size_t size) {
    size_t total = 0;
    uint8_t *ptr = buf;

    while (total < size) {
        ssize_t recvd = recv(fd, ptr + total, size - total, MSG_WAITALL);
        if (recvd <= 0) return -1;
        total += recvd;
    }
    return 0;
}

static inline int send_msg(int fd, uint8_t tipo, const void *payload, uint32_t size) {
    if (send_all(fd, &tipo, sizeof(tipo)) < 0) return -1;
    if (send_all(fd, &size, sizeof(size)) < 0) return -1;
    if (size > 0 && payload != NULL) {
        if (send_all(fd, payload, size) < 0) return -1;
    }
    return 0;
}

static inline void *recv_msg(int fd, uint8_t *out_tipo, uint32_t *out_size) {
    if (recv_all(fd, out_tipo, sizeof(*out_tipo)) < 0) return NULL;
    if (recv_all(fd, out_size, sizeof(*out_size)) < 0) return NULL;

    if (*out_size == 0) return NULL;

    void *buf = malloc(*out_size);
    if (!buf) return NULL;

    if (recv_all(fd, buf, *out_size) < 0) {
        free(buf);
        return NULL;
    }

    return buf;
}

static inline int send_signal(int fd, uint8_t tipo) {
    return send_msg(fd, tipo, NULL, 0);
}

#endif
