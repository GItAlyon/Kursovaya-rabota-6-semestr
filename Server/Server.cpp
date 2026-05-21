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

// КРОССПЛАТФОРМЕННАЯ ОБЁРТКА ДЛЯ СОКЕТОВ
#ifdef _WIN32
#include <winsock2.h>               // Windows: подключение Winsock (Windows Sockets)
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef SOCKET socket_t;            // В Windows сокет имеет тип SOCKET
#define close_socket closesocket    // Закрытие сокета в Windows через closesocket()
#else
#include <sys/socket.h>             // Linux/Unix: подключение POSIX-сокетов
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
typedef int socket_t;               // В Linux сокет — это обычный целочисленный дескриптор
#define close_socket close          // Закрытие сокета в Linux через close()
#define INVALID_SOCKET -1
#endif

using namespace std;

unique_ptr<Database> g_db;
unique_ptr<Crypto> g_crypto;
atomic<bool> server_running(true);
map<socket_t, string> client_sockets;
mutex clients_mutex;

// КЛАСС ОБРАБОТЧИК КЛИЕНТА (ЗАПУСКАЕТСЯ В ОТДЕЛЬНОМ ПОТОКЕ)
class ClientHandler {
private:
    socket_t client_fd;      // Сокет для общения с этим клиентом
    string username;         // Имя пользователя (после LOGIN)
    thread handler_thread;   // Поток, в котором работает этот обработчик
    atomic<bool> is_running; // Флаг работы потока (атомарный — безопасный для потоков)

public:
    // Конструктор: запоминает сокет и запускает поток
    ClientHandler(socket_t fd) : client_fd(fd), is_running(true) {
        handler_thread = thread(&ClientHandler::run, this);
    }
    // Деструктор: ждём завершения потока
    ~ClientHandler() {
        if (handler_thread.joinable()) {
            handler_thread.join();
        }
    }

    // ОСНОВНОЙ МЕТОД ОБРАБОТКИ КЛИЕНТА
    void run() {
        Message msg; // Буфер для приёма сообщений

        // Цикл работает, пока клиент подключён и сервер не остановлен
        while (is_running && server_running) {
            // ПРИЁМ СООБЩЕНИЯ ОТ КЛИЕНТА
            // recv() блокирует поток, пока данные не придут
            int bytes = recv(client_fd, reinterpret_cast<char*>(&msg), sizeof(msg), 0);

            if (bytes <= 0) break;  // Клиент отключился или ошибка

            // ОБРАБОТКА В ЗАВИСИМОСТИ ОТ ТИПА СООБЩЕНИЯ
            switch (msg.type) {
            // ОБРАБОТКА LOGIN (АУТЕНТИФИКАЦИЯ)
            case MessageType::LOGIN: {
                username = msg.from;
                g_db->user_exists(username);    // Добавляем пользователя в БД (если его там нет)

                lock_guard<mutex> lock(clients_mutex);   // МЬЮТЕКС: блокируем доступ к общему списку клиентов
                client_sockets[client_fd] = username;    // Сокет → имя
                g_logger.log("USER_LOGIN", username);

                // Отправка оффлайн сообщений
                // Получаем все не доставленные сообщения для этого пользователя
                auto undelivered = g_db->get_undelivered_messages(username);

                if (!undelivered.empty()) {
                    g_logger.log("OFFLINE", "Found " + to_string(undelivered.size()) + " messages for " + username);
                    // Отправляем каждое сообщение как PRIVATE_MESSAGE
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

            // ОБРАБОТКА SEND_PRIVATE (ПЕРЕСЫЛКА СООБЩЕНИЯ)
            case MessageType::SEND_PRIVATE: {
                // Не сохраняем пустые сообщения
                if (strlen(msg.content) == 0) break;
                // Получаем содержимое сообщения
                string content = msg.content;
                // Если сообщение зашифровано - расшифровываем
                if (msg.encrypted) {
                    Crypto crypto;
                    vector<unsigned char> cipher(content.begin(), content.end());
                    content = crypto.decrypt(cipher);
                }
                // Логируем сообщение
                g_logger.log_message(msg.from, msg.to, msg.content, true);
                // Сохраняем в базу данных (для истории)
                g_db->save_message(msg.from, msg.to, msg.content);
                // МЬЮТЕКС: защищаем доступ к списку клиентов
                lock_guard<mutex> lock(clients_mutex);

                // Отладочный вывод: кого ищем
                //g_logger.log("DEBUG", string("Looking for user: ") + msg.to);

                bool delivered = false;
                // Перебираем всех подключённых клиентов
                for (auto& pair : client_sockets) {
                    // Если нашли получателя
                    if (pair.second == msg.to) {
                        // Создаём новое сообщение для пересылки
                        Message forward_msg;
                        forward_msg.type = MessageType::PRIVATE_MESSAGE;  // Меняем тип!
                        strcpy(forward_msg.from, msg.from);
                        strcpy(forward_msg.to, msg.to);
                        strcpy(forward_msg.content, msg.content);
                        forward_msg.timestamp = msg.timestamp;
                        // ОТПРАВКА ПОЛУЧАТЕЛЮ
                        int result = send(pair.first, reinterpret_cast<char*>(&forward_msg), sizeof(forward_msg), 0);
                        g_logger.log("DEBUG", string("Send result: ") + to_string(result));
                        delivered = true;
                        // Помечаем сообщение в БД как доставленное
                        g_db->mark_message_delivered(msg.from, msg.to, msg.content);
                        g_logger.log("MESSAGE_DELIVERED", string(msg.from) + " -> " + string(msg.to));
                        break;   // Выходим из цикла
                    }
                }
                // Если получатель не в сети — сообщение сохранено в БД (офлайн)
                if (!delivered) {
                    g_logger.log("MESSAGE_SAVED_OFFLINE", string(msg.from) + " -> " + string(msg.to));
                }
                break;
            }

            // ОБРАБОТКА GET_HISTORY (ЗАПРОС ИСТОРИИ)
            case MessageType::GET_HISTORY: {
                // Получаем последние 100 сообщений между двумя пользователями
                auto history = g_db->get_conversation(msg.from, msg.to, 100);
                // Отправляем каждое сообщение как HISTORY_RESPONSE
                for (const auto& h : history) {
                    Message hist_msg;
                    hist_msg.type = MessageType::HISTORY_RESPONSE;
                    strcpy(hist_msg.from, get<1>(h).c_str());
                    strcpy(hist_msg.content, get<2>(h).c_str());
                    send(client_fd, (char*)&hist_msg, sizeof(hist_msg), 0);
                }
                break;
            }
            // ОБРАБОТКА LOGOUT (ВЫХОД)
            case MessageType::LOGOUT: {
                g_logger.log("USER_LOGOUT", username);
                is_running = false;  // Останавливаем цикл обработки
                break;
            }
            }
        }
        // Удаляем клиента из списка активных
        {
            lock_guard<mutex> lock(clients_mutex);
            client_sockets.erase(client_fd);
        }
        // Закрываем сокет
        close_socket(client_fd);
    }
};

// ГЛАВНАЯ ФУНКЦИЯ СЕРВЕРА
int main() { 
#ifdef _WIN32
    SetConsoleOutputCP(65001);  // UTF-8 для русских символов
    SetConsoleCP(65001);
    // Инициализация Winsock (обязательно для Windows)
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
    // Создание объектов базы данных, шифрования и логгера
    g_db = make_unique<Database>();
    g_crypto = make_unique<Crypto>();
    g_logger.log_server_start();

    // 1. СОЗДАНИЕ СОКЕТА
    // AF_INET = IPv4, SOCK_STREAM = TCP (надёжная доставка)
    socket_t server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == INVALID_SOCKET) {
        g_logger.log("ERROR", "Failed to create socket");
        return 1;
    }

    // 2. НАСТРОЙКА ПАРАМЕТРОВ СОКЕТА
    // SO_REUSEADDR позволяет переиспользовать порт после перезапуска сервера
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

    // 3. НАСТРОЙКА АДРЕСА
    sockaddr_in addr{};
    addr.sin_family = AF_INET;           // IPv4
    addr.sin_port = htons(PORT);         // Порт 8888 (htons = host to network short)
    addr.sin_addr.s_addr = INADDR_ANY;   // Слушаем все сетевые интерфейсы
    
    // 4. ПРИВЯЗКА СОКЕТА К АДРЕСУ И ПОРТУ
    if (bind(server_fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        g_logger.log("ERROR", "Failed to bind socket");
        return 1;
    }

    // 5. НАЧАЛО ПРОСЛУШИВАНИЯ
    // Очередь ожидания — до 10 подключений
    if (listen(server_fd, 10) < 0) {
        g_logger.log("ERROR", "Failed to listen");
        return 1;
    }

    g_logger.log("SERVER_READY", "Listening on port " + to_string(PORT));

    vector<unique_ptr<ClientHandler>> clients;    // Вектор для хранения всех обработчиков клиентов

    while (server_running) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        // БЛОКИРУЮЩИЙ ВЫЗОВ: ждёт нового клиента
        // При подключении возвращает НОВЫЙ сокет для общения с этим клиентом
        socket_t client_fd = accept(server_fd, (sockaddr*)&client_addr, &client_len);

        if (client_fd != INVALID_SOCKET) {
            clients.push_back(make_unique<ClientHandler>(client_fd));      // Создаём обработчик клиента (запускается в отдельном потоке)
            char ip[INET_ADDRSTRLEN];                                      // Преобразуем IP-адрес из бинарного формата в строку
            inet_ntop(AF_INET, &client_addr.sin_addr, ip, INET_ADDRSTRLEN);
            g_logger.log("NEW_CONNECTION", ip);
        }
    }

    close_socket(server_fd);     // Закрытие серверного сокета
    g_logger.log_server_stop();

#ifdef _WIN32
    WSACleanup();     // Завершение работы Winsock
#endif

    return 0;
}
