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

#include "Utils.h"
#include "Connection.h"
#include "ConnectionOpen.h"
#include "Function.h"

Napi::FunctionReference Connection::ctor;

Connection::Connection(const Napi::CallbackInfo &info)
    : Napi::ObjectWrap<Connection>(info) {
  uv_mutex_init(&invocationMutex);
  init(Value());
}

Connection::~Connection() {
  deferLog(Levels::SILLY, "Connection::~Connection");

  if (connectionHandle != nullptr) {
    RFC_RC rc = RfcCloseConnection(connectionHandle, &errorInfo);
    DEFER_LOG_API(this, "RfcCloseConnection");
    if (rc != RFC_OK) {
      deferLog(Levels::DBG, "Connection::CloseConnection: Error closing connection");
    }
  }

  uv_mutex_destroy(&invocationMutex);

  for (unsigned int i = 0; i < loginParamsSize; i++) {
    free(const_cast<SAP_UC *>(loginParams[i].name));
    free(const_cast<SAP_UC *>(loginParams[i].value));
  }
  free(loginParams);

  deferLog(Levels::SILLY, "Connection::~Connection [end]");
}

Napi::Object Connection::Init(Napi::Env env, Napi::Object exports) {

  auto con = DefineClass(env, "Connection", {
      InstanceMethod("GetVersion", &Connection::GetVersion),
      InstanceMethod("Open", &Connection::Open),
      InstanceMethod("Close", &Connection::Close),
      InstanceMethod("Ping", &Connection::Ping),
      InstanceMethod("IsOpen", &Connection::IsOpen),
      InstanceMethod("Lookup", &Connection::Lookup),
      InstanceMethod("SetIniPath", &Connection::SetIniPath),
  });

  ctor = Napi::Persistent(con);
  ctor.SuppressDestruct();
  exports.Set("Connection", con);
  return exports;
}

/**
 * @return Array
 */
Napi::Value Connection::GetVersion(const Napi::CallbackInfo &info) {
  auto env = info.Env();
  Napi::EscapableHandleScope scope{env};

  unsigned majorVersion, minorVersion, patchLevel;
  RfcGetVersion(&majorVersion, &minorVersion, &patchLevel);
  auto versionInfo = Napi::Array::New(env, 3);

  versionInfo.Set(uint32_t{0}, Napi::Number::New(env, majorVersion));
  versionInfo.Set(uint32_t{1}, Napi::Number::New(env, minorVersion));
  versionInfo.Set(uint32_t{2}, Napi::Number::New(env, patchLevel));

  return scope.Escape(versionInfo);
}

Napi::Value Connection::Open(const Napi::CallbackInfo &info) {
  auto env = info.Env();
  Napi::EscapableHandleScope scope{env};

  log(env, Levels::VERBOSE, "opening new SAP connection");

  if (info.Length() < 2) {
    throw Napi::Error::New(env, "Function expects 2 arguments");
  }
  if (!info[0].IsObject()) {
    throw Napi::TypeError::New(env, "Argument 1 must be an object");
  }
  if (!info[1].IsFunction()) {
    throw Napi::TypeError::New(env, "Argument 2 must be a function");
  }

  auto optionsObj = info[0].ToObject();
  auto props = optionsObj.GetPropertyNames();

  loginParamsSize = props.Length();
  loginParams = static_cast<RFC_CONNECTION_PARAMETER *>(malloc(loginParamsSize * sizeof(RFC_CONNECTION_PARAMETER)));
  memset(loginParams, 0, loginParamsSize * sizeof(RFC_CONNECTION_PARAMETER));
  memset(&errorInfo, 0, sizeof(RFC_ERROR_INFO));

  log(env, Levels::DBG, "Connection params", optionsObj);

  for (unsigned int i = 0; i < loginParamsSize; i++) {
    auto name = props.Get(i);
    auto value = optionsObj.Get(name);

    loginParams[i].name = convertToSAPUC(name.ToString());
    loginParams[i].value = convertToSAPUC(value.ToString());

#ifndef NDEBUG
    std::cout << name.ToString().Utf8Value() << "--> " << value.ToString().Utf8Value() << std::endl;
#endif
  }

  // Store callback
  auto callback = info[1].As<Napi::Function>();
  auto worker = new ConnectionOpen{callback, this};
  worker->Queue();
  Reference::Ref();
  return env.Undefined();
}

Napi::Value Connection::Close(const Napi::CallbackInfo &info) {
  auto env = info.Env();
  Napi::EscapableHandleScope scope{env};
  log(env, Levels::SILLY, "Connection::Close");
  return scope.Escape(CloseConnection(env));
}

Napi::Value Connection::CloseConnection(Napi::Env env) {
  Napi::EscapableHandleScope scope{env};

  log(env, Levels::SILLY, "Connection::CloseConnection");

  auto handle = connectionHandle;
  if (handle != nullptr) {
    this->connectionHandle = nullptr;
    CALL_API("Connection::CloseConnection: Error closing connection",
              RfcCloseConnection, handle);
  }

  return scope.Escape(Napi::Boolean::New(env, true));
}

RFC_CONNECTION_HANDLE Connection::GetConnectionHandle(void) {
  return this->connectionHandle;
}

void Connection::LockMutex() {
  uv_mutex_lock(&this->invocationMutex);
}

void Connection::UnlockMutex() {
  uv_mutex_unlock(&this->invocationMutex);
}

void Connection::addObjectInfoToLogMeta(Napi::Object meta) {
  char ptr[2 + sizeof(void *) * 2 + 1]; // optional "0x" + each byte of pointer represented by 2 digits + terminator
  snprintf(ptr, 2 + sizeof(void *) * 2 + 1, "%p", this);
  meta.Set("nativeConnection", ptr);
}

Napi::Value Connection::IsOpen(const Napi::CallbackInfo &info) {
  auto env = info.Env();
  Napi::EscapableHandleScope scope{env};

  log(env, Levels::SILLY, "Connection::IsOpen");

  int isValid;
  RfcIsConnectionHandleValid(connectionHandle, &isValid, &errorInfo);
  LOG_API(env, this, "RfcIsConnectionHandleValid");
  if (!isValid) {
    log(env, Levels::SILLY, "Connection::IsOpen: RfcIsConnectionHandleValid returned false");
  } else {
    log(env, Levels::SILLY, "Connection::IsOpen: RfcIsConnectionHandleValid returned true");
  }
  return scope.Escape(Napi::Boolean::New(env, static_cast<bool>(isValid)));
}

/**
 *
 * @return true if successful, else: RfcException
 */
Napi::Value Connection::Ping(const Napi::CallbackInfo &info) {
  auto env = info.Env();
  Napi::EscapableHandleScope scope{env};

  log(env, Levels::SILLY, "Connection::Ping");

  if (info.Length() > 0) {
    throw Napi::Error::New(env, "No arguments expected");
  }

  CALL_API("Connection::Ping: RfcPing failed", RfcPing, connectionHandle);

  return scope.Escape(Napi::Boolean::New(env, true));
}

/**
 *
 * @return Function
 */
Napi::Value Connection::Lookup(const Napi::CallbackInfo &info) {
  auto env = info.Env();
  Napi::EscapableHandleScope scope{env};
  log(env, Levels::SILLY, "Connection::Lookup");

  if (info.Length() < 1 || info.Length() > 2) {
    throw Napi::Error::New(env, "Function expects 1 or 2 arguments");
  }
  if (!info[0].IsString()) {
    throw Napi::TypeError::New(env, "Argument 1 must be function module name");
  }
  if (info.Length() > 1 && !info[1].IsObject()) {
    throw Napi::TypeError::New(env, "Argument 2 must be an object");
  }

  bool refreshMeta = info.Length() > 1 && info[1].ToObject().Get("refreshMeta").ToBoolean();
  auto functionName = info[0].ToString().Utf16Value();

  int isValid{};
  RfcIsConnectionHandleValid(connectionHandle, &isValid, &errorInfo);
  LOG_API(env, this, "RfcIsConnectionHandleValid");
  if (!isValid) {
    log(env, Levels::SILLY, "Connection::Lookup: RfcIsConnectionHandleValid returned false");
    throw RfcError(env, errorInfo);
  } else {
    log(env, Levels::SILLY, "Connection::Lookup: RfcIsConnectionHandleValid returned true");
  }

  log(env, Levels::SILLY, "Connection::Lookup: About to create function instance");
  auto jsf = Function::NewInstance(env, *this).As<Napi::Object>();
  Napi::ObjectWrap<Function>::Unwrap(jsf)->Lookup(env, functionName, refreshMeta);
  return scope.Escape(jsf);
}


/**
 *
 * @return true if successful, else: RfcException
 */
Napi::Value Connection::SetIniPath(const Napi::CallbackInfo &info) {
  auto env = info.Env();
  Napi::EscapableHandleScope scope{env};

  log(env, Levels::SILLY, "Connection::SetIniPath");

  if (info.Length() != 1) {
    throw Napi::Error::New(env, "Function expects 1 argument");
  }
  if (!info[0].IsString()) {
    throw Napi::TypeError::New(env, "Argument 1 must be a path name");
  }

  CALL_API_THROW("Connection::SetIniPath: RfcSetIniPath failed",
                  RfcSetIniPath,
                  reinterpret_cast<const SAP_UC *>(info[0].ToString().Utf16Value().c_str()));

  return scope.Escape(Napi::Boolean::New(env, true));
}
