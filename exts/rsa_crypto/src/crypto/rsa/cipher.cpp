// Copyright (c) 2013-2014 Plenluno All rights reserved.

#include <libnode/crypto/rsa/cipher.h>
#include <libnode/crypto/rsa/detail/cipher.h>

namespace libj {
namespace node {
namespace crypto {
namespace rsa {

Cipher::Ptr Cipher::create(
    PublicKey::CPtr key,
    Cipher::Padding padding) {
    if (key) {
        return Ptr(new detail::crypto::rsa::Cipher<Cipher>(key, padding));
    } else {
        return null();
    }
}

}  // namespace rsa
}  // namespace crypto
}  // namespace node
}  // namespace libj
