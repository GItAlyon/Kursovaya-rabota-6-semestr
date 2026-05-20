#pragma once
#include <sqlite3.h>
#include <string>
#include <vector>
#include <tuple>
#include <iostream>

class Database {
private:
    sqlite3* db;

public:
    Database(const std::string& filename = "messages.db") : db(nullptr) {
        sqlite3_open(filename.c_str(), &db);
        initialize_tables();
    }

    ~Database() {
        if (db) sqlite3_close(db);
    }

    void initialize_tables() {
        const char* sql = R"(
            CREATE TABLE IF NOT EXISTS users (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                username TEXT UNIQUE NOT NULL,
                created_at DATETIME DEFAULT CURRENT_TIMESTAMP
            );
            
            CREATE TABLE IF NOT EXISTS messages (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,
                from_user TEXT NOT NULL,
                to_user TEXT NOT NULL,
                content TEXT NOT NULL,
                delivered INTEGER DEFAULT 0
            );
            CREATE INDEX IF NOT EXISTS idx_undelivered ON messages(to_user, delivered);
        )";

        char* err_msg = nullptr;
        sqlite3_exec(db, sql, nullptr, nullptr, &err_msg);
    }

    bool save_message(const std::string& from, const std::string& to, const std::string& content) {
        std::string sql = "INSERT INTO messages (from_user, to_user, content, delivered) "
            "VALUES (?, ?, ?, 0);";

        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, from.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, to.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, content.c_str(), -1, SQLITE_STATIC);

        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        return rc == SQLITE_DONE;
    }

    bool user_exists(const std::string& username) {
        std::string sql = "INSERT OR IGNORE INTO users (username) VALUES (?);";
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);
        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        return true;
    }

    // Метод для получения истории сообщений
    std::vector<std::tuple<std::string, std::string, std::string>>
        get_conversation(const std::string& user, const std::string& with_user, int limit = 50) {
        std::vector<std::tuple<std::string, std::string, std::string>> result;

        std::string sql = "SELECT timestamp, from_user, content FROM messages "
            "WHERE from_user=? OR to_user=? "
            "ORDER BY timestamp DESC LIMIT ?;";

        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, user.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, user.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 3, limit);

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            result.push_back({
                reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)),
                reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)),
                reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2))
                });
        }

        sqlite3_finalize(stmt);
        return result;
    }

    // Получить не доставленные сообщения для пользователя
    std::vector<std::tuple<std::string, std::string, std::string>>
        get_undelivered_messages(const std::string& username) {
        std::vector<std::tuple<std::string, std::string, std::string>> result;

        std::string sql = "SELECT timestamp, from_user, content FROM messages "
            "WHERE to_user=? AND delivered=0 "
            "ORDER BY timestamp ASC;";

        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            result.push_back({
                reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)),
                reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)),
                reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2))
                });
        }

        sqlite3_finalize(stmt);
        return result;
    }

    // Пометить сообщение как доставленное (конкретное)
    void mark_message_delivered(const std::string& from_user, const std::string& to_user, const std::string& content) {
        std::string sql = "UPDATE messages SET delivered=1 WHERE from_user=? AND to_user=? AND content=? AND delivered=0;";
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, from_user.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, to_user.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, content.c_str(), -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    // Пометить ВСЕ сообщения пользователя как доставленные (при логине)
    void mark_all_delivered(const std::string& to_user) {
        std::string sql = "UPDATE messages SET delivered=1 WHERE to_user=? AND delivered=0;";
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, to_user.c_str(), -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
};
