// Copyright (c) 2013-2014 Plenluno All rights reserved.

#ifndef LIBNODE_CRYPTO_RSA_PUBLIC_KEY_H_
#define LIBNODE_CRYPTO_RSA_PUBLIC_KEY_H_

#include <libj/js_object.h>

namespace libj {
namespace node {
namespace crypto {
namespace rsa {

class PublicKey : LIBJ_JS_OBJECT(PublicKey)
};

}  // namespace rsa
}  // namespace crypto
}  // namespace node
}  // namespace libj

#include <libnode/impl/crypto/rsa/public_key.h>

#endif  // LIBNODE_CRYPTO_RSA_PUBLIC_KEY_H_
