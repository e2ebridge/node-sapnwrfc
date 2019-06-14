/*
-----------------------------------------------------------------------------
Copyright (c) 2016 Scheer E2E AG

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

#ifndef SAPNWRFC_FUNCTIONINVOKE_H
#define SAPNWRFC_FUNCTIONINVOKE_H

#include <napi.h>
#include "Connection.h"
#include "Function.h"

class FunctionInvoke : public Napi::AsyncWorker {
  public:
    FunctionInvoke(const Napi::Function &callback, Connection *connection, Function *function,
                   RFC_DATA_CONTAINER *functionHandle);
    FunctionInvoke(const FunctionInvoke &) = delete;
    FunctionInvoke &operator=(const FunctionInvoke &) = delete;
    FunctionInvoke(FunctionInvoke &&) = default;
    FunctionInvoke &operator=(FunctionInvoke &&) = default;

    virtual ~FunctionInvoke();

  protected:
    void Execute() override;
    void OnOK() override;
    void OnError(const Napi::Error &e) override;


  private:
    Connection *connection;
    Function *function;
    RFC_FUNCTION_HANDLE functionHandle;
};


#endif //SAPNWRFC_FUNCTIONINVOKE_H
