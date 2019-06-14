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

#include "FunctionInvoke.h"

FunctionInvoke::FunctionInvoke(const Napi::Function &callback, Connection *connection, Function *function,
                               RFC_DATA_CONTAINER *functionHandle)
    : AsyncWorker(callback), connection(connection), function(function), functionHandle(functionHandle) {}


void FunctionInvoke::Execute() {
  assert(functionHandle != nullptr);
  assert(connection != nullptr);
  assert(function != nullptr);

  connection->LockMutex();

  // Invocation
  RfcInvoke(connection->GetConnectionHandle(), functionHandle, &function->errorInfo);
  DEFER_LOG_API(function, "RfcInvoke");

  // If handle is invalid, fetch a better error message
  if (function->errorInfo.code == RFC_INVALID_HANDLE) {
    int isValid{};
    RfcIsConnectionHandleValid(connection->GetConnectionHandle(), &isValid, &function->errorInfo);
    DEFER_LOG_API(function, "RfcIsConnectionHandleValid");
  }

  connection->UnlockMutex();

  if (function->errorInfo.code != RFC_OK) {
    SetError("Error invoking function");
  }
}

void FunctionInvoke::OnOK() {
  Napi::HandleScope scope{Env()};
  auto result = function->DoReceive(Env(), functionHandle);
  if (IsException(Env(), result)) {
    Callback().Call({result, Env().Undefined()});
  } else {
    Callback().Call({Env().Undefined(), result});
  }
}

void FunctionInvoke::OnError(const Napi::Error &e) {
  Callback().Call({RfcError(Env(), function->errorInfo).Value()});
}

FunctionInvoke::~FunctionInvoke() {
  if (functionHandle) {
    RfcDestroyFunction(functionHandle, &function->errorInfo);
    LOG_API(Env(), function, "RfcDestroyFunction");
  }
  function->Reference::Unref();
}
