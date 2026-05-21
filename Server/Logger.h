#pragma once
#include <string>
#include <fstream>
#include <mutex>
#include <chrono>
#include <iomanip>
#include <iostream>

class Logger {
private:
    std::ofstream log_file;      // Файловый поток для записи
    std::mutex log_mutex;        // Мьютекс для потокобезопасности
    bool console_output;         // Выводить ли в консоль

public:
    Logger(const std::string& filename = "server.log", bool console = true)
        : console_output(console) {
        log_file.open(filename, std::ios::app);  // Открываем в режиме добавления
    }

    ~Logger() {
        if (log_file.is_open()) log_file.close();
    }

    void log(const std::string& event, const std::string& detail = "") {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);

        std::lock_guard<std::mutex> lock(log_mutex);  // Блокируем мьютекс

        // Безопасная версия localtime для Windows
        struct tm time_info;
        localtime_s(&time_info, &time_t);

        if (log_file.is_open()) {
            log_file << std::put_time(&time_info, "[%Y-%m-%d %H:%M:%S] ")
                << event << ": " << detail << std::endl;
            log_file.flush();  // Немедленная запись на диск
        } 

        if (console_output) {
            std::cout << std::put_time(&time_info, "[%Y-%m-%d %H:%M:%S] ")
                << event << ": " << detail << std::endl;
        }
    }

    void log_message(const std::string& from, const std::string& to,
        const std::string& content, bool sent) {
        std::string direction = sent ? "SENT" : "RECEIVED";
        log("MESSAGE_" + direction, from + " -> " + to + ": " + content);
    }

    void log_server_start() { log("SERVER_START", "Server initialized"); }
    void log_server_stop() { log("SERVER_STOP", "Server shutting down"); }
};

extern Logger g_logger;  // Глобальный объект логгера
