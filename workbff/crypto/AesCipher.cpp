#include "crypto/AesCipher.h"

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

namespace {

QByteArray transform(int enc, const QByteArray& input, const QByteArray& key, const QByteArray& iv)
{
    QByteArray output;
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
        return output;

    int ok = EVP_CipherInit_ex(ctx, EVP_aes_256_cbc(), nullptr,
                               reinterpret_cast<const unsigned char*>(key.constData()),
                               reinterpret_cast<const unsigned char*>(iv.constData()),
                               enc);
    if (ok == 1) {
        output.resize(input.size() + EVP_CIPHER_block_size(EVP_aes_256_cbc()));
        int outLen = 0;
        int total = 0;
        ok = EVP_CipherUpdate(ctx, reinterpret_cast<unsigned char*>(output.data()), &outLen,
                              reinterpret_cast<const unsigned char*>(input.constData()),
                              input.size());
        if (ok == 1) {
            total = outLen;
            ok = EVP_CipherFinal_ex(ctx, reinterpret_cast<unsigned char*>(output.data()) + outLen,
                                    &outLen);
            if (ok == 1)
                total += outLen;
        }
        output.resize(total);
    }

    EVP_CIPHER_CTX_free(ctx);
    return ok == 1 ? output : QByteArray();
}

} // namespace

QByteArray AesCipher::deriveKey(const QString& password)
{
    const QByteArray data = password.toUtf8();
    QByteArray key(32, Qt::Uninitialized);
    SHA256(reinterpret_cast<const unsigned char*>(data.constData()),
           static_cast<size_t>(data.size()),
           reinterpret_cast<unsigned char*>(key.data()));
    return key;
}

QByteArray AesCipher::encrypt(const QByteArray& plain, const QByteArray& key)
{
    QByteArray iv(16, Qt::Uninitialized);
    if (RAND_bytes(reinterpret_cast<unsigned char*>(iv.data()), iv.size()) != 1)
        return {};
    const QByteArray body = transform(1, plain, key, iv);
    if (body.isEmpty())
        return {};
    return iv + body;
}

QByteArray AesCipher::decrypt(const QByteArray& cipher, const QByteArray& key)
{
    if (cipher.size() < 16)
        return {};
    const QByteArray iv = cipher.left(16);
    const QByteArray body = cipher.mid(16);
    return transform(0, body, key, iv);
}
