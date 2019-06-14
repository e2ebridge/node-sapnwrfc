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

#include "ConnectionOpen.h"
#include <sapnwrfc.h>

ConnectionOpen::ConnectionOpen(const Napi::Function &callback, Connection *connection)
    : AsyncWorker{callback}, connection{connection} {}

void ConnectionOpen::Execute() {
  connection->connectionHandle = RfcOpenConnection(connection->loginParams,
                                                   connection->loginParamsSize,
                                                   &connection->errorInfo);
  DEFER_LOG_API(connection, "RfcOpenConnection");
  if (!connection->connectionHandle) {
    SetError("Connection handle is NULL, connection failed");
  } else {
    int isValid{};
    RfcIsConnectionHandleValid(connection->connectionHandle, &isValid, &connection->errorInfo);
    DEFER_LOG_API(connection, "RfcIsConnectionHandleValid");
    if (!isValid) {
      connection->deferLog(Loggable::Levels::SILLY, "Connection not valid");
      SetError("Connection not valid");
    } else {
      connection->deferLog(Loggable::Levels::SILLY, "Connection still valid");
    }
  }
}

void ConnectionOpen::OnError(const Napi::Error &e) {
  Callback().Call({RfcError(Env(), connection->errorInfo).Value()});
}

ConnectionOpen::~ConnectionOpen() {
  connection->Reference::Unref();
}
