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
        key.resize(32);
        iv.resize(16);
        RAND_bytes(key.data(), key.size());
        RAND_bytes(iv.data(), iv.size());
    }

    std::vector<unsigned char> encrypt(const std::string& plaintext) {
        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        std::vector<unsigned char> ciphertext(plaintext.size() + EVP_MAX_BLOCK_LENGTH);
        int len = 0, ciphertext_len = 0;

        EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, key.data(), iv.data());
        EVP_EncryptUpdate(ctx, ciphertext.data(), &len,
            reinterpret_cast<const unsigned char*>(plaintext.c_str()),
            plaintext.size());
        ciphertext_len = len;
        EVP_EncryptFinal_ex(ctx, ciphertext.data() + len, &len);
        ciphertext_len += len;

        ciphertext.resize(ciphertext_len);
        EVP_CIPHER_CTX_free(ctx);
        return ciphertext;
    }
};