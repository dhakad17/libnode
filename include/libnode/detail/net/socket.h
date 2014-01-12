// Copyright (c) 2012-2013 Plenluno All rights reserved.

#ifndef LIBNODE_DETAIL_NET_SOCKET_H_
#define LIBNODE_DETAIL_NET_SOCKET_H_

#include <libnode/config.h>
#include <libnode/dns.h>
#include <libnode/net.h>
#include <libnode/process.h>
#include <libnode/string_decoder.h>
#include <libnode/timer.h>
#include <libnode/uv/error.h>
#include <libnode/detail/uv/stream_common.h>
#include <libnode/detail/events/event_emitter.h>

#include <libj/json.h>
#include <libj/this.h>

#include <assert.h>

namespace libj {
namespace node {
namespace detail {
namespace http {

class Parser;
class OutgoingMessage;

}  // namespace http
}  // namespace detail
}  // namespace node
}  // namespace libj

namespace libj {
namespace node {
namespace detail {
namespace net {

class Socket : public events::EventEmitter<node::net::Socket> {
 public:
    LIBJ_MUTABLE_DEFS(Socket, node::net::Socket);

    static Ptr create(libj::JsObject::CPtr options = libj::JsObject::null()) {
        LIBJ_STATIC_SYMBOL_DEF(OPTION_HANDLE, "handle");

        if (options) {
            Boolean allowHalfOpen =
                to<Boolean>(options->get(OPTION_ALLOW_HALF_OPEN));
            uv_file fd;
            if (to<uv_file>(options->get(OPTION_FD), &fd)) {
                return create(fd, allowHalfOpen);
            } else {
                uv::Stream* handle =
                    to<uv::Stream*>(options->get(OPTION_HANDLE));
                return create(handle, allowHalfOpen);
            }
        } else {
            return create(static_cast<uv::Stream*>(NULL), false);
        }
    }

    static Ptr create(uv::Stream* handle, Boolean allowHalfOpen) {
        Socket* sock = new Socket();
        sock->handle_ = handle;
        if (allowHalfOpen) sock->setFlag(ALLOW_HALF_OPEN);
        return create(sock);
    }

    virtual Boolean readable() const {
        return hasFlag(READABLE);
    }

    virtual Boolean writable() const {
        return hasFlag(WRITABLE);
    }

    virtual Boolean setEncoding(Buffer::Encoding enc) {
        switch (enc) {
        case Buffer::UTF8:
        case Buffer::UTF16BE:
        case Buffer::UTF16LE:
        case Buffer::UTF32BE:
        case Buffer::UTF32LE:
        case Buffer::HEX:
        case Buffer::NONE:
            decoder_ = StringDecoder::create(enc);
            return true;
        default:
            decoder_ = StringDecoder::null();
            return false;
        }
    }

    virtual Boolean pause() {
        setFlag(PAUSED);
        return handle_ && !hasFlag(CONNECTING) && !handle_->readStop();
    }

    virtual Boolean resume() {
        unsetFlag(PAUSED);
        return handle_ && !hasFlag(CONNECTING) && !handle_->readStart();
    }

    virtual Boolean write(
        const Value& data,
        Buffer::Encoding enc = Buffer::NONE) {
        return write(data, enc, JsFunction::null());
    }

    virtual Boolean end(
        const Value& data = UNDEFINED,
        Buffer::Encoding enc = Buffer::NONE) {
        if (hasFlag(CONNECTING) && !hasFlag(SHUTDOWN_QUEUED)) {
            if (!data.isUndefined()) write(data, enc);
            unsetFlag(WRITABLE);
            setFlag(SHUTDOWN_QUEUED);
        }

        if (!hasFlag(WRITABLE)) {
            return false;
        } else {
            unsetFlag(WRITABLE);
        }

        if (!data.isUndefined()) write(data, enc);

        if (!hasFlag(READABLE)) {
            return destroySoon();
        } else {
            setFlag(SHUTDOWN);
            uv::Shutdown* req = handle_->shutdown();
            if (req) {
                AfterShutdown::Ptr afterShutdown(new AfterShutdown(this));
                req->onComplete = afterShutdown;
                return true;
            } else {
                destroy(node::uv::Error::last());
                return false;
            }
        }
    }

    virtual Boolean destroy() {
        return destroy(Error::null());
    }

    virtual Boolean destroySoon() {
        if (hasFlag(DESTROYED)) return false;

        unsetFlag(WRITABLE);
        setFlag(DESTROY_SOON);
        if (pendingWriteReqs_) {
            return true;
        } else {
            return destroy();
        }
    }

    virtual libj::JsObject::Ptr address() {
        if (handle_ && handle_->type() == UV_TCP) {
            uv::Tcp* tcp = static_cast<uv::Tcp*>(handle_);
            return tcp->getSockName();
        } else {
            return libj::JsObject::null();
        }
    }

    virtual String::CPtr remoteAddress() {
        LIBJ_STATIC_SYMBOL_DEF(symAddress, "address");

        if (handle_ && handle_->type() == UV_TCP) {
            uv::Tcp* tcp = static_cast<uv::Tcp*>(handle_);
            return tcp->getPeerName()->getCPtr<String>(symAddress);
        } else {
            return String::null();
        }
    }

    virtual Int remotePort() {
        LIBJ_STATIC_SYMBOL_DEF(symPort, "port");

        if (handle_ && handle_->type() == UV_TCP) {
            uv::Tcp* tcp = static_cast<uv::Tcp*>(handle_);
            Int port = to<Int>(tcp->getPeerName()->get(symPort), -1);
            return port;
        } else {
            return -1;
        }
    }

    virtual Size bytesRead() const {
        return bytesRead_;
    }

    virtual Size bytesWritten() const {
        Size bytes = bytesDispatched_;
        Size len = connectBufQueue_->length();
        for (Size i = 0; i < len; i++) {
            bytes += connectBufQueue_->getCPtr<Buffer>(i)->length();
        }
        return bytes;
    }

    virtual Boolean connect(
        Int port,
        String::CPtr host = String::null(),
        JsFunction::Ptr cb = JsFunction::null()) {
        if (port < 0) {
            return false;
        } else {
            connect(port, host, String::null(), String::null(), cb);
            return true;
        }
    }

    virtual Boolean connect(
        String::CPtr path,
        JsFunction::Ptr cb = JsFunction::null()) {
        if (path) {
            connect(0, String::null(), String::null(), path, cb);
            return true;
        } else {
            return false;
        }
    }

    virtual void setTimeout(
        UInt timeout,
        JsFunction::Ptr callback = JsFunction::null()) {
        if (timeout) {
            startTimer(timeout);
            if (callback) {
                once(EVENT_TIMEOUT, callback);
            }
        } else {
            finishTimer();
            if (callback) {
                removeListener(EVENT_TIMEOUT, callback);
            }
        }
    }

    virtual Boolean setNoDelay(Boolean noDelay) {
        if (handle_ && handle_->type() == UV_TCP) {
            uv::Tcp* tcp = static_cast<uv::Tcp*>(handle_);
            return !tcp->setNoDelay(noDelay);
        } else {
            return false;
        }
    }

    virtual Boolean setKeepAlive(
        Boolean enable = false,
        UInt initialDelay = 0) {
        if (handle_ && handle_->type() == UV_TCP) {
            uv::Tcp* tcp = static_cast<uv::Tcp*>(handle_);
            return !tcp->setKeepAlive(enable, initialDelay / 1000);
        } else {
            return false;
        }
    }

 public:
    Buffer::Encoding encoding() const {
        return decoder_ ? decoder_->encoding() : Buffer::NONE;
    }

    Boolean write(
        const Value& data,
        JsFunction::Ptr cb) {
        return write(data, Buffer::NONE, cb);
    }

    Boolean write(
        const Value& data,
        Buffer::Encoding enc,
        JsFunction::Ptr cb) {
        Buffer::CPtr buf;
        String::CPtr str = toCPtr<String>(data);
        if (str) {
            if (enc == Buffer::NONE) enc = Buffer::UTF8;
            buf = Buffer::create(str, enc);
        } else {
            buf = toCPtr<Buffer>(data);
        }
        if (!buf) return false;

        if (hasFlag(CONNECTING)) {
            connectQueueSize_ += buf->length();
            if (!connectBufQueue_) {
                assert(!connectCbQueue_);
                connectBufQueue_ = JsArray::create();
                connectCbQueue_ = JsArray::create();
            }
            connectBufQueue_->push(buf);
            connectCbQueue_->push(cb);
            return false;
        }

        return writeBuffer(buf, cb);
    }

    Boolean destroy(Error::CPtr err) {
        return destroy(err, JsFunction::null());
    }

    Boolean connect(
        libj::JsObject::CPtr options,
        JsFunction::Ptr cb = JsFunction::null()) {
        if (!options) return false;

        Int port = -1;
        const Value& vPort = options->get(node::net::OPTION_PORT);
        if (!vPort.isUndefined()) {
            String::CPtr sPort = String::valueOf(vPort);
            if (sPort) {
                Long lPort = to<Long>(json::parse(sPort), -1);
                port = static_cast<Int>(lPort);
            }
        }

        String::CPtr path =
            options->getCPtr<String>(node::net::OPTION_PATH);
        String::CPtr host =
            options->getCPtr<String>(node::net::OPTION_HOST);
        String::CPtr localAddress =
            options->getCPtr<String>(node::net::OPTION_LOCAL_ADDRESS);

        if (path) {
            return connect(path, cb);
        } else if (port >= 0) {
            connect(port, host, localAddress, String::null(), cb);
            return true;
        } else {
            return false;
        }
    }

    JsFunction::Ptr setOnData(JsFunction::Ptr onData) {
        JsFunction::Ptr previous = onData_;
        onData_ = onData;
        return previous;
    }

    JsFunction::Ptr setOnEnd(JsFunction::Ptr onEnd) {
        JsFunction::Ptr previous = onEnd_;
        onEnd_ = onEnd;
        return previous;
    }

    JsFunction::Ptr setOnDrain(JsFunction::Ptr onDrain) {
        if (onDrain_) {
            removeListener(EVENT_DRAIN, onDrain_);
        }
        if (onDrain) {
            addListener(EVENT_DRAIN, onDrain);
        }
        JsFunction::Ptr previous = onDrain_;
        onDrain_ = onDrain;
        return previous;
    }

    JsFunction::Ptr setOnClose(JsFunction::Ptr onClose) {
        if (onClose_) {
            removeListener(EVENT_CLOSE, onClose_);
        }
        if (onClose) {
            addListener(EVENT_CLOSE, onClose);
        }
        JsFunction::Ptr previous = onClose_;
        onClose_ = onClose;
        return previous;
    }

    http::Parser* parser() const {
        return parser_;
    }

    void setParser(http::Parser* parser) {
        parser_ = parser;
    }

    http::OutgoingMessage* httpMessage() const {
        return httpMessage_;
    }

    void setHttpMessage(http::OutgoingMessage* msg) {
        httpMessage_ = msg;
    }

    void active() {
        if (hasTimer()) {
            finishTimer();
            startTimer(timeout_);
        }
    }

    void ref() {
        if (handle_) handle_->ref();
    }

    void unref() {
        if (handle_) handle_->unref();
    }

 private:
    static Ptr create(int fd, Boolean allowHalfOpen = false) {
        uv::Pipe* pipe = new uv::Pipe();
        pipe->open(fd);

        Socket* sock = new Socket();
        sock->handle_ = pipe;
        sock->setFlag(READABLE);
        sock->setFlag(WRITABLE);
        if (allowHalfOpen) sock->setFlag(ALLOW_HALF_OPEN);
        return create(sock);
    }

    static Ptr create(Socket* self) {
        LIBJ_STATIC_SYMBOL_DEF(EVENT_DESTROY, "destroy");

        initSocketHandle(self);
        Ptr socket(self);
        JsFunction::Ptr onDestroy(new OnDestroy(socket));
        socket->on(EVENT_DESTROY, onDestroy);
        return socket;
    }

    static void initSocketHandle(Socket* self) {
        self->flags_ = 0;
        self->pendingWriteReqs_ = 0;
        self->connectQueueSize_ = 0;
        self->bytesRead_ = 0;
        self->bytesDispatched_ = 0;

        uv::Stream* handle  = self->handle_;
        if (handle) {
            OnRead::Ptr onRead(new OnRead(self, handle));
            handle->setOnRead(onRead);
            handle->setOwner(self);
        }
    }

    static void connect(
        Socket* self,
        uv::Pipe* handle,
        String::CPtr path) {
        assert(self->hasFlag(CONNECTING));

        uv::Connect* req = handle->connect(path);

        if (req) {
            AfterConnect::Ptr afterConnect(new AfterConnect(self));
            req->onComplete = afterConnect;
        } else {
            self->destroy(node::uv::Error::last());
        }
    }

    static void connect(
        Socket* self,
        uv::Tcp* handle,
        String::CPtr address,
        Int port,
        Int addressType,
        String::CPtr localAddress = String::null()) {
        assert(self->hasFlag(CONNECTING));

        if (localAddress) {
            Int r;
            if (addressType == 6) {
                r = handle->bind6(localAddress);
            } else {
                r = handle->bind(localAddress);
            }

            if (r) {
                self->destroy(node::uv::Error::last());
                return;
            }
        }

        uv::Connect* req;
        if (addressType == 6) {
            req = handle->connect6(address, port);
        } else {  // addressType == 4
            req = handle->connect(address, port);
        }

        if (req) {
            AfterConnect::Ptr afterConnect(new AfterConnect(self));
            req->onComplete = afterConnect;
        } else {
            self->destroy(node::uv::Error::last());
        }
    }

    void connect(
        Int port,
        String::CPtr host,
        String::CPtr localAddress,
        String::CPtr path,
        JsFunction::Ptr cb) {
        LIBJ_STATIC_SYMBOL_DEF(symLocalhost4, "127.0.0.1");

        Boolean pipe = !!path;

        if (hasFlag(DESTROYED) || !handle_) {
            handle_ = pipe
                ? reinterpret_cast<uv::Stream*>(new uv::Pipe())
                : reinterpret_cast<uv::Stream*>(new uv::Tcp());
            initSocketHandle(this);
        }

        if (cb) on(EVENT_CONNECT, cb);

        active();
        setFlag(CONNECTING);
        setFlag(WRITABLE);

        if (pipe) {
            uv::Pipe* pipe = reinterpret_cast<uv::Pipe*>(handle_);
            connect(this, pipe, path);
        } else {
            uv::Tcp* tcp = reinterpret_cast<uv::Tcp*>(handle_);
            if (!host) {
                connect(this, tcp, symLocalhost4, port, 4);
            } else {
                AfterLookup::Ptr afterLookup(
                    new AfterLookup(
                        LIBJ_THIS_PTR(Socket), tcp, port, localAddress));
                dns::lookup(host, afterLookup);
            }
        }
    }

    void connectQueueCleanUp() {
        unsetFlag(CONNECTING);
        connectQueueSize_ = 0;
        connectBufQueue_ = JsArray::null();
        connectCbQueue_ = JsArray::null();
    }

    void fireErrorCallbacks(Error::CPtr err, JsFunction::Ptr cb) {
        if (cb) cb->call(err);
        if (err && !hasFlag(ERROR_EMITTED)) {
            EmitError::Ptr emitErr(new EmitError(this, err));
            process::nextTick(emitErr);
            setFlag(ERROR_EMITTED);
        }
    }

    Boolean destroy(Error::CPtr err, JsFunction::Ptr cb) {
        if (hasFlag(DESTROYED)) {
#ifdef LIBNODE_REMOVE_LISTENER
            // OnDestroy has already called removeAllListeners
            // check the return value 'false' instead of calling:
#else
            fireErrorCallbacks(err, cb);
#endif
            return false;
        }

        connectQueueCleanUp();
        unsetFlag(READABLE);
        unsetFlag(WRITABLE);
        finishTimer();

        if (handle_) {
            handle_->close();
            handle_->setOnRead(JsFunction::null());
            handle_ = NULL;
        }

        fireErrorCallbacks(err, cb);

        EmitClose::Ptr emitClose(new EmitClose(this, err));
        process::nextTick(emitClose);

        setFlag(DESTROYED);

        // this.server has already registered a 'close' listener
        // which will do the following:
        // if (this.server) {
        //     this.server._connections--;
        //     if (this.server._emitCloseIfDrained) {
        //         this.server._emitCloseIfDrained();
        //     }
        // }

        return true;
    }

    Boolean writeBuffer(Buffer::CPtr buf, JsFunction::Ptr cb) {
        active();

        if (!handle_) {
            destroy(Error::create(Error::ILLEGAL_STATE), cb);
            return false;
        }

        uv::Write* req = handle_->writeBuffer(buf);

        if (!req) {
            destroy(node::uv::Error::last(), cb);
            return false;
        }

        AfterWrite::Ptr afterWrite(new AfterWrite(this, req));
        req->onComplete = afterWrite;
        req->cb = cb;

        pendingWriteReqs_++;
        bytesDispatched_ += req->bytes;
        return true;
    }

    Boolean hasTimer() {
        return !timer_.isUndefined();
    }

    void startTimer(Int timeout) {
        if (hasTimer()) finishTimer();

        JsFunction::Ptr emitTimeout(new EmitTimeout(this));
        timer_ = node::setTimeout(emitTimeout, timeout);
        timeout_ = timeout;
    }

    void finishTimer() {
        clearTimeout(timer_);
        timer_ = UNDEFINED;
    }

 private:
    class OnRead : LIBJ_JS_FUNCTION(OnRead)
     public:
        OnRead(
            Socket* sock,
            uv::Stream* handle)
            : self_(sock)
            , handle_(handle) {}

        virtual Value operator()(JsArray::Ptr args) {
            assert(self_->handle_ == handle_);

            self_->active();

            Buffer::CPtr buffer = args->getCPtr<Buffer>(0);
            if (buffer) {
                if (self_->decoder_) {
                    String::CPtr str = self_->decoder_->write(buffer);
                    if (str && str->length()) {
                        self_->emit(EVENT_DATA, str);
                    }
                } else {
                    self_->emit(EVENT_DATA, buffer);
                }

                self_->bytesRead_ += buffer->length();
                if (self_->onData_) (*self_->onData_)(args);
            } else {
                node::uv::Error::CPtr err = node::uv::Error::last();
                if (err->code() == node::uv::Error::_EOF) {
                    self_->unsetFlag(READABLE);
                    assert(!self_->hasFlag(GOT_EOF));
                    self_->setFlag(GOT_EOF);

                    if (!self_->hasFlag(WRITABLE)) self_->destroy();
                    if (!self_->hasFlag(ALLOW_HALF_OPEN)) self_->end();

                    // if (self_->decoder_) {
                    //     String::CPtr ret = self_->decoder_->end();
                    //     if (ret) self_->emit(EVENT_DATA, ret);
                    // }

                    self_->emit(EVENT_END);
                    if (self_->onEnd_) (*self_->onEnd_)();
                } else if (err->code() == node::uv::Error::_ECONNRESET) {
                    self_->destroy();
                } else {
                    self_->destroy(err);
                }
            }
            return Status::OK;
        }

     private:
        Socket* self_;
        uv::Stream* handle_;
    };

    class AfterShutdown : LIBJ_JS_FUNCTION(AfterShutdown)
     public:
        AfterShutdown(Socket* self) : self_(self) {}

        virtual Value operator()(JsArray::Ptr args) {
            assert(self_->hasFlag(SHUTDOWN));
            assert(!self_->hasFlag(WRITABLE));

            if (self_->hasFlag(DESTROYED)) {
                return Status::OK;
            } else if (self_->hasFlag(GOT_EOF) || !self_->hasFlag(READABLE)) {
                self_->destroy();
            }
            return Status::OK;
        }

     private:
        Socket* self_;
    };

    class AfterWrite : LIBJ_JS_FUNCTION(AfterWrite)
     public:
        AfterWrite(
            Socket* sock,
            uv::Write* req)
            : self_(sock)
            , req_(req) {}

        virtual Value operator()(JsArray::Ptr args) {
            if (self_->hasFlag(DESTROYED)) {
                return Error::ILLEGAL_STATE;
            }

            assert(args->get(0).is<int>());
            int status = to<int>(args->get(0));
            if (status) {
                node::uv::Error::CPtr err = node::uv::Error::last();
                self_->destroy(err, req_->cb);
                return err->code();
            }

            self_->active();
            self_->pendingWriteReqs_--;
            if (self_->pendingWriteReqs_ == 0) {
                self_->emit(EVENT_DRAIN);
            }

            if (req_->cb) (*req_->cb)();

            if (self_->pendingWriteReqs_ == 0 &&
                self_->hasFlag(DESTROY_SOON)) {
                self_->destroy();
            }
            return Status::OK;
        }

     private:
        Socket* self_;
        uv::Write* req_;
    };

    class AfterConnect : LIBJ_JS_FUNCTION(AfterConnect)
     public:
        AfterConnect(Socket* sock) : self_(sock) {}

        virtual Value operator()(JsArray::Ptr args) {
            if (self_->hasFlag(DESTROYED)) return Status::OK;

            assert(self_->hasFlag(CONNECTING));
            self_->unsetFlag(CONNECTING);

            assert(args->get(0).is<int>());
            assert(args->get(3).is<Boolean>());
            assert(args->get(4).is<Boolean>());
            int status = to<int>(args->get(0));
            Boolean readable = to<Boolean>(args->get(3));
            Boolean writable = to<Boolean>(args->get(4));

            if (!status) {
                if (readable) {
                    self_->setFlag(READABLE);
                } else {
                    self_->unsetFlag(READABLE);
                }
                if (writable) {
                    self_->setFlag(WRITABLE);
                } else {
                    self_->unsetFlag(WRITABLE);
                }
                self_->active();

                if (readable && !self_->hasFlag(PAUSED)) {
                    self_->handle_->readStart();
                }

                if (self_->connectQueueSize_) {
                    JsArray::Ptr connectBufQueue = self_->connectBufQueue_;
                    JsArray::Ptr connectCbQueue = self_->connectCbQueue_;
                    Size len = connectBufQueue->length();
                    assert(connectCbQueue->length() == len);
                    for (Size i = 0; i < len; i++) {
                        JsFunction::Ptr cb =
                            connectCbQueue->getPtr<JsFunction>(i);
                        self_->write(connectBufQueue->get(i), cb);
                    }
                    self_->connectQueueCleanUp();
                }

                self_->emit(EVENT_CONNECT);

                if (self_->hasFlag(SHUTDOWN_QUEUED)) {
                    self_->unsetFlag(SHUTDOWN_QUEUED);
                    self_->end();
                }
            } else {
                self_->connectQueueCleanUp();
                self_->destroy(node::uv::Error::last());
            }
            return Status::OK;
        }

     private:
        Socket* self_;
    };

    class AfterLookup : LIBJ_JS_FUNCTION(AfterLookup)
     public:
        AfterLookup(
            Socket::Ptr self,
            uv::Tcp* tcp,
            Int port,
            String::CPtr localAddress)
            : self_(self)
            , tcp_(tcp)
            , port_(port)
            , localAddress_(localAddress) {}

        virtual Value operator()(JsArray::Ptr args) {
            LIBJ_STATIC_SYMBOL_DEF(symLocalhost4, "127.0.0.1");
            LIBJ_STATIC_SYMBOL_DEF(symLocalhost6, "0:0:0:0:0:0:0:1");

            if (!self_->hasFlag(CONNECTING)) return Status::OK;

            Error::CPtr err = args->getCPtr<Error>(0);
            String::CPtr ip = args->getCPtr<String>(1);
            Int addressType = to<Int>(args->get(2));

            if (err) {
                JsFunction::Ptr destroy(
                    new EmitErrorAndDestroy(&(*self_), err));
                process::nextTick(destroy);
            } else {
                self_->active();
                if (!addressType) {
                    addressType = 4;
                }
                if (!ip) {
                    ip = addressType == 4 ? symLocalhost4 : symLocalhost6;
                }
                connect(
                    &(*self_), tcp_, ip, port_, addressType, localAddress_);
            }
            return Status::OK;
        }

     private:
        Socket::Ptr self_;
        uv::Tcp* tcp_;
        Int port_;
        String::CPtr localAddress_;
    };

    class EmitError : LIBJ_JS_FUNCTION(EmitError)
     public:
        EmitError(
            Socket* sock,
            Error::CPtr err)
            : self_(sock)
            , error_(err) {}

        virtual Value operator()(JsArray::Ptr args) {
            self_->emit(EVENT_ERROR, error_);
            return Status::OK;
        }

     private:
        Socket* self_;
        Error::CPtr error_;
    };

    class EmitErrorAndDestroy : LIBJ_JS_FUNCTION(EmitErrorAndDestroy)
     public:
        EmitErrorAndDestroy(
            Socket* sock,
            Error::CPtr err)
            : self_(sock)
            , error_(err) {}

        virtual Value operator()(JsArray::Ptr args) {
            self_->emit(EVENT_ERROR, error_);
            self_->destroy();
            return Status::OK;
        }

     private:
        Socket* self_;
        Error::CPtr error_;
    };

    class EmitClose : LIBJ_JS_FUNCTION(EmitClose)
     public:
        EmitClose(
            Socket* sock,
            Error::CPtr err)
            : self_(sock)
            , error_(err) {}

        virtual Value operator()(JsArray::Ptr args) {
            LIBJ_STATIC_SYMBOL_DEF(EVENT_DESTROY, "destroy");

            self_->emit(EVENT_CLOSE, !!error_);
            self_->emit(EVENT_DESTROY);
            return Status::OK;
        }

     private:
        Socket* self_;
        Error::CPtr error_;
    };

    class EmitTimeout : LIBJ_JS_FUNCTION(EmitTimeout)
     public:
        EmitTimeout(Socket* self) : self_(self) {}

        virtual Value operator()(JsArray::Ptr args) {
            self_->emit(EVENT_TIMEOUT);
            return Status::OK;
        }

     private:
        Socket* self_;
    };

    class OnDestroy : LIBJ_JS_FUNCTION(OnDestroy)
     public:
        OnDestroy(Socket::Ptr sock) : self_(sock) {}

        virtual Value operator()(JsArray::Ptr args) {
            assert(!self_->handle_);
            assert(self_->hasFlag(DESTROYED));
#ifdef LIBNODE_REMOVE_LISTENER
            self_->removeAllListeners();
#endif
            return Status::OK;
        }

     private:
        Socket::Ptr self_;
    };

 public:
    enum Flag {
        READABLE        = 1 << 0,
        WRITABLE        = 1 << 1,
        CONNECTING      = 1 << 2,
        PAUSED          = 1 << 3,
        GOT_EOF         = 1 << 4,
        DESTROYED       = 1 << 5,
        DESTROY_SOON    = 1 << 6,
        SHUTDOWN        = 1 << 7,
        SHUTDOWN_QUEUED = 1 << 8,
        ERROR_EMITTED   = 1 << 9,
        ALLOW_HALF_OPEN = 1 << 10,
    };

 private:
    uv::Stream* handle_;
    Value timer_;
    Int timeout_;
    Size pendingWriteReqs_;
    Size connectQueueSize_;
    JsArray::Ptr connectBufQueue_;
    JsArray::Ptr connectCbQueue_;
    Size bytesRead_;
    Size bytesDispatched_;
    StringDecoder::Ptr decoder_;
    JsFunction::Ptr onData_;
    JsFunction::Ptr onEnd_;
    JsFunction::Ptr onDrain_;
    JsFunction::Ptr onClose_;
    http::Parser* parser_;
    http::OutgoingMessage* httpMessage_;

    Socket()
        : handle_(NULL)
        , timer_(UNDEFINED)
        , timeout_(0)
        , pendingWriteReqs_(0)
        , connectQueueSize_(0)
        , connectBufQueue_(JsArray::null())
        , connectCbQueue_(JsArray::null())
        , bytesRead_(0)
        , bytesDispatched_(0)
        , decoder_(StringDecoder::null())
        , onData_(JsFunction::null())
        , onEnd_(JsFunction::null())
        , onDrain_(JsFunction::null())
        , onClose_(JsFunction::null())
        , parser_(NULL)
        , httpMessage_(NULL) {}

 public:
    virtual ~Socket() {
        if (handle_) handle_->close();
    }
};

}  // namespace net
}  // namespace detail
}  // namespace node
}  // namespace libj

#endif  // LIBNODE_DETAIL_NET_SOCKET_H_
