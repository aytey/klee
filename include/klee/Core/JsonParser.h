//===-- UserSearcher.h ------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_JSONPARSER_H
#define KLEE_JSONPARSER_H

#include <string>
#include <utility>
#include <vector>

namespace klee {
  class ParameterTypeInfo{
  public:
    std::string type_name;
    unsigned type_size;
    bool is_float;

    ParameterTypeInfo(std::string name,int size,bool is_float):
      type_name(std::move(name)),type_size(size),is_float(is_float){}
  };

  class FunctionTypeInfo{
  public:
    std::string func_name;
    ParameterTypeInfo ret_type;
    std::vector<ParameterTypeInfo> args_type;
    std::string unique_name;
    bool is_float;

    FunctionTypeInfo(const std::string& name,
                     const ParameterTypeInfo& retType,
                     const std::vector<ParameterTypeInfo>& argsType):
        func_name(name),ret_type(retType),
        args_type(argsType){
      // compute unique_name :
      // name_r64_64_32 -> retBit=64, (arg0Bit=64,arg1Bit=32)
      //unique_name = name + "_r" + std::to_string(retType.type_size);
      unique_name = name; // don't care retValue width
      for (auto & arg : argsType)
        unique_name += "_" + std::to_string(arg.type_size);

      is_float = retType.is_float;
    }
  };
  bool matchObjDeclVarName(const std::string &objName,
                           const std::string &declName,
                           bool isArray);
  void getDataBytes(double drealVal,
                    const std::string& varType,
                    std::vector<unsigned char> &data);

  void getDataBytes(std::string strVal,
                      const std::string& varType,
                      std::vector<unsigned char> &data);

  int getSizeFromType(const std::string& typeName);

  void parseJsonConfig(std::vector<std::string> &linkLibs,
                       std::vector<std::string> &noMainObj,
                       std::vector<FunctionTypeInfo> &funcType);

  void buildFloatCFType(
      const std::vector<std::string> &basicComplexFuncSet,
      std::vector<FunctionTypeInfo> &funcType);

  void buildFPCheckType(std::vector<FunctionTypeInfo> &funcType);
}

#endif /* KLEE_JSONPARSER_H */
