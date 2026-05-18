#define _CRT_SECURE_NO_WARNINGS
#include "../Server/Constants.h"
#include <iostream>
#include <thread>
#include <atomic>
#include <string>
#include <chrono>
#include <fstream>
#include <vector>
#include <map>
#include <algorithm>

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

const string SAVE_FILE = "last_user.txt";
const string CHATS_FILE = "chats_";

class Client {
private:
    socket_t sock_fd;
    string username;
    atomic<bool> is_running;
    thread send_thread, recv_thread;
    string current_chat_with;  // Текущий собеседник
    vector<string> chat_list;   // Список чатов
    map<string, vector<pair<string, string>>> message_history; // Кэш сообщений

public:
    Client() : sock_fd(INVALID_SOCKET), is_running(true), current_chat_with("") {
#ifdef _WIN32
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
    }

    ~Client() {
        save_chat_history();
        save_chats();
        is_running = false;
        if (sock_fd != INVALID_SOCKET) close_socket(sock_fd);
#ifdef _WIN32
        WSACleanup();
#endif
    }

    bool connect_to_server(const string& server_ip) {
        sock_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (sock_fd == INVALID_SOCKET) return false;

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(PORT);
        inet_pton(AF_INET, server_ip.c_str(), &addr.sin_addr);

        return ::connect(sock_fd, (sockaddr*)&addr, sizeof(addr)) == 0;
    }

    bool login(const string& user) {
        username = user;
        Message msg;
        msg.type = MessageType::LOGIN;
        strcpy(msg.from, user.c_str());

        if (send(sock_fd, (char*)&msg, sizeof(msg), 0) > 0) {
            save_last_user(user);
            load_chats();  // Загружаем сохранённые чаты
            load_chat_history();
            return true;
        }
        return false;
    }

    void save_last_user(const string& user) {
        ofstream file(SAVE_FILE);
        if (file.is_open()) {
            file << user << endl;
            file.close();
        }
    }

    string get_last_user() {
        ifstream file(SAVE_FILE);
        if (file.is_open()) {
            string user;
            getline(file, user);
            file.close();
            return user;
        }
        return "";
    }

    // Загрузка списка чатов из файла
    void load_chats() {
        string filename = CHATS_FILE + username + ".txt";
        ifstream file(filename);
        if (file.is_open()) {
            string chat;
            while (getline(file, chat)) {
                if (!chat.empty() && chat != username) {
                    chat_list.push_back(chat);
                }
            }
            file.close();
        }

        // Добавляем стандартные чаты, если их нет
        if (chat_list.empty()) {
        }
    }

    // Сохранение списка чатов
    void save_chats() {
        string filename = CHATS_FILE + username + ".txt";
        ofstream file(filename);
        if (file.is_open()) {
            for (const auto& chat : chat_list) {
                file << chat << endl;
            }
            file.close();
        }
    }

    // Добавление нового чата
    void add_chat(const string& user) {
        if (user == username) {
            cout << "You cannot chat with yourself!" << endl;
            return;
        }

        // Проверяем, есть ли уже такой чат
        for (const auto& chat : chat_list) {
            if (chat == user) {
                cout << "Chat with " << user << " already exists!" << endl;
                return;
            }
        }

        chat_list.push_back(user);
        save_chats();
        cout << "Chat with " << user << " created!" << endl;
    }

    // Вывод списка чатов
    void show_chat_list() {
        cout << "\nYOUR CHATS:" << endl;

        if (chat_list.empty()) {
            cout << "  No chats yet." << endl;
        }
        else {
            for (size_t i = 0; i < chat_list.size(); i++) {
                cout << "  " << i + 1 << ". " << chat_list[i];
                if (chat_list[i] == current_chat_with) {
                    cout << " (current)";
                }
                cout << endl;
            }
        }

        cout << "  " << chat_list.size() + 1 << ". Create new chat" << endl;
        cout << "  " << chat_list.size() + 2 << ". Back to main menu" << endl;
        cout << "Choice: ";
    }

    // Выбор собеседника
    bool select_chat() {
        while (true) {
            show_chat_list();

            int choice;
            cin >> choice;
            cin.ignore();

            if (choice >= 1 && choice <= (int)chat_list.size()) {
                current_chat_with = chat_list[choice - 1];

                current_chat_with = chat_list[choice - 1];

                // Очищаем историю от мусора перед показом
                auto& history = message_history[current_chat_with];
                vector<pair<string, string>> clean_history;
                for (const auto& msg : history) {
                    if (!msg.second.empty() && msg.first != "[]" && msg.first.length() > 0) {
                        clean_history.push_back(msg);
                    }
                }
                message_history[current_chat_with] = clean_history;

                cout << "\nNow chatting with: " << current_chat_with << endl;
                cout << "Type your message (or /back to change chat, /quit to exit)\n" << endl;
                return true;
            }
            else if (choice == chat_list.size() + 1) {
                // Создать новый чат
                cout << "Enter username to chat with: ";
                string new_chat;
                getline(cin, new_chat);
                if (!new_chat.empty() && new_chat != username) {
                    add_chat(new_chat);
                    current_chat_with = new_chat;

                    // Запрашиваем историю для нового чата
                    Message hist_msg;
                    hist_msg.type = MessageType::GET_HISTORY;
                    strcpy(hist_msg.from, username.c_str());
                    strcpy(hist_msg.to, current_chat_with.c_str());
                    send(sock_fd, (char*)&hist_msg, sizeof(hist_msg), 0);

                    cout << "\nNow chatting with: " << current_chat_with << endl;
                    return true;
                }
                else {
                    cout << "Invalid username!" << endl;
                }
            }
            else if (choice == chat_list.size() + 2) {
                return false;
            }
            else {
                cout << "Invalid choice!" << endl;
            }
        }
    }

    void send_message(const string& to, const string& content) {
        if (content.empty()) return;

        // Сразу сохраняем в файл
        string filename = "history_" + username + "_" + to + ".txt";
        ofstream file(filename, ios::app);
        if (file.is_open()) {
            file << username << "|" << content << endl;
            file.close();
        }

        Message msg;
        msg.type = MessageType::SEND_PRIVATE;
        strcpy(msg.from, username.c_str());
        strcpy(msg.to, to.c_str());
        strcpy(msg.content, content.c_str());
        send(sock_fd, (char*)&msg, sizeof(msg), 0);

        message_history[to].push_back({ username, content });
    }

    void receive_loop() {
        Message msg;
        while (is_running) {
            int bytes = recv(sock_fd, (char*)&msg, sizeof(msg), 0);
            if (bytes <= 0) break;

            if (msg.type == MessageType::PRIVATE_MESSAGE) {
                string from_user = msg.from;
                string content = msg.content;

                if (content.empty()) continue;

                // Сохраняем в кэш
                message_history[from_user].push_back({ from_user, content });

                // Сохраняем сразу в файл
                string filename = "history_" + username + "_" + from_user + ".txt";
                ofstream file(filename, ios::app);
                if (file.is_open()) {
                    file << from_user << "|" << content << endl;
                    file.close();
                }

                // Добавляем в список чатов
                bool chat_exists = false;
                for (const auto& chat : chat_list) {
                    if (chat == from_user) {
                        chat_exists = true;
                        break;
                    }
                }
                if (!chat_exists && from_user != username) {
                    chat_list.push_back(from_user);
                    save_chats();
                }

                // Если мы в чате с отправителем - показываем сообщение
                if (current_chat_with == from_user) {
                    cout << "[" << from_user << "]: " << content << endl;
                    cout << "[" << username << " -> " << current_chat_with << "]> " << flush;
                }
                else if (current_chat_with.empty()) {
                    cout << "\n[!] New message from " << from_user << endl;
                    cout << "[" << username << "]> " << flush;
                }
                else {
                    cout << "\n[!] New message from " << from_user << " (not in this chat)" << endl;
                    cout << "[" << username << " -> " << current_chat_with << "]> " << flush;
                }
            }
            else if (msg.type == MessageType::HISTORY_RESPONSE) {
                string from_user = msg.from;
                string content = msg.content;
                if (!content.empty() && from_user.length() > 0) {
                    message_history[from_user].push_back({ from_user, content });
                }
            }
        }
    }

    void show_main_menu() {
        cout << "\nMESSENGER - " << username << endl;
        cout << "  1. My chats" << endl;
        cout << "  2. Create new chat" << endl;
        cout << "  3. Exit" << endl;
        cout << "Choice: ";
    }

    // Сохраняем историю чата в файл
    void save_chat_history() {
        for (const auto& chat : chat_list) {
            string filename = "history_" + username + "_" + chat + ".txt";
            ofstream file(filename);
            if (file.is_open()) {
                auto& history = message_history[chat];
                for (const auto& msg : history) {
                    file << msg.first << "|" << msg.second << endl;
                }
                file.close();
            }
        }
    }

    // Загружаем историю чата из файла
    void load_chat_history() {
        for (const auto& chat : chat_list) {
            string filename = "history_" + username + "_" + chat + ".txt";
            ifstream file(filename);
            if (file.is_open()) {
                string line;
                while (getline(file, line)) {
                    size_t pos = line.find('|');
                    if (pos != string::npos) {
                        string from = line.substr(0, pos);
                        string content = line.substr(pos + 1);
                        if (!content.empty()) {
                            message_history[chat].push_back({ from, content });
                        }
                    }
                }
                file.close();
            }
        }
    }

    void show_history() {
        if (current_chat_with.empty()) {
            cout << "Select a chat first!" << endl;
            return;
        }

        cout << "\nMessage history with " << current_chat_with << endl;
        auto& history = message_history[current_chat_with];
        if (history.empty()) {
            cout << "No messages yet. Send a message to start the conversation!" << endl;
        }
        else {
            for (const auto& msg : history) {
                if (msg.first == username) {
                    cout << "[You]: " << msg.second << endl;
                }
                else {
                    cout << "[" << msg.first << "]: " << msg.second << endl;
                }
            }
        }
    }

    void chat_loop() {
        string input;

        show_chat_history();

        while (is_running && !current_chat_with.empty()) {
            cout << "[" << username << " -> " << current_chat_with << "]> " << flush;
            getline(cin, input);

            if (input == "/back") {
                current_chat_with = "";
                cout << "\nReturning to main menu..." << endl;
                return;
            }
            else if (input == "/quit") {
                cout << "Goodbye!" << endl;
                is_running = false;
                exit(0);
            }
            else if (input == "/history") {
                show_full_history();
            }
            else if (input == "/clear") {
#ifdef _WIN32
                system("cls");
#else
                system("clear");
#endif
                show_chat_history();
            }
            else if (!input.empty()) {
                send_message(current_chat_with, input);
            }
        }
    }

    // Показываем последние сообщения чата (и свои, и чужие)
    void show_chat_history() {
        auto& history = message_history[current_chat_with];
        if (history.empty()) {
            cout << "\nNo messages yet. Say hello!" << endl;
            return;
        }

        cout << "\nLast messages with " << current_chat_with << ":" << endl;

        int total = (int)history.size();
        int start = total > 15 ? total - 15 : 0;

        for (int i = start; i < total; i++) {
            if (history[i].first == username) {
                cout << "You: " << history[i].second << endl;
            }
            else {
                cout << history[i].first << ": " << history[i].second << endl;
            }
        }
        cout << endl;
    }

    // Показываем всю историю чата (и свои, и чужие)
    void show_full_history() {
        auto& history = message_history[current_chat_with];
        if (history.empty()) {
            cout << "\nNo messages yet!" << endl;
            return;
        }

        cout << "\nConversation with " << current_chat_with << ":" << endl;

        for (size_t i = 0; i < history.size(); i++) {
            if (history[i].first == username) {
                cout << "You: " << history[i].second << endl;
            }
            else {
                cout << history[i].first << ": " << history[i].second << endl;
            }
        }
        cout << endl;
    }

    void start() {
        // Запускаем поток получения сообщений
        recv_thread = thread(&Client::receive_loop, this);

        // Основной цикл приложения
        while (is_running) {
            show_main_menu();

            int choice;
            cin >> choice;
            cin.ignore();

            switch (choice) {
            case 1: // Мои чаты
                if (select_chat()) {
                    chat_loop();
                }
                break;

            case 2: // Создать новый чат
            {
                cout << "Enter username to chat with: ";
                string new_chat;
                getline(cin, new_chat);
                if (!new_chat.empty() && new_chat != username) {
                    add_chat(new_chat);
                    current_chat_with = new_chat;
                    chat_loop();
                }
                else {
                    cout << "Invalid username!" << endl;
                }
            }
            break;

            case 3: // Выход
                cout << "Goodbye!" << endl;
                is_running = false;
                exit(0);  
                break;

            default:
                cout << "Invalid choice!" << endl;
            }
        }
    }
};

// Функция показа меню входа
void show_auth_menu() {
    cout << "\nMESSENGER CLIENT" << endl;
    cout << "  1. Login (existing user)" << endl;
    cout << "  2. Register (new user)" << endl;
    cout << "  3. Continue as last user" << endl;
    cout << "  4. Exit" << endl;
    cout << "Choice: ";
}

vector<string> get_saved_users() {
    vector<string> users;
    ifstream file("users.txt");
    if (file.is_open()) {
        string user;
        while (getline(file, user)) {
            users.push_back(user);
        }
        file.close();
    }
    return users;
}

void save_user(const string& user) {
    vector<string> users = get_saved_users();
    for (const auto& u : users) {
        if (u == user) return;
    }

    ofstream file("users.txt", ios::app);
    if (file.is_open()) {
        file << user << endl;
        file.close();
    }
}

int main(int argc, char* argv[]) {
    string server_ip = "127.0.0.1";

    if (argc >= 2) {
        server_ip = argv[1];
    }

    Client client;

    if (!client.connect_to_server(server_ip)) {
        cerr << "Failed to connect to server at " << server_ip << ":" << PORT << endl;
        cout << "Make sure the server is running!" << endl;
        return 1;
    }

    string username;
    int choice = 0;
    string last_user = client.get_last_user();

    while (choice != 4) {
        show_auth_menu();
        cin >> choice;
        cin.ignore();

        if (choice == 1) {
            vector<string> users = get_saved_users();
            if (users.empty()) {
                cout << "No saved users. Please register first." << endl;
                continue;
            }

            cout << "\nSaved users:" << endl;
            for (size_t i = 0; i < users.size(); i++) {
                cout << "  " << i + 1 << ". " << users[i] << endl;
            }
            cout << "Select user (1-" << users.size() << "): ";
            int user_choice;
            cin >> user_choice;
            cin.ignore();

            if (user_choice >= 1 && user_choice <= (int)users.size()) {
                username = users[user_choice - 1];
                break;
            }
        }
        else if (choice == 2) {
            cout << "Enter username: ";
            getline(cin, username);

            if (username.empty()) {
                cout << "Username cannot be empty!" << endl;
                continue;
            }

            save_user(username);
            break;
        }
        else if (choice == 3) {
            if (last_user.empty()) {
                cout << "No last user found. Please login or register first." << endl;
                continue;
            }
            username = last_user;
            break;
        }
        else if (choice == 4) {
            return 0;
        }
        else {
            cout << "Invalid choice!" << endl;
        }
    }

    if (!client.login(username)) {
        cerr << "Failed to login as " << username << endl;
        return 1;
    }

    cout << "\nConnected as '" << username << "'" << endl;
    client.start();
    cout << "Application closed." << endl;
    return 0;
}