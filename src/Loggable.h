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

#ifndef LOGGABLE_H_
#define LOGGABLE_H_

#include <sapnwrfc.h>
#include <napi.h>
#include <string>
#include <vector>
#include <utility>
#include "current_function.hpp"

class Loggable {
  public:
    class Levels {
      public:
        static const std::string SILLY;
        static const std::string DBG;                // DEBUG may be set by GYP on various occasions...
        static const std::string VERBOSE;
        static const std::string INFO;
        static const std::string WARN;
        static const std::string ERR; // ERROR is set by windows.h. I don't even...
    };

    struct LogEntry {
      typedef std::vector<std::pair<std::string, std::string> > Meta;

      std::string level{};
      std::string message{};
      std::string file{};
      uint32_t line{};
      std::string function{};
      std::string call{};
      Meta meta{};
      RFC_ERROR_INFO errorInfo{};
    };

    static const std::string API_CALL_MESSAGE;

    ~Loggable() = default;

    void log(Napi::Env env, const std::string &level, const char *message) { log(env, level, std::string(message)); }
    void log(Napi::Env env, const std::string &level, const std::string &message);
    void log(Napi::Env env, const std::string &level, Napi::Value message);
    void log(Napi::Env env, const std::string &level, const char *message, Napi::Value meta) {
      log(env, level, std::string(message), meta);
    }
    void log(Napi::Env env, const std::string &level, const std::string &message, Napi::Value meta);
    void log(Napi::Env env, const std::string &level, Napi::Value message, Napi::Value meta);
    void log_(Napi::Env env, const std::string &level, Napi::Value message, Napi::Value meta);
    void log(Napi::Env env, const LogEntry &logEntry);
    void logAPICall(Napi::Env env, const std::string &call, const std::string &file, const std::string &function,
                    unsigned long line, RFC_ERROR_INFO &errorInfo);
    void logAPICall(Napi::Env env, const std::string &call, const std::string &file, const std::string &function,
                    unsigned long line, RFC_ERROR_INFO &errorInfo, const Loggable::LogEntry::Meta &meta);
    void deferLogAPICall(const std::string &call, const std::string &file, const std::string &function,
                         unsigned long line, RFC_ERROR_INFO &errorInfo);
    void deferLogAPICall(const std::string &call, const std::string &file, const std::string &function,
                         unsigned long line, RFC_ERROR_INFO &errorInfo, const Loggable::LogEntry::Meta &meta);

    /*
     * Since logging is being done on the JS side, we cannot log anything directly in libuv's
     * work task (it works in different thread). This method allows to queue logs to output them
     * at later time (at latest just before regular log is outputted, so that order is preserved)
     * */
    void deferLog(const std::string &level, const std::string &message, const LogEntry::Meta &meta = LogEntry::Meta());
    void logDeferred(Napi::Env env);

    void resetLogFunction();

  protected:

    Napi::FunctionReference logFunction;

    void init(Napi::Object thisHandle);

    typedef std::vector<LogEntry> LogEntries;
    LogEntries deferredLogs;

    void createAPILogEntry_(Loggable::LogEntry &logEntry, const std::string &call,
                            const std::string &file, const std::string &function,
                            unsigned long line, RFC_ERROR_INFO &errorInfo,
                            const Loggable::LogEntry::Meta &meta);

    virtual void addObjectInfoToLogMeta(Napi::Object meta) = 0;
};

#define LOG_API(env, self, call) { (self)->logAPICall((env),(call),__FILE__,BOOST_CURRENT_FUNCTION,__LINE__,(self)->errorInfo);}while(0)
#define DEFER_LOG_API(self, call) { (self)->deferLogAPICall((call),__FILE__,BOOST_CURRENT_FUNCTION,__LINE__,(self)->errorInfo);}while(0)

#endif /* LOGGABLE_H_ */
