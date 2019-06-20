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

#ifndef FUNCTION_H_
#define FUNCTION_H_

#include "Utils.h"
#include "Loggable.h"
#include <sapnwrfc.h>
#include "Connection.h"

class Function : public Loggable, public Napi::ObjectWrap<Function> {
    friend class FunctionInvoke;

  public:
    static Napi::Object Init(Napi::Env env, Napi::Object exports);
    static Napi::Value NewInstance(Napi::Env env, Connection &connection);

    explicit Function(const Napi::CallbackInfo &info);
    ~Function();

    void Lookup(Napi::Env env, std::u16string &functionName, bool refreshMeta);

    RFC_ERROR_INFO errorInfo{};

  protected:

    Napi::Value Invoke(const Napi::CallbackInfo &info);
    Napi::Value MetaData(const Napi::CallbackInfo &info);
    Napi::Value DoReceive(Napi::Env env, CHND container);

    Napi::Value SetValue(Napi::Env env, CHND container, RFCTYPE type, const SAP_UC *name, unsigned len, Napi::Value value);
    Napi::Value StructureToExternal(Napi::Env env, CHND container, const SAP_UC *name, Napi::Value value);
    Napi::Value StructureToExternal(Napi::Env env, RFC_STRUCTURE_HANDLE structHandle, Napi::Value value);
    Napi::Value TableToExternal(Napi::Env env, CHND container, const SAP_UC *name, Napi::Value value);
    Napi::Value StringToExternal(Napi::Env env, CHND container, const SAP_UC *name, Napi::Value value);
    Napi::Value XStringToExternal(Napi::Env env, CHND container, const SAP_UC *name, Napi::Value value);
    Napi::Value NumToExternal(Napi::Env env, CHND container, const SAP_UC *name, Napi::Value value, unsigned len);
    Napi::Value CharToExternal(Napi::Env env, CHND container, const SAP_UC *name, Napi::Value value, unsigned len);
    Napi::Value ByteToExternal(Napi::Env env, CHND container, const SAP_UC *name, Napi::Value value, unsigned len);
    Napi::Value IntToExternal(Napi::Env env, CHND container, const SAP_UC *name, Napi::Value value);
    Napi::Value Int1ToExternal(Napi::Env env, CHND container, const SAP_UC *name, Napi::Value value);
    Napi::Value Int2ToExternal(Napi::Env env, CHND container, const SAP_UC *name, Napi::Value value);
    Napi::Value FloatToExternal(Napi::Env env, CHND container, const SAP_UC *name, Napi::Value value);
    Napi::Value TimeToExternal(Napi::Env env, CHND container, const SAP_UC *name, Napi::Value value);
    Napi::Value DateToExternal(Napi::Env env, CHND container, const SAP_UC *name, Napi::Value value);
    Napi::Value BCDToExternal(Napi::Env env, CHND container, const SAP_UC *name, Napi::Value value);

    Napi::Value GetValue(Napi::Env env, CHND container, RFCTYPE type, const SAP_UC *name, unsigned len);
    Napi::Value StructureToInternal(Napi::Env env, CHND container, const SAP_UC *name);
    Napi::Value StructureToInternal(Napi::Env env, RFC_STRUCTURE_HANDLE structHandle);
    Napi::Value TableToInternal(Napi::Env env, CHND container, const SAP_UC *name);
    Napi::Value StringToInternal(Napi::Env env, CHND container, const SAP_UC *name);
    Napi::Value XStringToInternal(Napi::Env env, CHND container, const SAP_UC *name);
    Napi::Value NumToInternal(Napi::Env env, CHND container, const SAP_UC *name, unsigned len);
    Napi::Value CharToInternal(Napi::Env env, CHND container, const SAP_UC *name, unsigned len);
    Napi::Value ByteToInternal(Napi::Env env, CHND container, const SAP_UC *name, unsigned len);
    Napi::Value IntToInternal(Napi::Env env, CHND container, const SAP_UC *name);
    Napi::Value Int1ToInternal(Napi::Env env, CHND container, const SAP_UC *name);
    Napi::Value Int2ToInternal(Napi::Env env, CHND container, const SAP_UC *name);
    Napi::Value FloatToInternal(Napi::Env env, CHND container, const SAP_UC *name);
    Napi::Value DateToInternal(Napi::Env env, CHND container, const SAP_UC *name);
    Napi::Value TimeToInternal(Napi::Env env, CHND container, const SAP_UC *name);
    Napi::Value BCDToInternal(Napi::Env env, CHND container, const SAP_UC *name);

    static std::string mapExternalTypeToJavaScriptType(RFCTYPE sapType);

    bool addMetaData(Napi::Env env, CHND container, Napi::Object parent,
                     const RFC_ABAP_NAME name, RFCTYPE type, unsigned int length,
                     RFC_DIRECTION direction, RFC_PARAMETER_TEXT paramText = nullptr,
                     bool refresh = false);

    void addObjectInfoToLogMeta(Napi::Object meta) override;

    static Napi::FunctionReference ctor;

    Connection *connection{};
    RFC_FUNCTION_DESC_HANDLE functionDescHandle{};
};

#endif /* FUNCTION_H_ */
