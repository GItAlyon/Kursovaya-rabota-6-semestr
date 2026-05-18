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
                content TEXT NOT NULL
            );
        )";

        char* err_msg = nullptr;
        sqlite3_exec(db, sql, nullptr, nullptr, &err_msg);
    }

    bool save_message(const std::string& from, const std::string& to,
        const std::string& content) {
        std::string sql = "INSERT INTO messages (from_user, to_user, content) "
            "VALUES (?, ?, ?);";

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
};