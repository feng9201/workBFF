#pragma once

#include <QByteArray>
#include <QString>

// AES-256-CBC 加解密（基于 OpenSSL EVP）
// 密文格式：16 字节随机 IV + 密文
class AesCipher
{
public:
    // 由密码派生 32 字节 AES-256 密钥（SHA-256）
    static QByteArray deriveKey(const QString& password);

    static QByteArray encrypt(const QByteArray& plain, const QByteArray& key);
    // 解密失败（密钥错误/数据损坏）返回空数组
    static QByteArray decrypt(const QByteArray& cipher, const QByteArray& key);
};
