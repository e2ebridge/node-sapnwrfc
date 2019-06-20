/*
-----------------------------------------------------------------------------
Copyright (c) 2011 Joachim Dorner
Copyright (c) 2014-2019 Scheer E2E AG

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
-----------------------------------------------------------------------------
*/

#ifndef COMMON_H_
#define COMMON_H_

#include <sapnwrfc.h>
#include <iostream>
#include <napi.h>
#include "Loggable.h"

#ifdef _MSC_VER
#if _MSC_VER < 1900
#define snprintf _snprintf_s
#endif
#endif

typedef DATA_CONTAINER_HANDLE CHND;

static std::string convertToString(Napi::Env env, const SAP_UC *str) {
  Napi::HandleScope scope{env};
  auto utf16String = Napi::String::New(env, (const char16_t *) (str));
  return utf16String.Utf8Value();
}

static SAP_UC *convertToSAPUC(Napi::String const &str) {
  auto u16Value = str.Utf16Value();
  auto size = u16Value.length();
  auto ret = new char16_t[size + 1];
  u16Value.copy(ret, size);
  ret[size] = 0;
  return reinterpret_cast<SAP_UC *>(ret);
}

static void FillRfcInfo(Napi::Env env, const RFC_ERROR_INFO &info, Napi::Object out) {
  using namespace Napi;
  HandleScope scope{env};

  out.Set("code", Number::New(env, info.code));
  out.Set("group", Number::New(env, info.group));
  out.Set("key", String::New(env, (const char16_t *) (info.key)));
  out.Set("class", String::New(env, (const char16_t *) (info.abapMsgClass)));
  out.Set("type", String::New(env, (const char16_t *) (info.abapMsgType)));
  out.Set("number", String::New(env, (const char16_t *) (info.abapMsgNumber)));
  out.Set("msgv1", String::New(env, (const char16_t *) (info.abapMsgV1)));
  out.Set("msgv2", String::New(env, (const char16_t *) (info.abapMsgV2)));
  out.Set("msgv3", String::New(env, (const char16_t *) (info.abapMsgV3)));
  out.Set("msgv4", String::New(env, (const char16_t *) (info.abapMsgV4)));
}

static Napi::Error RfcError(Napi::Env env, const RFC_ERROR_INFO &info) {
  using namespace Napi;
  HandleScope scope{env};

  auto e = Error::New(env, String::New(env, (const char16_t *) (info.message)));
  FillRfcInfo(env, info, e.Value());

  return e;
}

static bool IsException(Napi::Env env, const Napi::Value value) {
  bool result{};
  napi_is_error(env, value, &result);
  return result;
}

template<typename This, typename Api, typename... Args>
Napi::Value call_api(Napi::Env env, Napi::EscapableHandleScope* scope, This* that, const std::string& file, const std::string& function,
              unsigned long line, const char* failMsg, Api api, const char* apiName, Args... args) {
  api(args..., &that->errorInfo);
  that->logAPICall(env, apiName, file, function, line, that->errorInfo);
  if (that->errorInfo.code) {
    if(failMsg) {
      that->log(env, Loggable::Levels::DBG, failMsg);
    }
    if(scope) {
      return scope->Escape(RfcError(env, that->errorInfo).Value());
    } else {
      throw RfcError(env, that->errorInfo);
    }
  }
  return env.Undefined();
}

#define CALL_API_THROW(failMsg_, api_, ...) \
call_api(env, nullptr, this, __FILE__, BOOST_CURRENT_FUNCTION, __LINE__, failMsg_, api_, #api_, __VA_ARGS__)

#define CALL_API(failMsg_, api_, ...) \
{ \
  auto r_ = call_api(env, &scope, this, __FILE__, BOOST_CURRENT_FUNCTION, __LINE__, failMsg_, api_, #api_, __VA_ARGS__); \
  if(!r_.IsUndefined()) { \
    return r_; \
  } \
} while(0)


#endif /* COMMON_H_ */
