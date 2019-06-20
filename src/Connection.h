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

#ifndef CONNECTION_H_
#define CONNECTION_H_

#include "Loggable.h"
#include <uv.h>
#include <sapnwrfc.h>
#include <iostream>

class Connection : public Loggable, public Napi::ObjectWrap<Connection> {
    friend class Function;
    friend class FunctionInvoke;
    friend class ConnectionOpen;

  public:
    static Napi::Object Init(Napi::Env env, Napi::Object exports);
    explicit Connection(const Napi::CallbackInfo &info);
    ~Connection();

    RFC_ERROR_INFO errorInfo{};
  protected:
    Napi::Value GetVersion(const Napi::CallbackInfo &info);
    Napi::Value Open(const Napi::CallbackInfo &info);
    Napi::Value Close(const Napi::CallbackInfo &info);
    Napi::Value Ping(const Napi::CallbackInfo &info);
    Napi::Value Lookup(const Napi::CallbackInfo &info);
    Napi::Value IsOpen(const Napi::CallbackInfo &info);
    Napi::Value SetIniPath(const Napi::CallbackInfo &info);

    Napi::Value CloseConnection(Napi::Env env);

    RFC_CONNECTION_HANDLE GetConnectionHandle();
    void LockMutex();
    void UnlockMutex();
    void addObjectInfoToLogMeta(Napi::Object meta) override;

    unsigned int loginParamsSize{};
    RFC_CONNECTION_PARAMETER *loginParams{};
    RFC_CONNECTION_HANDLE connectionHandle{};
    static Napi::FunctionReference ctor;

    uv_mutex_t invocationMutex;
};

#endif /* CONNECTION_H_ */
