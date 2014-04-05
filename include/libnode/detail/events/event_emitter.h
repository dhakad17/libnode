// Copyright (c) 2012-2014 Plenluno All rights reserved.

#ifndef LIBNODE_DETAIL_EVENTS_EVENT_EMITTER_H_
#define LIBNODE_DETAIL_EVENTS_EVENT_EMITTER_H_

#include <libnode/debug_print.h>
#include <libnode/detail/flags.h>
#include <libnode/detail/arguments_list.h>

#include <libj/js_object.h>
#include <libj/js_function.h>
#include <libj/typed_js_array.h>
#include <libj/detail/js_object.h>

namespace libj {
namespace node {
namespace detail {
namespace events {

template<typename I>
class EventEmitter
    : public libj::detail::JsObject<I>
    , public node::detail::Flags {
#ifdef LIBNODE_USE_SP
 private:
    typedef TypedJsArray<String::CPtr> StringArray;
    typedef TypedJsArray<JsFunction::Ptr> JsFunctionArray;
    typedef TypedJsArray<JsFunctionArray::Ptr> JsFunctionArrays;

 public:
    EventEmitter()
        : maxListeners_(10)
        , events_(StringArray::create())
        , listenerArrays_(JsFunctionArrays::create()) {}

    virtual void addListener(
        String::CPtr event, JsFunction::Ptr listener) {
        if (!listener) return;

        JsFunctionArray::Ptr ls = getListeners(event);
        if (!ls) {
            ls = newListeners(event);
        }

        ls->addTyped(listener);
        if (maxListeners_ && ls->length() > maxListeners_) {
            LIBNODE_DEBUG_PRINT("%d listeners added", ls->length());
        }
        emit(I::EVENT_NEW_LISTENER);
    }

    virtual void removeListener(
        String::CPtr event, JsFunction::CPtr listener) {
        JsFunctionArray::Ptr ls = getListeners(event);
        if (!ls) return;

        Size len = ls->length();
        for (Size i = 0; i < len; i++) {
            JsFunction::Ptr f = ls->getTyped(i);
            if (f->type() == Type<Once>::id()) {
                typename Once::Ptr once = LIBJ_STATIC_PTR_CAST(Once)(f);
                if (once->listener()->equals(listener)) {
                    ls->removeTyped(i);
                    return;
                }
            } else {
                if (f->equals(listener)) {
                    ls->removeTyped(i);
                    return;
                }
            }
        }
    }

    virtual void removeAllListeners() {
        events_->clear();
        listenerArrays_->clear();
    }

    virtual void removeAllListeners(String::CPtr event) {
        Size len = events_->length();
        for (Size i = 0; i < len; i++) {
            if (events_->getTyped(i)->equals(event)) {
                events_->removeTyped(i);
                listenerArrays_->removeTyped(i);
                return;
            }
        }
    }

    virtual JsArray::Ptr listeners(String::CPtr event) {
        JsFunctionArray::Ptr ls = getListeners(event);
        if (!ls) {
            ls = newListeners(event);
        }
        return ls;
    }

    virtual Boolean emit(
        String::CPtr event, JsArray::Ptr args = JsArray::null()) {
        JsFunctionArray::Ptr ls = getListeners(event);
        if (!ls || ls->isEmpty()) return false;

        Size i = 0;
        Size len = ls->length();
        while (i < len) {
            JsFunction::Ptr f = ls->getTyped(i);
            if (f->type() == Type<Once>::id()) {
                len--;
            } else {
                i++;
            }
            (*f)(args);
        }
        return true;
    }
#else
 public:
    EventEmitter()
        : maxListeners_(10)
        , events_(libj::JsObject::create()) {}

    virtual void addListener(
        String::CPtr event, JsFunction::Ptr listener) {
        if (!listener) return;

        JsArray::Ptr ls = listeners(event);
        ls->add(listener);
        if (maxListeners_ && ls->length() > maxListeners_) {
            LIBNODE_DEBUG_PRINT("%d listeners added", ls->length());
        }
        emit(I::EVENT_NEW_LISTENER);
    }

    virtual void removeListener(
        String::CPtr event, JsFunction::CPtr listener) {
        JsArray::Ptr a = listeners(event);
        Size n = a->size();
        for (Size i = 0; i < n; i++) {
            Value v = a->get(i);
            if (v.is<Once>()) {
                typename Once::Ptr once = toPtr<Once>(v);
                if (once->listener()->equals(listener)) {
                    a->remove(i);
                    break;
                }
            } else {
                JsFunction::Ptr func = toPtr<JsFunction>(v);
                if (func->equals(listener)) {
                    a->remove(i);
                    break;
                }
            }
        }
    }

    virtual void removeAllListeners() {
        events_->clear();
    }

    virtual void removeAllListeners(String::CPtr event) {
        events_->remove(event);
    }

    virtual JsArray::Ptr listeners(String::CPtr event) {
        JsArray::Ptr a = events_->getPtr<JsArray>(event);
        if (a) {
            return a;
        } else {
            a = JsArray::create();
            events_->put(String::intern(event), a);
            return a;
        }
    }

    virtual Boolean emit(
        String::CPtr event, JsArray::Ptr args = JsArray::null()) {
        JsArray::Ptr a = listeners(event);
        if (a->isEmpty()) return false;

        Size i = 0;
        while (i < a->size()) {
            Value v = a->get(i);
            if (v.is<Once>()) i++;
            JsFunction::Ptr f = toPtr<JsFunction>(v);
            (*f)(args);
        }
        return true;
    }
#endif

    virtual void on(String::CPtr event, JsFunction::Ptr listener) {
        addListener(event, listener);
    }

    virtual void once(String::CPtr event, JsFunction::Ptr listener) {
        if (!listener) return;

        JsFunction::Ptr cb(new Once(event, listener, this));
        addListener(event, cb);
    }

    virtual void setMaxListeners(Size max) {
        maxListeners_ = max;
    }

 public:
    virtual Boolean emit(
        String::CPtr event,
        const Value& v1) {
        LIBNODE_ARGUMENTS_ALLOC(args);
        args->add(v1);

        Boolean r = emit(event, args);
        LIBNODE_ARGUMENTS_FREE(args);
        return r;
    }

    virtual Boolean emit(
        String::CPtr event,
        const Value& v1, const Value& v2) {
        LIBNODE_ARGUMENTS_ALLOC(args);
        args->add(v1);
        args->add(v2);

        Boolean r = emit(event, args);
        LIBNODE_ARGUMENTS_FREE(args);
        return r;
    }

    virtual Boolean emit(
        String::CPtr event,
        const Value& v1, const Value& v2, const Value& v3) {
        LIBNODE_ARGUMENTS_ALLOC(args);
        args->add(v1);
        args->add(v2);
        args->add(v3);

        Boolean r = emit(event, args);
        LIBNODE_ARGUMENTS_FREE(args);
        return r;
    }

    virtual Boolean emit(
        String::CPtr event,
        const Value& v1, const Value& v2, const Value& v3,
        const Value& v4) {
        LIBNODE_ARGUMENTS_ALLOC(args);
        args->add(v1);
        args->add(v2);
        args->add(v3);
        args->add(v4);

        Boolean r = emit(event, args);
        LIBNODE_ARGUMENTS_FREE(args);
        return r;
    }

    virtual Boolean emit(
        String::CPtr event,
        const Value& v1, const Value& v2, const Value& v3,
        const Value& v4, const Value& v5) {
        LIBNODE_ARGUMENTS_ALLOC(args);
        args->add(v1);
        args->add(v2);
        args->add(v3);
        args->add(v4);
        args->add(v5);

        Boolean r = emit(event, args);
        LIBNODE_ARGUMENTS_FREE(args);
        return r;
    }

    virtual Boolean emit(
        String::CPtr event,
        const Value& v1, const Value& v2, const Value& v3,
        const Value& v4, const Value& v5, const Value& v6) {
        LIBNODE_ARGUMENTS_ALLOC(args);
        args->add(v1);
        args->add(v2);
        args->add(v3);
        args->add(v4);
        args->add(v5);
        args->add(v6);

        Boolean r = emit(event, args);
        LIBNODE_ARGUMENTS_FREE(args);
        return r;
    }

    virtual Boolean emit(
        String::CPtr event,
        const Value& v1, const Value& v2, const Value& v3,
        const Value& v4, const Value& v5, const Value& v6,
        const Value& v7) {
        LIBNODE_ARGUMENTS_ALLOC(args);
        args->add(v1);
        args->add(v2);
        args->add(v3);
        args->add(v4);
        args->add(v5);
        args->add(v6);
        args->add(v7);

        Boolean r = emit(event, args);
        LIBNODE_ARGUMENTS_FREE(args);
        return r;
    }

    virtual Boolean emit(
        String::CPtr event,
        const Value& v1, const Value& v2, const Value& v3,
        const Value& v4, const Value& v5, const Value& v6,
        const Value& v7, const Value& v8) {
        LIBNODE_ARGUMENTS_ALLOC(args);
        args->add(v1);
        args->add(v2);
        args->add(v3);
        args->add(v4);
        args->add(v5);
        args->add(v6);
        args->add(v7);
        args->add(v8);

        Boolean r = emit(event, args);
        LIBNODE_ARGUMENTS_FREE(args);
        return r;
    }

    virtual Boolean emit(
        String::CPtr event,
        const Value& v1, const Value& v2, const Value& v3,
        const Value& v4, const Value& v5, const Value& v6,
        const Value& v7, const Value& v8, const Value& v9) {
        LIBNODE_ARGUMENTS_ALLOC(args);
        args->add(v1);
        args->add(v2);
        args->add(v3);
        args->add(v4);
        args->add(v5);
        args->add(v6);
        args->add(v7);
        args->add(v8);
        args->add(v9);

        Boolean r = emit(event, args);
        LIBNODE_ARGUMENTS_FREE(args);
        return r;
    }

 private:
    class Once : LIBJ_JS_FUNCTION_TEMPLATE(Once)
     public:
        Once(
            String::CPtr event,
            JsFunction::Ptr listener,
            EventEmitter* ee)
            : event_(event)
            , listener_(listener)
            , ee_(ee) {}

        Value operator()(JsArray::Ptr args) {
            (*listener_)(args);
            ee_->removeListener(event_, listener_);
            return UNDEFINED;
        }

        JsFunction::Ptr listener() {
            return listener_;
        }

     private:
        String::CPtr event_;
        JsFunction::Ptr listener_;
        EventEmitter* ee_;
    };

#ifdef LIBNODE_USE_SP
 private:
    JsFunctionArray::Ptr getListeners(String::CPtr event) {
        Size len = events_->length();
        for (Size i = 0; i < len; i++) {
            if (events_->getTyped(i)->equals(event)) {
                return listenerArrays_->getTyped(i);
            }
        }
        return JsFunctionArray::null();
    }

    JsFunctionArray::Ptr newListeners(String::CPtr event) {
        events_->addTyped(String::intern(event));
        JsFunctionArray::Ptr ls = JsFunctionArray::create();
        listenerArrays_->addTyped(ls);
        return ls;
    }

 private:
    Size maxListeners_;
    StringArray::Ptr events_;
    JsFunctionArrays::Ptr listenerArrays_;
#else
 private:
    Size maxListeners_;
    libj::JsObject::Ptr events_;
#endif
};

}  // namespace events
}  // namespace detail
}  // namespace node
}  // namespace libj

#endif  // LIBNODE_DETAIL_EVENTS_EVENT_EMITTER_H_
