// Copyright (c) 2012-2014 Plenluno All rights reserved.

#ifndef LIBNODE_DETAIL_HTTP_PARSER_H_
#define LIBNODE_DETAIL_HTTP_PARSER_H_

#include <libnode/buffer.h>
#include <libnode/http/method.h>
#include <libnode/detail/flags.h>
#include <libnode/detail/net/socket.h>
#include <libnode/detail/http/header_scanner.h>
#include <libnode/detail/http/incoming_message_list.h>

#include <assert.h>
#include <http_parser.h>

namespace libj {
namespace node {
namespace detail {
namespace http {

class Parser : public Flags {
 private:
    typedef TypedJsArray<String::CPtr> StringArray;

 public:
    Parser(
        enum http_parser_type type,
        net::Socket::Ptr sock,
        Size maxHeadersCount = 0)
        : url_(String::null())
        , method_(String::null())
        , fields_(StringArray::create())
        , values_(StringArray::create())
        , maxHeadersCount_(maxHeadersCount)
        , socket_(sock)
        , incoming_(IncomingMessage::null())
        , onIncoming_(JsFunction::null()) {
        static http_parser_settings settings;
        static Boolean initSettings = false;
        if (!initSettings) {
            settings.on_message_begin = Parser::onMessageBegin;
            settings.on_url = Parser::onUrl;
            settings.on_header_field = Parser::onHeaderField;
            settings.on_header_value = Parser::onHeaderValue;
            settings.on_headers_complete = Parser::onHeadersComplete;
            settings.on_body = Parser::onBody;
            settings.on_message_complete = Parser::onMessageComplete;
            initSettings = true;
        }

        http_parser_init(&parser_, type);
        parser_.data = this;
        settings_ = &settings;
    }

    Int execute(Buffer::CPtr buf) {
        size_t len = buf->length();
        size_t numParsed = http_parser_execute(
                                &parser_,
                                settings_,
                                static_cast<const char*>(buf->data()),
                                len);
        if (!parser_.upgrade && numParsed != len) {
            return -1;
        } else {
            return numParsed;
        }
    }

    Boolean finish() {
        size_t numParsed = http_parser_execute(
                                &parser_,
                                settings_,
                                NULL,
                                0);
        return !numParsed;
    }

    JsArray::Ptr free() {
        fields_->clear();
        values_->clear();

        JsArray::Ptr keeper = JsArray::create();
        if (socket_) {
            keeper->add(socket_->setOnData(JsFunction::null()));
            keeper->add(socket_->setOnEnd(JsFunction::null()));
            socket_->setParser(NULL);
        }
        socket_ = net::Socket::null();

        incoming_ = IncomingMessage::null();
        onIncoming_ = JsFunction::null();
        return keeper;
    }

    IncomingMessage::Ptr incoming() {
        return incoming_;
    }

    void setOnIncoming(JsFunction::Ptr onIncoming) {
        onIncoming_ = onIncoming;
    }

 private:
    static String::CPtr concat(String::CPtr s, const char* at, size_t len) {
        if (s) {
            return s->concat(String::create(at, String::UTF8, len));
        } else {
            return String::create(at, String::UTF8, len);
        }
    }

    static int onMessageBegin(http_parser* parser) {
        Parser* self = static_cast<Parser*>(parser->data);
        self->url_ = String::null();
        self->fields_->clear();
        self->values_->clear();
        return 0;
    }

    static int onUrl(http_parser* parser, const char* at, size_t len) {
        Parser* self = static_cast<Parser*>(parser->data);
        self->url_ = concat(self->url_, at, len);
        return 0;
    }

    static int onHeaderField(
        http_parser* parser, const char* at, size_t len) {
        Parser* self = static_cast<Parser*>(parser->data);
        assert(self->fields_->size() == self->values_->size());
        self->fields_->addTyped(scanHeaderField<char>(at, len));
        return 0;
    }

    static int onHeaderValue(
        http_parser* parser, const char* at, size_t len) {
        Parser* self = static_cast<Parser*>(parser->data);
        StringArray::Ptr fields = self->fields_;
        StringArray::Ptr values = self->values_;
        Size numFields = fields->size();
        Size numValues = values->size();
        if (numValues != numFields) {
            values->addTyped(String::null());
        }

        assert(values->size() == numFields);
        String::CPtr value = values->getTyped(numFields - 1);
        value = concat(value, at, len);
        values->setTyped(numFields - 1, value);
        return 0;
    }

    static int onHeadersComplete(http_parser* parser) {
        Parser* self = static_cast<Parser*>(parser->data);
        if (parser->type == HTTP_REQUEST) {
            String::CPtr method;
            switch (parser->method) {
            case HTTP_DELETE:
                method = node::http::METHOD_DELETE;
                break;
            case HTTP_GET:
                method = node::http::METHOD_GET;
                break;
            case HTTP_HEAD:
                method = node::http::METHOD_HEAD;
                break;
            case HTTP_POST:
                method = node::http::METHOD_POST;
                break;
            case HTTP_PUT:
                method = node::http::METHOD_PUT;
                break;
            case HTTP_CONNECT:
                method = node::http::METHOD_CONNECT;
                break;
            case HTTP_OPTIONS:
                method = node::http::METHOD_OPTIONS;
                break;
            case HTTP_TRACE:
                method = node::http::METHOD_TRACE;
                break;
            case HTTP_COPY:
                method = node::http::METHOD_COPY;
                break;
            case HTTP_LOCK:
                method = node::http::METHOD_LOCK;
                break;
            case HTTP_MKCOL:
                method = node::http::METHOD_MKCOL;
                break;
            case HTTP_MOVE:
                method = node::http::METHOD_MOVE;
                break;
            case HTTP_PROPFIND:
                method = node::http::METHOD_PROPFIND;
                break;
            case HTTP_PROPPATCH:
                method = node::http::METHOD_PROPPATCH;
                break;
            case HTTP_SEARCH:
                method = node::http::METHOD_SEARCH;
                break;
            case HTTP_UNLOCK:
                method = node::http::METHOD_UNLOCK;
                break;
            default:
                method = String::create();
            }
            self->method_ = method;
        } else if (parser->type == HTTP_RESPONSE) {
            self->statusCode_ = static_cast<Int>(parser->status_code);
        }

        self->majorVer_ = static_cast<Int>(parser->http_major);
        self->minorVer_ = static_cast<Int>(parser->http_minor);

        if (parser->upgrade) {
            self->setFlag(UPGRADE);
        } else {
            self->unsetFlag(UPGRADE);
        }

        if (http_should_keep_alive(parser)) {
            self->setFlag(SHOULD_KEEP_ALIVE);
        } else {
            self->unsetFlag(SHOULD_KEEP_ALIVE);
        }

        return self->onHeadersComplete() ? 1 : 0;
    }

    static int onBody(http_parser* parser, const char* at, size_t length) {
        Parser* self = static_cast<Parser*>(parser->data);
        self->onBody(Buffer::create(at, length));
        return 0;
    }

    static int onMessageComplete(http_parser* parser) {
        Parser* self = static_cast<Parser*>(parser->data);
        self->onMessageComplete();
        return 0;
    }

 private:
    int onHeadersComplete() {
        incoming_ = incomingMessageList()->alloc();
        if (incoming_) {
            incoming_->setNewSocket(socket_);
        } else {
            incoming_ = IncomingMessage::create(socket_);
        }
        incoming_->setUrl(url_);
        incoming_->setHttpVersionMajor(majorVer_);
        incoming_->setHttpVersionMinor(minorVer_);

        if (majorVer_ == 1 && minorVer_ == 1) {
            LIBJ_STATIC_SYMBOL_DEF(symV1_1, "1.1");
            incoming_->setHttpVersion(symV1_1);
        } else {
            StringBuilder::Ptr httpVer = StringBuilder::create();
            httpVer->append(majorVer_);
            httpVer->appendChar('.');
            httpVer->append(minorVer_);
            incoming_->setHttpVersion(httpVer->toString());
        }

        Size n = values_->length();
        assert(fields_->length() == n || fields_->length() == n + 1);
        if (maxHeadersCount_) {
            n = n < maxHeadersCount_ ? n : maxHeadersCount_;
        }
        for (Size i = 0; i < n; i++) {
            incoming_->addHeaderLine(
                fields_->getTyped(i),
                values_->getTyped(i));
        }

        url_ = String::null();
        fields_->clear();
        values_->clear();

        if (method_) {
            incoming_->setMethod(method_);
        } else {
            incoming_->setStatusCode(statusCode_);
        }

        if (hasFlag(UPGRADE))
            incoming_->setFlag(IncomingMessage::UPGRADE);

        Boolean skipBody = false;
        if (!hasFlag(UPGRADE)) {
            Value r = invoke(
                onIncoming_,
                incoming_,
                hasFlag(SHOULD_KEEP_ALIVE));
            skipBody = to<Boolean>(r);
        }
        return skipBody;
    }

    void onBody(Buffer::CPtr buf) {
        LinkedList::Ptr pendings = incoming_->pendings();
        if (incoming_->hasFlag(IncomingMessage::PAUSED) ||
            pendings->length()) {
            pendings->push(buf);
        } else {
            incoming_->emitData(buf);
        }
    }

    void onMessageComplete() {
        incoming_->setFlag(IncomingMessage::COMPLETE);

        if (!fields_->isEmpty()) {
            Size n = fields_->size();
            assert(values_->size() == n);
            for (Size i = 0; i < n; i++) {
                incoming_->addHeaderLine(
                    fields_->getTyped(i),
                    values_->getTyped(i));
            }
            url_ = String::null();
            fields_->clear();
            values_->clear();
        }

        if (!incoming_->hasFlag(IncomingMessage::UPGRADE)) {
            LinkedList::Ptr pendings = incoming_->pendings();
            if (incoming_->hasFlag(IncomingMessage::PAUSED) ||
                pendings->length()) {
                pendings->push(0);  // EOF
            } else {
                incoming_->unsetFlag(IncomingMessage::READABLE);
                incoming_->emitEnd();
            }
        }

        if (socket_->readable()) {
            socket_->resume();
        }
    }

 private:
    enum Flag {
        HAVE_FLUSHED      = 1 << 0,
        UPGRADE           = 1 << 1,
        SHOULD_KEEP_ALIVE = 1 << 2,
    };

 private:
    http_parser parser_;
    http_parser_settings* settings_;
    String::CPtr url_;
    String::CPtr method_;
    Int majorVer_;
    Int minorVer_;
    Int statusCode_;
    StringArray::Ptr fields_;
    StringArray::Ptr values_;
    Size maxHeadersCount_;
    net::Socket::Ptr socket_;
    IncomingMessage::Ptr incoming_;
    JsFunction::Ptr onIncoming_;
};

}  // namespace http
}  // namespace detail
}  // namespace node
}  // namespace libj

#endif  // LIBNODE_DETAIL_HTTP_PARSER_H_
