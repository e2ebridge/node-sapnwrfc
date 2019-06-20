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

#include "Loggable.h"
#include "Common.h"
#include <algorithm>

const std::string Loggable::Levels::SILLY	= "silly";
const std::string Loggable::Levels::DBG	= "debug";
const std::string Loggable::Levels::VERBOSE	= "verbose";
const std::string Loggable::Levels::INFO	= "info";
const std::string Loggable::Levels::WARN	= "warn";
const std::string Loggable::Levels::ERR	= "error";

const std::string Loggable::API_CALL_MESSAGE	= "SAPNWRFC API Call";

Napi::Value metaToJS(Napi::Env env, const Loggable::LogEntry::Meta &meta) {
  Napi::EscapableHandleScope scope{env};

  auto result = Napi::Object::New(env);

  for (const auto &entry : meta) {
    result.Set(entry.first, entry.second);
  }

  return scope.Escape(result);
}

static Napi::Value errorInfoToJS(Napi::Env env, const RFC_ERROR_INFO &info) {
  Napi::EscapableHandleScope scope{env};

  auto obj = Napi::Object::New(env);
  FillRfcInfo(env, info, obj);
  return scope.Escape(obj);
}

void Loggable::init(Napi::Object thisHandle) {
  logFunction = Napi::Persistent(thisHandle.Get("_log").As<Napi::Function>());
}

void Loggable::log(Napi::Env env, const std::string &level, const std::string &message) {
  Napi::HandleScope scope{env};
  log(env, level, Napi::String::New(env, message));
}

void Loggable::log(Napi::Env env, const std::string &level, Napi::Value message) {
  Napi::HandleScope scope{env};
  log(env, level, message, env.Undefined());
}

void Loggable::log(Napi::Env env, const std::string &level, const std::string &message, Napi::Value meta) {
  Napi::HandleScope scope{env};
  log(env, level, Napi::String::New(env, message), meta);
}

void Loggable::log(Napi::Env env, const std::string &level, Napi::Value message, Napi::Value meta) {
  logDeferred(env);
  log_(env, level, message, meta);
}

void Loggable::log_(Napi::Env env, const std::string &level, Napi::Value message, Napi::Value meta) {
  Napi::HandleScope scope{env};

  if (meta.IsEmpty() || meta.IsUndefined()) {
    meta = Napi::Object::New(env);
  }

  addObjectInfoToLogMeta(meta.ToObject());
  logFunction({Napi::String::New(env, level), message, meta});
}

void Loggable::log(Napi::Env env, const LogEntry &logEntry) {
  Napi::HandleScope scope{env};

  Napi::Object meta;
  if (!logEntry.call.empty()) {
    meta = metaToJS(env, logEntry.meta).As<Napi::Object>();
    meta.Set("call", logEntry.call);
    meta.Set("errorInfo", errorInfoToJS(env, logEntry.errorInfo));
  } else {
    meta = Napi::Object::New(env);
  }

  if (!logEntry.file.empty()) {
    meta.Set("file", logEntry.file);
  }

  if (!logEntry.function.empty()) {
    meta.Set("function", logEntry.function);
  }

  if (logEntry.line) {
    meta.Set("line", logEntry.line);
  }

  log_(env, logEntry.level, Napi::String::From(env, logEntry.message), meta);
}

void Loggable::deferLog(const std::string &level, const std::string &message, const Loggable::LogEntry::Meta &meta) {
  LogEntry entry;
  entry.level = level;
  entry.message = message;
  entry.meta = meta;
  deferredLogs.push_back(entry);
}

void Loggable::createAPILogEntry_(Loggable::LogEntry &logEntry, const std::string &call,
                                  const std::string &file, const std::string &function,
                                  unsigned long line, RFC_ERROR_INFO &errorInfo,
                                  const Loggable::LogEntry::Meta &meta) {
  logEntry.level = Levels::DBG;
  logEntry.message = API_CALL_MESSAGE;
  logEntry.call = call;
  logEntry.file = file;
  logEntry.function = function;
  logEntry.line = line;
  logEntry.errorInfo = errorInfo;
  logEntry.meta = meta;
}

void Loggable::logAPICall(Napi::Env env, const std::string &call, const std::string &file, const std::string &function,
                          unsigned long line, RFC_ERROR_INFO &errorInfo) {
  logAPICall(env, call, file, function, line, errorInfo, LogEntry::Meta());
}

void Loggable::logAPICall(Napi::Env env, const std::string &call, const std::string &file, const std::string &function,
                          unsigned long line, RFC_ERROR_INFO &errorInfo, const Loggable::LogEntry::Meta &meta) {
  logDeferred(env);
  LogEntry logEntry;
  createAPILogEntry_(logEntry, call, file, function, line, errorInfo, meta);
  log(env, logEntry);
}

void Loggable::deferLogAPICall(const std::string &call, const std::string &file, const std::string &function,
                               unsigned long line, RFC_ERROR_INFO &errorInfo) {
  deferLogAPICall(call, file, function, line, errorInfo, LogEntry::Meta());
}

void Loggable::deferLogAPICall(const std::string &call, const std::string &file, const std::string &function,
                               unsigned long line, RFC_ERROR_INFO &errorInfo, const Loggable::LogEntry::Meta &meta) {
  LogEntry logEntry;
  createAPILogEntry_(logEntry, call, file, function, line, errorInfo, meta);
  deferredLogs.push_back(logEntry);
}

void Loggable::logDeferred(Napi::Env env) {
  for (const auto &entry : deferredLogs) {
    log(env, entry);
  }
  deferredLogs.clear();
}

void Loggable::resetLogFunction() {
  logFunction.Reset();
}
