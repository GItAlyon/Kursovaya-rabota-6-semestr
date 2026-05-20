#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS

#include "Constants.h"
#include "Logger.h"
#include "Database.h"
#include "Crypto.h"
#include "Logger.h"
#include <iostream>
#include <thread>
#include <vector>
#include <map>
#include <memory>
#include <atomic>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef SOCKET socket_t;
#define close_socket closesocket
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
typedef int socket_t;
#define close_socket close
#define INVALID_SOCKET -1
#endif

using namespace std;

unique_ptr<Database> g_db;
unique_ptr<Crypto> g_crypto;
atomic<bool> server_running(true);
map<socket_t, string> client_sockets;
mutex clients_mutex;

class ClientHandler {
private:
    socket_t client_fd;
    string username;
    thread handler_thread;
    atomic<bool> is_running;

public:
    ClientHandler(socket_t fd) : client_fd(fd), is_running(true) {
        handler_thread = thread(&ClientHandler::run, this);
    }

    ~ClientHandler() {
        if (handler_thread.joinable()) {
            handler_thread.join();
        }
    }

    void run() {
        Message msg;

        while (is_running && server_running) {
            int bytes = recv(client_fd, reinterpret_cast<char*>(&msg), sizeof(msg), 0);

            if (bytes <= 0) break;

            switch (msg.type) {
            case MessageType::LOGIN: {
                username = msg.from;
                g_db->user_exists(username);
                lock_guard<mutex> lock(clients_mutex);
                client_sockets[client_fd] = username;
                g_logger.log("USER_LOGIN", username);
                // Отправка оффлайн сообщений
                auto undelivered = g_db->get_undelivered_messages(username);

                if (!undelivered.empty()) {
                    g_logger.log("OFFLINE", "Found " + to_string(undelivered.size()) + " messages for " + username);

                    for (const auto& undelivered_msg : undelivered) {
                        Message offline_msg;
                        offline_msg.type = MessageType::PRIVATE_MESSAGE;
                        strcpy(offline_msg.from, std::get<1>(undelivered_msg).c_str());
                        strcpy(offline_msg.content, std::get<2>(undelivered_msg).c_str());
                        send(client_fd, (char*)&offline_msg, sizeof(offline_msg), 0);
                        g_logger.log("OFFLINE_DELIVERED", "From " + std::get<1>(undelivered_msg));
                    }

                    // Помечаем все сообщения как доставленные
                    g_db->mark_all_delivered(username);
                }
                break;
            }

            case MessageType::SEND_PRIVATE: {
                // Не сохраняем пустые сообщения
                if (strlen(msg.content) == 0) break;
                g_logger.log_message(msg.from, msg.to, msg.content, true);
                g_db->save_message(msg.from, msg.to, msg.content);

                lock_guard<mutex> lock(clients_mutex);

                // Отладочный вывод
                g_logger.log("DEBUG", string("Looking for user: ") + msg.to);

                bool delivered = false;
                for (auto& pair : client_sockets) {
                    g_logger.log("DEBUG", string("User online: ") + pair.second);

                    if (pair.second == msg.to) {
                        // Создаём новое сообщение для получателя
                        Message forward_msg;
                        forward_msg.type = MessageType::PRIVATE_MESSAGE;
                        strcpy(forward_msg.from, msg.from);
                        strcpy(forward_msg.to, msg.to);
                        strcpy(forward_msg.content, msg.content);
                        forward_msg.timestamp = msg.timestamp;

                        int result = send(pair.first, reinterpret_cast<char*>(&forward_msg), sizeof(forward_msg), 0);
                        g_logger.log("DEBUG", string("Send result: ") + to_string(result));
                        delivered = true;
                        // Помечаем как доставленное
                        g_db->mark_message_delivered(msg.from, msg.to, msg.content);
                        g_logger.log("MESSAGE_DELIVERED", string(msg.from) + " -> " + string(msg.to));
                        break;
                    }
                }

                if (!delivered) {
                    g_logger.log("MESSAGE_SAVED_OFFLINE", string(msg.from) + " -> " + string(msg.to));
                }
                break;
            }

            case MessageType::GET_HISTORY: {
                auto history = g_db->get_conversation(msg.from, msg.to, 100);
                for (const auto& h : history) {
                    Message hist_msg;
                    hist_msg.type = MessageType::HISTORY_RESPONSE;
                    strcpy(hist_msg.from, get<1>(h).c_str());
                    strcpy(hist_msg.content, get<2>(h).c_str());
                    send(client_fd, (char*)&hist_msg, sizeof(hist_msg), 0);
                }
                break;
            }

            case MessageType::LOGOUT: {
                g_logger.log("USER_LOGOUT", username);
                is_running = false;
                break;
            }
            }
        }

        {
            lock_guard<mutex> lock(clients_mutex);
            client_sockets.erase(client_fd);
        }
        close_socket(client_fd);
    }
};

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(65001); 
    SetConsoleCP(65001);
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

    g_db = make_unique<Database>();
    g_crypto = make_unique<Crypto>();
    g_logger.log_server_start();

    socket_t server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == INVALID_SOCKET) {
        g_logger.log("ERROR", "Failed to create socket");
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        g_logger.log("ERROR", "Failed to bind socket");
        return 1;
    }

    if (listen(server_fd, 10) < 0) {
        g_logger.log("ERROR", "Failed to listen");
        return 1;
    }

    g_logger.log("SERVER_READY", "Listening on port " + to_string(PORT));

    vector<unique_ptr<ClientHandler>> clients;

    while (server_running) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        socket_t client_fd = accept(server_fd, (sockaddr*)&client_addr, &client_len);

        if (client_fd != INVALID_SOCKET) {
            clients.push_back(make_unique<ClientHandler>(client_fd));
            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &client_addr.sin_addr, ip, INET_ADDRSTRLEN);
            g_logger.log("NEW_CONNECTION", ip);
        }
    }

    close_socket(server_fd);
    g_logger.log_server_stop();

#ifdef _WIN32
    WSACleanup();
#endif

    return 0;
}
