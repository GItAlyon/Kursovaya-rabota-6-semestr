#pragma once
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <vector>
#include <string>

class Crypto {
private:
    std::vector<unsigned char> key;
    std::vector<unsigned char> iv;

public:
    Crypto() {
        // ФИКСИРОВАННЫЙ КЛЮЧ (32 байта) - ОДИНАКОВЫЙ ДЛЯ ВСЕХ
        key = {
            0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,
            0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F,0x10,
            0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,
            0x19,0x1A,0x1B,0x1C,0x1D,0x1E,0x1F,0x20
        };
        // ФИКСИРОВАННЫЙ ВЕКТОР ИНИЦИАЛИЗАЦИИ (16 байт)
        iv = {
            0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,
            0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F,0x10
        };
    }

    // Шифрование
    std::vector<unsigned char> encrypt(const std::string& plaintext) {
        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();  // Создаём контекст шифрования
        std::vector<unsigned char> ciphertext(plaintext.size() + EVP_MAX_BLOCK_LENGTH);
        int len = 0, ciphertext_len = 0;

        // Инициализация шифрования (AES-256-CBC)
        EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, key.data(), iv.data());
        // Шифрование данных
        EVP_EncryptUpdate(ctx, ciphertext.data(), &len,
            reinterpret_cast<const unsigned char*>(plaintext.c_str()),
            plaintext.size());
        ciphertext_len = len;
        // Финализация (добавляем padding до кратности 16 байт)
        EVP_EncryptFinal_ex(ctx, ciphertext.data() + len, &len);
        ciphertext_len += len;

        ciphertext.resize(ciphertext_len);
        EVP_CIPHER_CTX_free(ctx);
        return ciphertext;
    }

    // Расшифрование
    std::string decrypt(const std::vector<unsigned char>& ciphertext) {
        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        std::vector<unsigned char> plaintext(ciphertext.size());
        int len = 0, plaintext_len = 0;

        EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, key.data(), iv.data());
        EVP_DecryptUpdate(ctx, plaintext.data(), &len, ciphertext.data(), static_cast<int>(ciphertext.size()));
        plaintext_len = len;
        EVP_DecryptFinal_ex(ctx, plaintext.data() + len, &len);
        plaintext_len += len;

        plaintext.resize(plaintext_len);
        EVP_CIPHER_CTX_free(ctx);

        return std::string(plaintext.begin(), plaintext.end());
    }
};
