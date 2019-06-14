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

#include "Function.h"
#include "FunctionInvoke.h"
#include <cassert>
#include <sstream>
#include <limits>
#include <cstdint>

Napi::FunctionReference Function::ctor;

Function::Function(const Napi::CallbackInfo &info)
    : Napi::ObjectWrap<Function>(info) {
  init(Value());
  log(info.Env(), Levels::SILLY, "Function::Function (begin)");
}

Function::~Function() {
  deferLog(Levels::SILLY, "Function::~Function");
}

Napi::Object Function::Init(Napi::Env env, Napi::Object exports) {
  Napi::Function func = DefineClass(env, "Function", {
      InstanceMethod("Invoke", &Function::Invoke),
      InstanceMethod("MetaData", &Function::MetaData)
  });

  ctor = Napi::Persistent(func);
  ctor.SuppressDestruct();
  exports.Set("Function", func);
  return exports;
}

Napi::Value Function::NewInstance(Napi::Env env, Connection &connection) {
  Napi::EscapableHandleScope scope{env};

  auto func = ctor.New({});
  Function *self = Napi::ObjectWrap<Function>::Unwrap(func);

  // Save connection
  assert(self != nullptr);
  self->connection = &connection;

  self->log(env, Levels::SILLY, "Function::NewInstance");
  return scope.Escape(func);
}

void Function::Lookup(Napi::Env env, std::u16string &functionName, bool refreshMeta) {
  Napi::EscapableHandleScope scope{env};

  log(env, Levels::SILLY, "Function::NewInstance");
  assert(connection);

  if (refreshMeta) {
    log(env, Levels::SILLY, "Perfoming function descriptor refresh");
    RFC_ATTRIBUTES connectionAttributes;
    RfcGetConnectionAttributes(connection->GetConnectionHandle(), &connectionAttributes, &errorInfo);
    LOG_API(env, this, "RfcGetConnectionAttributes");
    RfcRemoveFunctionDesc(connectionAttributes.sysId, (const SAP_UC *) functionName.c_str(), &errorInfo);
    LOG_API(env, this, "RfcRemoveFunctionDesc");
  }

  // Lookup function interface
  functionDescHandle = RfcGetFunctionDesc(connection->GetConnectionHandle(), (const SAP_UC *) functionName.c_str(),
                                          &errorInfo);
  LOG_API(env, this, "RfcGetFunctionDesc");
  assert(errorInfo.code != RFC_INVALID_HANDLE);

  if (functionDescHandle == nullptr) {
    log(env, Levels::DBG, "Function::NewInstance: Function description handle is NULL.");
    throw RfcError(env, errorInfo);
  }

  unsigned int parmCount;
  RfcGetParameterCount(functionDescHandle, &parmCount, &errorInfo);
  LOG_API(env, this, "RfcGetParameterCount");
  if (errorInfo.code != RFC_OK) {
    log(env, Levels::DBG, "Function::NewInstance: RfcGetParameterCount unsuccessful");
    throw RfcError(env, errorInfo);
  }

  // Dynamically add parameters to JS object
  for (unsigned int i = 0; i < parmCount; i++) {
    RFC_PARAMETER_DESC parmDesc;

    RfcGetParameterDescByIndex(functionDescHandle, i, &parmDesc, &errorInfo);
    if (errorInfo.code != RFC_OK) {
      log(env, Levels::DBG, "Function::NewInstance: RfcGetParameterDescByIndex unsuccessful");
      throw RfcError(env, errorInfo);
    }
    Value().Set(Napi::String::New(env, (const char16_t *) (parmDesc.name)), env.Null());
  }
}


Napi::Value Function::Invoke(const Napi::CallbackInfo &info) {
  auto env = info.Env();
  Napi::EscapableHandleScope scope{env};
  unsigned int parmCount;

  log(env, Levels::SILLY, "Function::Invoke");

  if (info.Length() < 2) {
    throw Napi::Error::New(env, "Function expects 2 arguments");
  }
  if (!info[0].IsObject()) {
    throw Napi::TypeError::New(env, "Argument 1 must be an object");
  }
  if (!info[1].IsFunction()) {
    throw Napi::TypeError::New(env, "Argument 2 must be a function");
  }

  auto callback = info[1].As<Napi::Function>();

  auto functionHandle = RfcCreateFunction(functionDescHandle, &errorInfo);
  LOG_API(env, this, "RfcCreateFunction");
  if (functionHandle == nullptr) {
    log(env, Levels::DBG, "Function::Invoke: RfcCreateFunction finished with error");
    return scope.Escape(RfcError(Env(), errorInfo).Value());
  }

  RfcGetParameterCount(functionDescHandle, &parmCount, &errorInfo);
  LOG_API(env, this, "RfcGetParameterCount");
  if (errorInfo.code != RFC_OK) {
    log(env, Levels::DBG, "Function::Invoke: RfcGetParameterCount returned with error");
    return scope.Escape(RfcError(Env(), errorInfo).Value());
  }

  auto inputParam = info[0].ToObject();

  for (unsigned int i = 0; i < parmCount; i++) {
    RFC_PARAMETER_DESC paramDesc;

    RfcGetParameterDescByIndex(functionDescHandle, i, &paramDesc, &errorInfo);
    LOG_API(env, this, "RfcGetParameterDescByIndex");
    if (errorInfo.code != RFC_OK) {
      log(env, Levels::DBG, "Function::Invoke: RfcGetParameterDescByIndex finished with error");
      return scope.Escape(RfcError(Env(), errorInfo).Value());
    }

    auto parmName = Napi::String::New(env, (const char16_t *) (paramDesc.name));
    auto result = env.Undefined();

    if (inputParam.Has(parmName) && !inputParam.Get(parmName).IsNull()) {
      switch (paramDesc.direction) {
        case RFC_IMPORT:
        case RFC_CHANGING:
        case RFC_TABLES:
          result = SetValue(env, functionHandle, paramDesc.type, paramDesc.name, paramDesc.nucLength,
                            inputParam.Get(parmName));
          break;
        case RFC_EXPORT:
        default:
          break;
      }

      if (IsException(env, result)) {
        log(env, Levels::SILLY, "Function::Invoke: About call callback with error.");
        callback.Call({result, env.Null()});
        return env.Undefined();
      }
    }

    RfcSetParameterActive(functionHandle, paramDesc.name, true, &errorInfo);
    LOG_API(env, this, "RfcSetParameterActive");
    if (errorInfo.code != RFC_OK) {
      log(env, Levels::DBG, "Function::Invoke: RfcSetParameterActive returned error.");
      return scope.Escape(RfcError(Env(), errorInfo).Value());
    }
  }

  auto worker = new FunctionInvoke{callback, connection, this, functionHandle};
  worker->Queue();

  // This must be alive when the callback will be called.
  Reference::Ref();

  return env.Undefined();
}

Napi::Value Function::MetaData(const Napi::CallbackInfo &info) {
  auto env = info.Env();
  Napi::EscapableHandleScope scope{env};
  unsigned int parmCount;

  log(env, Levels::SILLY, "Function::MetaData");

  // get the options
  bool refreshMeta = false;
  if (info.Length() > 0) {
    auto optionsObject = info[0].ToObject();
    refreshMeta = optionsObject.Get("refresh").ToBoolean();
  }

  RfcGetParameterCount(functionDescHandle, &parmCount, &errorInfo);
  LOG_API(env, this, "RfcGetParameterCount");
  if (errorInfo.code != RFC_OK) {
    log(env, Levels::DBG, "Function::MetaData: RfcGetParameterCount returned with error");
    return scope.Escape(RfcError(env, errorInfo).Value());
  }

  auto metaObject = Napi::Object::New(env);
  RFC_ABAP_NAME functionName;
  RfcGetFunctionName(functionDescHandle, functionName, &errorInfo);
  LOG_API(env, this, "RfcGetFunctionName");
  if (errorInfo.code != RFC_OK) {
    log(env, Levels::DBG, "Function::MetaData: RfcGetFunctionName returned with error");
    return scope.Escape(RfcError(env, errorInfo).Value());
  }

  RFC_FUNCTION_HANDLE functionHandle = RfcCreateFunction(functionDescHandle, &errorInfo);
  LOG_API(env, this, "RfcCreateFunction");
  if (functionHandle == nullptr) {
    log(env, Levels::DBG, "Function::MetaData: RfcCreateFunction finished with error");
    RfcDestroyFunction(functionHandle, &errorInfo);
    LOG_API(env, this, "RfcDestroyFunction");
    return scope.Escape(RfcError(env, errorInfo).Value());
  }

  std::string title = "Signature of SAP RFC function " + convertToString(env, functionName);

  metaObject.Set("title", Napi::String::New(env, title));
  metaObject.Set("type", Napi::String::New(env, "object"));

  auto properties = Napi::Object::New(env);
  metaObject.Set("properties", properties);

  // Dynamically add parameters to JS object
  for (unsigned int i = 0; i < parmCount; i++) {
    RFC_PARAMETER_DESC parmDesc;

    RfcGetParameterDescByIndex(functionDescHandle, i, &parmDesc, &errorInfo);
    LOG_API(env, this, "RfcGetParameterDescByIndex");
    if (errorInfo.code != RFC_OK) {
      log(env, Levels::DBG, "Function::MetaData: RfcGetParameterDescByIndex finished with error");
      return scope.Escape(RfcError(env, errorInfo).Value());
    }

    if (!addMetaData(env, functionHandle, properties, parmDesc.name, parmDesc.type,
                     parmDesc.nucLength, parmDesc.direction,
                     parmDesc.parameterText, refreshMeta)) {
      return scope.Escape(RfcError(env, errorInfo).Value());
    }
  }

  RfcDestroyFunction(functionHandle, &errorInfo);
  LOG_API(env, this, "RfcDestroyFunction");

  return scope.Escape(metaObject);
}


Napi::Value Function::DoReceive(Napi::Env env, CHND container) {
  Napi::EscapableHandleScope scope{env};
  unsigned int parmCount;

  auto result = Napi::Object::New(env);

  // Get resulting values for exporting/changing/table parameters
  RfcGetParameterCount(this->functionDescHandle, &parmCount, &errorInfo);
  LOG_API(env, this, "RfcGetParameterCount");
  if (errorInfo.code != RFC_OK) {
    return scope.Escape(RfcError(env, errorInfo).Value());
  }

  for (unsigned int i = 0; i < parmCount; i++) {
    RFC_PARAMETER_DESC parmDesc;
    auto paramValue = Napi::Value();

    RfcGetParameterDescByIndex(this->functionDescHandle, i, &parmDesc, &errorInfo);
    LOG_API(env, this, "RfcGetParameterDescByIndex");
    if (errorInfo.code != RFC_OK) {
      return scope.Escape(RfcError(env, errorInfo).Value());
    }

    switch (parmDesc.direction) {
      case RFC_IMPORT:
        //break;
      case RFC_CHANGING:
      case RFC_TABLES:
      case RFC_EXPORT:
        paramValue = this->GetValue(env, container, parmDesc.type, parmDesc.name, parmDesc.nucLength);
        if (IsException(env, paramValue)) {
          return scope.Escape(paramValue);
        }
        result.Set(Napi::String::New(env, (const char16_t *) (parmDesc.name)), paramValue);
        break;
      default:
        assert(0);
        break;
    }
  }

  return scope.Escape(result);
}

Napi::Value
Function::SetValue(Napi::Env env, CHND container, RFCTYPE type, const SAP_UC *name, unsigned len, Napi::Value value) {
  Napi::EscapableHandleScope scope{env};

  auto result = env.Undefined();

  switch (type) {
    case RFCTYPE_DATE:
      result = DateToExternal(env, container, name, value);
      break;
    case RFCTYPE_TIME:
      result = TimeToExternal(env, container, name, value);
      break;
    case RFCTYPE_NUM:
      result = NumToExternal(env, container, name, value, len);
      break;
    case RFCTYPE_BCD:
      result = BCDToExternal(env, container, name, value);
      break;
    case RFCTYPE_CHAR:
      result = CharToExternal(env, container, name, value, len);
      break;
    case RFCTYPE_BYTE:
      result = ByteToExternal(env, container, name, value, len);
      break;
    case RFCTYPE_FLOAT:
      result = FloatToExternal(env, container, name, value);
      break;
    case RFCTYPE_INT:
      result = IntToExternal(env, container, name, value);
      break;
    case RFCTYPE_INT1:
      result = Int1ToExternal(env, container, name, value);
      break;
    case RFCTYPE_INT2:
      result = Int2ToExternal(env, container, name, value);
      break;
    case RFCTYPE_STRUCTURE:
      result = StructureToExternal(env, container, name, value);
      break;
    case RFCTYPE_TABLE:
      result = TableToExternal(env, container, name, value);
      break;
    case RFCTYPE_STRING:
      result = StringToExternal(env, container, name, value);
      break;
    case RFCTYPE_XSTRING:
      result = XStringToExternal(env, container, name, value);
      break;
    default:
      // Type not implemented
      auto err = "RFC type not implemented: " + std::to_string(type);
      result = Napi::Error::New(env, err).Value();
      break;
  }

  if (IsException(env, result)) {
    return scope.Escape(result);
  }

  return scope.Escape(env.Null());
}

Napi::Value Function::StructureToExternal(Napi::Env env, CHND container, const SAP_UC *name, Napi::Value value) {
  Napi::EscapableHandleScope scope{env};
  RFC_STRUCTURE_HANDLE structHandle;

  RfcGetStructure(container, name, &structHandle, &errorInfo);
  LOG_API(env, this, "RfcGetStructure");
  if (errorInfo.code != RFC_OK) {
    return scope.Escape(RfcError(env, errorInfo).Value());
  }

  return scope.Escape(StructureToExternal(env, structHandle, value));
}

Napi::Value Function::StructureToExternal(Napi::Env env, RFC_STRUCTURE_HANDLE structHandle, Napi::Value value) {
  Napi::EscapableHandleScope scope{env};

  if (!value.IsObject()) {
    return scope.Escape(Napi::Error::New(env, "StructureToExternal: Object expected").Value());
  }
  auto valueObj = value.ToObject();

  auto typeHandle = RfcDescribeType(structHandle, &errorInfo);
  LOG_API(env, this, "RfcDescribeType");
  assert(typeHandle);
  if (typeHandle == nullptr) {
    return scope.Escape(RfcError(env, errorInfo).Value());
  }

  unsigned fieldCount;
  RfcGetFieldCount(typeHandle, &fieldCount, &errorInfo);
  LOG_API(env, this, "RfcGetFieldCount");
  if (errorInfo.code != RFC_OK) {
    return scope.Escape(RfcError(env, errorInfo).Value());
  }

  for (unsigned int i = 0; i < fieldCount; i++) {
    RFC_FIELD_DESC fieldDesc;
    RfcGetFieldDescByIndex(typeHandle, i, &fieldDesc, &errorInfo);
    LOG_API(env, this, "RfcGetFieldDescByIndex");
    if (errorInfo.code != RFC_OK) {
      return scope.Escape(RfcError(env, errorInfo).Value());
    }

    auto fieldName = Napi::String::New(env, (const char16_t *) (fieldDesc.name));

    if (valueObj.Has(fieldName)) {
      auto result = SetValue(env, structHandle, fieldDesc.type, fieldDesc.name, fieldDesc.nucLength,
                             valueObj.Get(fieldName));
      // Bail out on exception
      if (IsException(env, result)) {
        return scope.Escape(result);
      }
    }
  }

  return scope.Escape(env.Null());
}

Napi::Value Function::TableToExternal(Napi::Env env, CHND container, const SAP_UC *name, Napi::Value value) {
  Napi::EscapableHandleScope scope{env};

  if (!value.IsArray()) {
    auto err = "Argument has unexpected type: " + convertToString(env, name);
    return scope.Escape(Napi::TypeError::New(env, err).Value());
  }

  RFC_TABLE_HANDLE tableHandle;
  RfcGetTable(container, name, &tableHandle, &errorInfo);
  LOG_API(env, this, "RfcGetTable");
  if (errorInfo.code != RFC_OK) {
    return scope.Escape(RfcError(env, errorInfo).Value());
  }

  auto source = value.As<Napi::Array>();
  auto rowCount = source.Length();

  for (uint32_t i = 0; i < rowCount; i++) {
    auto structHandle = RfcAppendNewRow(tableHandle, &errorInfo);
    LOG_API(env, this, "RfcAppendNewRow");

    auto result = StructureToExternal(env, structHandle, source.Get(i));
    // Bail out on exception
    if (IsException(env, result)) {
      return scope.Escape(result);
    }
  }

  return scope.Escape(env.Null());
}

Napi::Value Function::StringToExternal(Napi::Env env, CHND container, const SAP_UC *name, Napi::Value value) {
  Napi::EscapableHandleScope scope{env};

  if (!value.IsString()) {
    auto err = "Argument has unexpected type: " + convertToString(env, name);
    return scope.Escape(Napi::TypeError::New(env, err).Value());
  }

  auto valueU16 = value.ToString().Utf16Value();
  RfcSetString(container, name, (const SAP_UC *) valueU16.data(), valueU16.length(), &errorInfo);
  LOG_API(env, this, "RfcSetString");
  if (errorInfo.code != RFC_OK) {
    return scope.Escape(RfcError(env, errorInfo).Value());
  }

  return scope.Escape(env.Null());
}

Napi::Value Function::XStringToExternal(Napi::Env env, CHND container, const SAP_UC *name, Napi::Value value) {
  Napi::EscapableHandleScope scope{env};

  if (!value.IsBuffer()) {
    auto err = "Argument has unexpected type: " + convertToString(env, name);
    return scope.Escape(Napi::TypeError::New(env, err).Value());
  }

  auto buffer = value.As<Napi::Buffer<SAP_RAW>>();

  RfcSetXString(container, name, buffer.Data(), buffer.Length(), &errorInfo);
  LOG_API(env, this, "RfcSetXString");
  if (errorInfo.code != RFC_OK) {
    return scope.Escape(RfcError(env, errorInfo).Value());
  }

  return scope.Escape(env.Null());
}

Napi::Value
Function::NumToExternal(Napi::Env env, CHND container, const SAP_UC *name, Napi::Value value, unsigned len) {
  Napi::EscapableHandleScope scope{env};

  if (!value.IsString()) {
    auto err = "Argument has unexpected type: " + convertToString(env, name);
    return scope.Escape(Napi::TypeError::New(env, err).Value());
  }

  auto valueU16 = value.ToString().Utf16Value();
  if (valueU16.length() > len) {
    auto err = "Argument exceeds maximum length: " + convertToString(env, name);
    return scope.Escape(Napi::TypeError::New(env, err).Value());
  }

  RfcSetNum(container, name, (const RFC_NUM *) valueU16.data(), valueU16.length(), &errorInfo);
  LOG_API(env, this, "RfcSetNum");
  if (errorInfo.code != RFC_OK) {
    return scope.Escape(RfcError(env, errorInfo).Value());
  }

  return scope.Escape(env.Null());
}

Napi::Value
Function::CharToExternal(Napi::Env env, CHND container, const SAP_UC *name, Napi::Value value, unsigned len) {
  Napi::EscapableHandleScope scope{env};

  if (!value.IsString()) {
    auto err = "Argument has unexpected type: " + convertToString(env, name);
    return scope.Escape(Napi::TypeError::New(env, err).Value());
  }

  auto valueU16 = value.ToString().Utf16Value();
  if (valueU16.length() > len) {
    auto err = "Argument exceeds maximum length: " + convertToString(env, name);
    return scope.Escape(Napi::TypeError::New(env, err).Value());
  }

  RfcSetChars(container, name, (const RFC_CHAR *) valueU16.data(), valueU16.length(), &errorInfo);
  LOG_API(env, this, "RfcSetChars");
  if (errorInfo.code != RFC_OK) {
    return scope.Escape(RfcError(env, errorInfo).Value());
  }

  return scope.Escape(env.Null());
}

Napi::Value
Function::ByteToExternal(Napi::Env env, CHND container, const SAP_UC *name, Napi::Value value, unsigned len) {
  Napi::EscapableHandleScope scope{env};

  if (!value.IsBuffer()) {
    auto err = "Argument has unexpected type: " + convertToString(env, name);
    return scope.Escape(Napi::TypeError::New(env, err).Value());
  }

  auto buffer = value.As<Napi::Buffer<SAP_RAW>>();
  if (buffer.Length() > len) {
    auto err = "Argument exceeds maximum length: " + convertToString(env, name);
    return scope.Escape(Napi::TypeError::New(env, err).Value());
  }

  RfcSetBytes(container, name, buffer.Data(), buffer.Length(), &errorInfo);
  LOG_API(env, this, "RfcSetBytes");
  if (errorInfo.code != RFC_OK) {
    return scope.Escape(RfcError(env, errorInfo).Value());
  }

  return scope.Escape(env.Null());
}

Napi::Value Function::IntToExternal(Napi::Env env, CHND container, const SAP_UC *name, Napi::Value value) {
  Napi::EscapableHandleScope scope{env};

  if (!value.IsNumber()) {
    auto err = "Argument has unexpected type: " + convertToString(env, name);
    return scope.Escape(Napi::TypeError::New(env, err).Value());
  }
  RFC_INT rfcValue = value.ToNumber().Int32Value();

  RfcSetInt(container, name, rfcValue, &errorInfo);
  LOG_API(env, this, "RfcSetInt");
  if (errorInfo.code != RFC_OK) {
    return scope.Escape(RfcError(env, errorInfo).Value());
  }

  return scope.Escape(env.Null());
}

Napi::Value Function::Int1ToExternal(Napi::Env env, CHND container, const SAP_UC *name, Napi::Value value) {
  Napi::EscapableHandleScope scope{env};

  if (!value.IsNumber()) {
    auto err = "Argument has unexpected type: " + convertToString(env, name);
    return scope.Escape(Napi::TypeError::New(env, err).Value());
  }
  int32_t convertedValue = value.ToNumber().Int32Value();
  if ((convertedValue < std::numeric_limits<int8_t>::min()) || (convertedValue > std::numeric_limits<int8_t>::max())) {
    auto err = "Argument out of range: " + convertToString(env, name);
    return scope.Escape(Napi::TypeError::New(env, err).Value());
  }
  RFC_INT1 rfcValue = convertedValue;

  RfcSetInt1(container, name, rfcValue, &errorInfo);
  LOG_API(env, this, "RfcSetInt1");
  if (errorInfo.code != RFC_OK) {
    return scope.Escape(RfcError(env, errorInfo).Value());
  }

  return scope.Escape(env.Null());
}

Napi::Value Function::Int2ToExternal(Napi::Env env, CHND container, const SAP_UC *name, Napi::Value value) {
  Napi::EscapableHandleScope scope{env};

  if (!value.IsNumber()) {
    auto err = "Argument has unexpected type: " + convertToString(env, name);
    return scope.Escape(Napi::TypeError::New(env, err).Value());
  }
  int32_t convertedValue = value.ToNumber().Int32Value();
  if ((convertedValue < std::numeric_limits<int16_t>::min()) ||
      (convertedValue > std::numeric_limits<int16_t>::max())) {
    auto err = "Argument out of range: " + convertToString(env, name);
    return scope.Escape(Napi::TypeError::New(env, err).Value());
  }
  RFC_INT2 rfcValue = convertedValue;

  RfcSetInt2(container, name, rfcValue, &errorInfo);
  LOG_API(env, this, "RfcSetInt2");
  if (errorInfo.code != RFC_OK) {
    return scope.Escape(RfcError(env, errorInfo).Value());
  }

  return scope.Escape(env.Null());
}

Napi::Value Function::FloatToExternal(Napi::Env env, CHND container, const SAP_UC *name, Napi::Value value) {
  Napi::EscapableHandleScope scope{env};

  if (!value.IsNumber()) {
    auto err = "Argument has unexpected type: " + convertToString(env, name);
    return scope.Escape(Napi::TypeError::New(env, err).Value());
  }
  RFC_FLOAT rfcValue = value.ToNumber().DoubleValue();

  RfcSetFloat(container, name, rfcValue, &errorInfo);
  LOG_API(env, this, "RfcSetFloat");
  if (errorInfo.code != RFC_OK) {
    return scope.Escape(RfcError(env, errorInfo).Value());
  }

  return scope.Escape(env.Null());
}

Napi::Value Function::DateToExternal(Napi::Env env, CHND container, const SAP_UC *name, Napi::Value value) {
  Napi::EscapableHandleScope scope{env};

  if (!value.IsString()) {
    auto err = "Argument has unexpected type: " + convertToString(env, name);
    return scope.Escape(Napi::TypeError::New(env, err).Value());
  }

  auto valueU16 = value.ToString().Utf16Value();
  if (valueU16.length() != 8) {
    auto err = "Invalid date format: " + convertToString(env, name);
    return scope.Escape(Napi::TypeError::New(env, err).Value());
  }

  RfcSetDate(container, name, (const RFC_CHAR *) valueU16.c_str(), &errorInfo);
  LOG_API(env, this, "RfcSetDate");
  if (errorInfo.code != RFC_OK) {
    return scope.Escape(RfcError(env, errorInfo).Value());
  }

  return scope.Escape(env.Null());
}

Napi::Value Function::TimeToExternal(Napi::Env env, CHND container, const SAP_UC *name, Napi::Value value) {
  Napi::EscapableHandleScope scope{env};

  if (!value.IsString()) {
    auto err = "Argument has unexpected type: " + convertToString(env, name);
    return scope.Escape(Napi::TypeError::New(env, err).Value());
  }

  auto valueU16 = value.ToString().Utf16Value();
  if (valueU16.length() != 6) {
    auto err = "Invalid time format: " + convertToString(env, name);
    return scope.Escape(Napi::TypeError::New(env, err).Value());
  }

  RfcSetTime(container, name, (const RFC_CHAR *) valueU16.c_str(), &errorInfo);
  LOG_API(env, this, "RfcSetTime");
  if (errorInfo.code != RFC_OK) {
    return scope.Escape(RfcError(env, errorInfo).Value());
  }

  return scope.Escape(env.Null());
}

Napi::Value Function::BCDToExternal(Napi::Env env, CHND container, const SAP_UC *name, Napi::Value value) {
  Napi::EscapableHandleScope scope{env};

  if (!value.IsNumber()) {
    auto err = "Argument has unexpected type: " + convertToString(env, name);
    return scope.Escape(Napi::TypeError::New(env, err).Value());
  }

  auto valueU16 = value.ToString().Utf16Value();

  RfcSetString(container, name, (const SAP_UC *) valueU16.data(), valueU16.length(), &errorInfo);
  LOG_API(env, this, "RfcSetString");
  if (errorInfo.code != RFC_OK) {
    return scope.Escape(RfcError(env, errorInfo).Value());
  }

  return scope.Escape(env.Null());
}


Napi::Value Function::GetValue(Napi::Env env, const CHND container, RFCTYPE type, const SAP_UC *name, unsigned len) {
  Napi::EscapableHandleScope scope{env};
  Napi::Value value{};

  switch (type) {
    case RFCTYPE_DATE:
      value = this->DateToInternal(env, container, name);
      break;
    case RFCTYPE_TIME:
      value = this->TimeToInternal(env, container, name);
      break;
    case RFCTYPE_NUM:
      value = this->NumToInternal(env, container, name, len);
      break;
    case RFCTYPE_BCD:
      value = this->BCDToInternal(env, container, name);
      break;
    case RFCTYPE_CHAR:
      value = this->CharToInternal(env, container, name, len);
      break;
    case RFCTYPE_BYTE:
      value = this->ByteToInternal(env, container, name, len);
      break;
    case RFCTYPE_FLOAT:
      value = this->FloatToInternal(env, container, name);
      break;
    case RFCTYPE_INT:
      value = this->IntToInternal(env, container, name);
      break;
    case RFCTYPE_INT1:
      value = this->Int1ToInternal(env, container, name);
      break;
    case RFCTYPE_INT2:
      value = this->Int2ToInternal(env, container, name);
      break;
    case RFCTYPE_STRUCTURE:
      value = this->StructureToInternal(env, container, name);
      break;
    case RFCTYPE_TABLE:
      value = this->TableToInternal(env, container, name);
      break;
    case RFCTYPE_STRING:
      value = this->StringToInternal(env, container, name);
      break;
    case RFCTYPE_XSTRING:
      value = this->XStringToInternal(env, container, name);
      break;
    default:
      // Type not implemented
      auto err = "RFC type not implemented: " + std::to_string(type);
      value = Napi::Error::New(env, err).Value();
      break;
  }

  return scope.Escape(value);
}

Napi::Value Function::StructureToInternal(Napi::Env env, CHND container, const SAP_UC *name) {
  Napi::EscapableHandleScope scope{env};
  RFC_STRUCTURE_HANDLE structHandle;

  RfcGetStructure(container, name, &structHandle, &errorInfo);
  LOG_API(env, this, "RfcGetStructure");
  if (errorInfo.code != RFC_OK) {
    return scope.Escape(RfcError(env, errorInfo).Value());
  }

  return scope.Escape(StructureToInternal(env, structHandle));
}

Napi::Value Function::StructureToInternal(Napi::Env env, RFC_STRUCTURE_HANDLE structHandle) {
  Napi::EscapableHandleScope scope{env};

  auto typeHandle = RfcDescribeType(structHandle, &errorInfo);
  LOG_API(env, this, "RfcDescribeType");
  assert(typeHandle);
  if (typeHandle == nullptr) {
    return scope.Escape(RfcError(env, errorInfo).Value());
  }

  unsigned fieldCount{};
  RfcGetFieldCount(typeHandle, &fieldCount, &errorInfo);
  LOG_API(env, this, "RfcGetFieldCount");
  if (errorInfo.code != RFC_OK) {
    return scope.Escape(RfcError(env, errorInfo).Value());
  }

  auto obj = Napi::Object::New(env);

  for (unsigned int i = 0; i < fieldCount; i++) {
    RFC_FIELD_DESC fieldDesc;
    RfcGetFieldDescByIndex(typeHandle, i, &fieldDesc, &errorInfo);
    LOG_API(env, this, "RfcGetFieldDescByIndex");
    if (errorInfo.code != RFC_OK) {
      return scope.Escape(RfcError(env, errorInfo).Value());
    }

    auto value = GetValue(env, structHandle, fieldDesc.type, fieldDesc.name, fieldDesc.nucLength);
    // Bail out on exception
    if (IsException(env, value)) {
      return scope.Escape(value);
    }
    obj.Set(Napi::String::New(env, (const char16_t *) (fieldDesc.name)), value);
  }

  return scope.Escape(obj);
}

Napi::Value Function::TableToInternal(Napi::Env env, CHND container, const SAP_UC *name) {
  Napi::EscapableHandleScope scope{env};

  RFC_TABLE_HANDLE tableHandle;
  RfcGetTable(container, name, &tableHandle, &errorInfo);
  LOG_API(env, this, "RfcGetTable");
  if (errorInfo.code != RFC_OK) {
    return scope.Escape(RfcError(env, errorInfo).Value());
  }

  unsigned rowCount{};
  RfcGetRowCount(tableHandle, &rowCount, &errorInfo);
  LOG_API(env, this, "RfcGetRowCount");
  if (errorInfo.code != RFC_OK) {
    return scope.Escape(RfcError(env, errorInfo).Value());
  }

  // Create array holding table lines
  auto obj = Napi::Array::New(env);

  for (unsigned int i = 0; i < rowCount; i++) {
    RfcMoveTo(tableHandle, i, &errorInfo);
    LOG_API(env, this, "RfcMoveTo");
    auto structHandle = RfcGetCurrentRow(tableHandle, &errorInfo);
    LOG_API(env, this, "RfcGetCurrentRow");

    auto line = StructureToInternal(env, structHandle);
    // Bail out on exception
    if (IsException(env, line)) {
      return scope.Escape(line);
    }
    obj.Set(i, line);
  }

  return scope.Escape(obj);
}

template<typename T>
static std::unique_ptr<T[]> getZeroedBuffer(unsigned len) {
  auto buffer = std::unique_ptr<T[]>(new T[len]);
  assert(buffer);
  memset(buffer.get(), 0, len * sizeof(T));
  return buffer;
}

Napi::Value Function::StringToInternal(Napi::Env env, CHND container, const SAP_UC *name) {
  Napi::EscapableHandleScope scope{env};

  unsigned strLen{};
  RfcGetStringLength(container, name, &strLen, &errorInfo);
  LOG_API(env, this, "RfcGetStringLength");
  if (errorInfo.code != RFC_OK) {
    return scope.Escape(RfcError(env, errorInfo).Value());
  }

  if (strLen == 0) {
    return scope.Escape(Napi::String::New(env, ""));
  }

  auto buffer = getZeroedBuffer<SAP_UC>(strLen + 1);

  unsigned retStrLen{};
  RfcGetString(container, name, buffer.get(), strLen + 1, &retStrLen, &errorInfo);
  LOG_API(env, this, "RfcGetString");
  if (errorInfo.code != RFC_OK) {
    return scope.Escape(RfcError(env, errorInfo).Value());
  }

  return scope.Escape(Napi::String::New(env, (const char16_t *) (buffer.get())));
}

Napi::Value Function::XStringToInternal(Napi::Env env, const CHND container, const SAP_UC *name) {
  Napi::EscapableHandleScope scope{env};

  unsigned strLen{};
  RfcGetStringLength(container, name, &strLen, &errorInfo);
  LOG_API(env, this, "RfcGetStringLength");
  if (errorInfo.code != RFC_OK) {
    return scope.Escape(RfcError(env, errorInfo).Value());
  }

  if (strLen == 0) {
    return scope.Escape(Napi::String::New(env, ""));
  }

  auto buffer = getZeroedBuffer<SAP_RAW>(strLen + 1);

  unsigned retStrLen{};
  RfcGetXString(container, name, buffer.get(), strLen, &retStrLen, &errorInfo);
  LOG_API(env, this, "RfcGetXString");
  if (errorInfo.code != RFC_OK) {
    return scope.Escape(RfcError(env, errorInfo).Value());
  }

  /* Buffer does not assume ownership by its own but requires data to be alive
   * for its lifetime. However with the finalizer, we can treat it as a smart pointer.
   */
  auto value = Napi::Buffer<SAP_RAW>::New(env, buffer.get(), strLen,
                                          [](Napi::Env env, SAP_RAW *buffer) {
                                            delete[] buffer;
                                          });
  buffer.release(); // let the Buffer take care of it from now on

  return scope.Escape(value);
}

Napi::Value Function::NumToInternal(Napi::Env env, CHND container, const SAP_UC *name, unsigned len) {
  Napi::EscapableHandleScope scope{env};

  auto buffer = getZeroedBuffer<RFC_NUM>(len + 1);

  RfcGetNum(container, name, buffer.get(), len, &errorInfo);
  LOG_API(env, this, "RfcGetNum");
  if (errorInfo.code != RFC_OK) {
    return scope.Escape(RfcError(env, errorInfo).Value());
  }

  return scope.Escape(Napi::String::New(env, (const char16_t *) (buffer.get())));
}

Napi::Value Function::CharToInternal(Napi::Env env, CHND container, const SAP_UC *name, unsigned len) {
  Napi::EscapableHandleScope scope{env};

  auto buffer = getZeroedBuffer<RFC_CHAR>(len + 1);

  RfcGetChars(container, name, buffer.get(), len, &errorInfo);
  LOG_API(env, this, "RfcGetChars");
  if (errorInfo.code != RFC_OK) {
    return scope.Escape(RfcError(env, errorInfo).Value());
  }

  return scope.Escape(Napi::String::New(env, (const char16_t *) (buffer.get())));
}

Napi::Value Function::ByteToInternal(Napi::Env env, CHND container, const SAP_UC *name, unsigned len) {
  Napi::EscapableHandleScope scope{env};

  auto buffer = getZeroedBuffer<RFC_BYTE>(len);

  RfcGetBytes(container, name, buffer.get(), len, &errorInfo);
  LOG_API(env, this, "RfcGetBytes");
  if (errorInfo.code != RFC_OK) {
    return scope.Escape(RfcError(env, errorInfo).Value());
  }

  /* Buffer does not assume ownership by its own but requires data to be alive
   * for its lifetime. However with the finalizer, we can treat it as a smart pointer.
   */
  auto value = Napi::Buffer<RFC_BYTE>::New(env, buffer.get(), len,
                                           [](Napi::Env env, RFC_BYTE *buffer) {
                                             delete[] buffer;
                                           });
  buffer.release(); // let the Buffer take care of it from now on

  return scope.Escape(value);
}

Napi::Value Function::IntToInternal(Napi::Env env, CHND container, const SAP_UC *name) {
  Napi::EscapableHandleScope scope{env};
  RFC_INT value;

  RfcGetInt(container, name, &value, &errorInfo);
  LOG_API(env, this, "RfcGetInt");
  if (errorInfo.code != RFC_OK) {
    return scope.Escape(RfcError(env, errorInfo).Value());
  }

  return scope.Escape(Napi::Number::New(env, value));
}

Napi::Value Function::Int1ToInternal(Napi::Env env, CHND container, const SAP_UC *name) {
  Napi::EscapableHandleScope scope{env};
  RFC_INT1 value;

  RfcGetInt1(container, name, &value, &errorInfo);
  LOG_API(env, this, "RfcGetInt1");
  if (errorInfo.code != RFC_OK) {
    return scope.Escape(RfcError(env, errorInfo).Value());
  }

  return scope.Escape(Napi::Number::New(env, value));
}

Napi::Value Function::Int2ToInternal(Napi::Env env, CHND container, const SAP_UC *name) {
  Napi::EscapableHandleScope scope{env};
  RFC_INT2 value;

  RfcGetInt2(container, name, &value, &errorInfo);
  LOG_API(env, this, "RfcGetInt2");
  if (errorInfo.code != RFC_OK) {
    return scope.Escape(RfcError(env, errorInfo).Value());
  }

  return scope.Escape(Napi::Number::New(env, value));
}

Napi::Value Function::FloatToInternal(Napi::Env env, CHND container, const SAP_UC *name) {
  Napi::EscapableHandleScope scope{env};
  RFC_FLOAT value;

  RfcGetFloat(container, name, &value, &errorInfo);
  LOG_API(env, this, "RfcGetFloat");
  if (errorInfo.code != RFC_OK) {
    return scope.Escape(RfcError(env, errorInfo).Value());
  }

  return scope.Escape(Napi::Number::New(env, value));
}

Napi::Value Function::DateToInternal(Napi::Env env, CHND container, const SAP_UC *name) {
  Napi::EscapableHandleScope scope{env};
  RFC_DATE date = {0};

  RfcGetDate(container, name, date, &errorInfo);
  LOG_API(env, this, "RfcGetDate");
  if (errorInfo.code != RFC_OK) {
    return scope.Escape(RfcError(env, errorInfo).Value());
  }
  return scope.Escape(Napi::String::New(env, (const char16_t *) (date), sizeof(RFC_DATE) / sizeof(RFC_CHAR)));
}

Napi::Value Function::TimeToInternal(Napi::Env env, CHND container, const SAP_UC *name) {
  Napi::EscapableHandleScope scope{env};
  RFC_TIME time = {0};

  RfcGetTime(container, name, time, &errorInfo);
  LOG_API(env, this, "RfcGetTime");
  if (errorInfo.code != RFC_OK) {
    return scope.Escape(RfcError(env, errorInfo).Value());
  }
  return scope.Escape(Napi::String::New(env, (const char16_t *) (time), sizeof(RFC_TIME) / sizeof(RFC_CHAR)));
}

Napi::Value Function::BCDToInternal(Napi::Env env, CHND container, const SAP_UC *name) {
  unsigned strLen = 25;
  do {
    Napi::EscapableHandleScope scope{env};
    unsigned retStrLen{};
    auto buffer = getZeroedBuffer<SAP_UC>(strLen + 1);

    RfcGetString(container, name, buffer.get(), strLen + 1, &retStrLen, &errorInfo);
    LOG_API(env, this, "RfcGetString");

    if (errorInfo.code == RFC_BUFFER_TOO_SMALL) {
      // Retry with suggested string length
      strLen = retStrLen;
      log(env, Levels::SILLY, "Function::BCDToInternal: Retry");
    } else if (errorInfo.code != RFC_OK) {
      return scope.Escape(RfcError(env, errorInfo).Value());
    } else {
      return scope.Escape(Napi::String::New(env, (const char16_t *) (buffer.get()), retStrLen).ToNumber());
    }
  } while (errorInfo.code == RFC_BUFFER_TOO_SMALL);

  // silence compiler warnings
  return env.Undefined();
}

std::string Function::mapExternalTypeToJavaScriptType(RFCTYPE sapType) {
  switch (sapType) {
    case RFCTYPE_CHAR:
    case RFCTYPE_DATE:
    case RFCTYPE_TIME:
    case RFCTYPE_BYTE:
    case RFCTYPE_NUM:
    case RFCTYPE_STRING:
    case RFCTYPE_XSTRING:
      return "string";
    case RFCTYPE_TABLE:
      return "array";
    case RFCTYPE_ABAPOBJECT:
    case RFCTYPE_STRUCTURE:
      return "object";
    case RFCTYPE_BCD:
    case RFCTYPE_FLOAT:
    case RFCTYPE_DECF16:
    case RFCTYPE_DECF34:
      return "number";
    case RFCTYPE_INT:
    case RFCTYPE_INT2:
    case RFCTYPE_INT1:
    case RFCTYPE_INT8:
    case RFCTYPE_UTCLONG:
    case RFCTYPE_UTCSECOND:
    case RFCTYPE_UTCMINUTE:
    case RFCTYPE_DTDAY:
    case RFCTYPE_DTWEEK:
    case RFCTYPE_DTMONTH:
    case RFCTYPE_TSECOND:
    case RFCTYPE_TMINUTE:
    case RFCTYPE_CDAY:
      return "integer";
    default:
      return "undefined";
  }

}

bool Function::addMetaData(Napi::Env env, CHND container, Napi::Object parent,
                           const RFC_ABAP_NAME name, RFCTYPE type, unsigned int length,
                           RFC_DIRECTION direction, RFC_PARAMETER_TEXT paramText,
                           bool refresh) {
  Napi::HandleScope scope{env};

  log(env, Levels::SILLY, "Function::addMetaData");

  auto actualType = Napi::Object::New(env);
  parent.Set(Napi::String::New(env, (const char16_t *) name), actualType);

  actualType.Set("type", Napi::String::New(env, mapExternalTypeToJavaScriptType(type)));

  std::stringstream lengthString;
  lengthString << length;
  actualType.Set("length", Napi::String::New(env, lengthString.str()));

  actualType.Set("sapType", Napi::String::New(env, (char16_t *) RfcGetTypeAsString(type)));

  if (paramText != nullptr) {
    actualType.Set("description", Napi::String::New(env, (char16_t *) paramText));
  }

  if (direction != 0) {
    actualType.Set("sapDirection", Napi::String::New(env, (char16_t *) RfcGetDirectionAsString(direction)));
  }

  if (type == RFCTYPE_STRUCTURE) {
    RFC_STRUCTURE_HANDLE structHandle;

    RfcGetStructure(container, name, &structHandle, &errorInfo);
    LOG_API(env, this, "RfcGetStructure");
    if (errorInfo.code != RFC_OK) {
      return false;
    }

    auto typeHandle = RfcDescribeType(structHandle, &errorInfo);
    LOG_API(env, this, "RfcDescribeType");
    assert(typeHandle);
    if (typeHandle == nullptr) {
      return false;
    }

    RFC_ABAP_NAME typeName;
    RfcGetTypeName(typeHandle, typeName, &errorInfo);
    LOG_API(env, this, "RfcGetTypeName");
    if (errorInfo.code != RFC_OK) {
      return false;
    }

    actualType.Set("sapTypeName", Napi::String::New(env, (char16_t *) typeName));

    if (refresh) {
      RFC_ATTRIBUTES connectionAttributes;
      RfcGetConnectionAttributes(connection->connectionHandle, &connectionAttributes, &errorInfo);
      LOG_API(env, this, "RfcGetConnectionAttributes");
      RfcRemoveTypeDesc(connectionAttributes.sysId, typeName, &errorInfo);
      LOG_API(env, this, "RfcRemoveTypeDesc");
      typeHandle = RfcDescribeType(structHandle, &errorInfo);
      LOG_API(env, this, "RfcDescribeType");
      assert(typeHandle);
      if (typeHandle == nullptr) {
        return false;
      }
    }

    unsigned fieldCount;
    RfcGetFieldCount(typeHandle, &fieldCount, &errorInfo);
    LOG_API(env, this, "RfcGetFieldCount");
    if (errorInfo.code != RFC_OK) {
      return false;
    }

    auto properties = Napi::Object::New(env);
    actualType.Set("properties", properties);

    for (unsigned int i = 0; i < fieldCount; i++) {
      RFC_FIELD_DESC fieldDesc;
      RfcGetFieldDescByIndex(typeHandle, i, &fieldDesc, &errorInfo);
      LOG_API(env, this, "RfcGetFieldDescByIndex");
      if (errorInfo.code != RFC_OK) {
        return false;
      }

      if (!addMetaData(env, structHandle, properties, fieldDesc.name, fieldDesc.type,
                       fieldDesc.nucLength, RFC_DIRECTION(0), nullptr, refresh)) {
        return false;
      }
    }
  } else if (type == RFCTYPE_TABLE) {
    RFC_TABLE_HANDLE tableHandle;
    RFC_TYPE_DESC_HANDLE typeHandle;
    RFC_FIELD_DESC fieldDesc;
    unsigned fieldCount;
    RFC_ABAP_NAME typeName;

    RfcGetTable(container, name, &tableHandle, &errorInfo);
    LOG_API(env, this, "RfcGetTable");
    if (errorInfo.code != RFC_OK) {
      return false;
    }

    typeHandle = RfcDescribeType(tableHandle, &errorInfo);
    LOG_API(env, this, "RfcDescribeType");
    assert(typeHandle);
    if (typeHandle == nullptr) {
      return false;
    }

    RfcGetTypeName(typeHandle, typeName, &errorInfo);
    LOG_API(env, this, "RfcGetTypeName");
    if (errorInfo.code != RFC_OK) {
      return false;
    }

    if (refresh) {
      RFC_ATTRIBUTES connectionAttributes;
      RfcGetConnectionAttributes(connection->connectionHandle, &connectionAttributes, &errorInfo);
      LOG_API(env, this, "RfcGetConnectionAttributes");
      RfcRemoveTypeDesc(connectionAttributes.sysId, typeName, &errorInfo);
      LOG_API(env, this, "RfcRemoveTypeDesc");
      typeHandle = RfcDescribeType(tableHandle, &errorInfo);
      LOG_API(env, this, "RfcDescribeType");
      assert(typeHandle);
      if (typeHandle == nullptr) {
        return false;
      }
    }

    RfcGetFieldCount(typeHandle, &fieldCount, &errorInfo);
    LOG_API(env, this, "RfcGetFieldCount");
    if (errorInfo.code != RFC_OK) {
      return false;
    }

    auto items = Napi::Object::New(env);
    actualType.Set("items", items);
    items.Set("sapTypeName", Napi::String::New(env, (char16_t *) typeName));

    items.Set("type", Napi::String::New(env, "object"));

    auto properties = Napi::Object::New(env);
    items.Set("properties", properties);

    RFC_STRUCTURE_HANDLE rowHandle = RfcAppendNewRow(tableHandle, &errorInfo);
    LOG_API(env, this, "RfcAppendNewRow");
    if (errorInfo.code != RFC_OK) {
      return false;
    }

    for (unsigned int i = 0; i < fieldCount; i++) {
      RfcGetFieldDescByIndex(typeHandle, i, &fieldDesc, &errorInfo);
      LOG_API(env, this, "RfcGetFieldDescByIndex");
      if (errorInfo.code != RFC_OK) {
        return false;
      }

      log(env, Levels::SILLY, "Function::addMetaData recurse");
      if (!addMetaData(env, rowHandle, properties, fieldDesc.name, fieldDesc.type,
                       fieldDesc.nucLength, RFC_DIRECTION(0), nullptr, refresh)) {
        return false;
      }
    }
  }

  return true;
}

void Function::addObjectInfoToLogMeta(Napi::Object meta) {
  if (connection) {
    connection->addObjectInfoToLogMeta(meta);
  } else {
    meta.Set("nativeConnection", "(null)");
  }
  char ptr[2 + sizeof(void *) * 2 + 1];
  snprintf(ptr, 2 + sizeof(void *) * 2 + 1, "%p", this);
  meta.Set("nativeFunction", ptr);
}
