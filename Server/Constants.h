#pragma once
#include <string>
#include <cstdint>
#include <cstring>

constexpr int PORT = 8888;
constexpr int MAX_USERNAME = 32;
constexpr int MAX_MESSAGE = 2048;

enum class MessageType : uint8_t {
    LOGIN = 1,
    LOGOUT = 2,
    SEND_PRIVATE = 3,
    PRIVATE_MESSAGE = 4,
    GET_HISTORY = 5,     
    HISTORY_RESPONSE = 6,
    SERVER_SHUTDOWN = 12
};

#pragma pack(push, 1)
struct Message {
    MessageType type;
    char from[MAX_USERNAME];
    char to[MAX_USERNAME];
    char content[MAX_MESSAGE];
    uint8_t encrypted;
    uint32_t seq_num;
    uint64_t timestamp;

    Message() : type(MessageType::LOGIN), encrypted(0),
        seq_num(0), timestamp(0) {
        memset(from, 0, MAX_USERNAME);
        memset(to, 0, MAX_USERNAME);
        memset(content, 0, MAX_MESSAGE);
    }
};
#pragma pack(pop)