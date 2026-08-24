//===-- DRealBuilder.cpp ------------------------------------------*- C++ -*-====//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#include "klee/Core/JsonParser.h"
#include "klee/Expr/Expr.h"
#include "klee/Solver/SolverStats.h"

#include "llvm/ADT/StringExtras.h"

#include "GoSATBuilder.h"
#include <utility>
#include <cstring>
#include <iostream>

using namespace klee;

namespace klee {

enum goSATAlgorithm {
  kUndefinedAlg = 0,
  kCRS2   = NLOPT_GN_CRS2_LM,
  kISRES  = NLOPT_GN_ISRES,
  kMLSL   = NLOPT_G_MLSL,
  kDirect = NLOPT_GN_DIRECT_L,
//  kBYTEEA = NLOPT_GN_BYTEEA,
  kESCH   = NLOPT_GN_ESCH     //evolution computation
};


bool invokeGoSAT(const char *smtlibStr,
                 std::map<std::string,std::string> &varTypes,
                 const std::vector<const Array *> &objects,
                 std::vector<std::vector<unsigned char>> &values) {

  const char *substr = "concat";
  const char* found = strstr(smtlibStr, substr);
  if(found) return false;

  z3::context smt_ctx;
//  z3::expr smt_expr = smt_ctx.parse_string(smtlibStr);

//z3-4.13.0 support by yx
    z3::expr_vector smt_expr_vector = smt_ctx.parse_string(smtlibStr);
    z3::expr smt_expr = smt_expr_vector[0];
    if(smt_expr_vector.size() > 1){
        smt_expr = z3::mk_and(smt_expr_vector);
    }

  std::chrono::steady_clock::time_point time_start = std::chrono::steady_clock::now();
  using namespace llvm;
  std::string func_name ="invokeGoSAT";

  LLVMContext context;
  std::unique_ptr<Module> module = std::make_unique<Module>(StringRef(func_name), context);
  gosat::FPIRGenerator ir_gen(&context, module.get());
//  llvm::errs()<<"*********************="<<smt_expr.to_string()<<"\n";
  auto ll_func_ptr = ir_gen.genFunction(smt_expr);
  // add by yx=
//  llvm::errs()<<"======>func:\n";
//  ll_func_ptr->print(llvm::errs());

//  llvm::outs()<<"\n";
//  ll_func_ptr->dump();

//  llvm::outs()<<smt_expr.to_string()<<"\n";
//  llvm::outs()<<"func:\n";
//  ll_func_ptr->print(llvm::outs());
//  llvm::outs()<<"end\n";
//
//  llvm::outs()<<"ir_gen:\n";
//  ir_gen.getDistanceFunction()->print(llvm::outs());
//  llvm::outs()<<"end\n";

  std::string err_str;
  std::unique_ptr<ExecutionEngine> exec_engine(
          EngineBuilder(std::move(module))
                  .setEngineKind(EngineKind::JIT)
                  .setOptLevel(CodeGenOpt::Less)
                  .setErrorStr(&err_str)
                  .create());
  if (exec_engine == nullptr) {
    std::cerr << func_name << ": Failed to construct ExecutionEngine: "
              << err_str
              << "\n";
    return false;
  }

  ir_gen.addGlobalFunctionMappings(exec_engine.get());

  exec_engine->finalizeObject();
//  exec_engine->getPointerToFunction(ll_func_ptr)
  auto func_ptr = reinterpret_cast<nlopt_func>(exec_engine->getPointerToFunction(ll_func_ptr));

  // Now working with optimization backend
  goSATAlgorithm current_alg = kCRS2;
//  goSATAlgorithm current_alg = kBYTEEA;
  gosat::NLoptOptimizer nl_opt(static_cast<nlopt_algorithm>(current_alg));

  gosat::OptConfig Config(nl_opt.m_global_opt_alg, nl_opt.m_local_opt_alg);
//  nl_opt.Config.MaxEvalCount = 50000000;
//  nl_opt.Config.RelTolerance = 1e-20;
//  nl_opt.Config.MaxEvalCount = 5000000;
//  nl_opt.Config.RelTolerance = 1e-10;

  double minima = 1.0; /* minimum getValue */
  //model_vec相当于优化的决策变量向量,ir_gen.getVarCount决策变量维度  0.0 init is 0
  std::vector<double> model_vec(ir_gen.getVarCount(), 0.0);

  if (ir_gen.getVarCount() == 0) {
    // const function
    minima = (func_ptr)(0, nullptr, nullptr, nullptr);
  }
  else {
    nl_opt.optimize(func_ptr,//objective function
                    static_cast<unsigned>(model_vec.size()),//D
                    model_vec.data(),//X
                    &minima);//result
  }
  if (ir_gen.isFoundUnsupportedSMTExpr()) {
    std::cout<< "unsupported\n";
  }

  if (minima == 0) {
    for (const auto &array : objects) {
      std::string arrName = array->name;
      auto varsVec = ir_gen.getVars();
      for (unsigned idx = 0; idx < varsVec.size(); idx ++){
        std::string gosatVarName = varsVec[idx]->expr()->decl().name().str();
        if (!matchObjDeclVarName(arrName,gosatVarName,false))
          continue;
        double realRes = model_vec[idx];//这里的model_vec是gosat求解出的symblic直,double形式
        std::string varType = varTypes[arrName];

//        llvm::errs()<<"solution : "<<arrName<<"  type: "<<varType<<"  val : "<<realRes<<"\n";

        std::vector<unsigned char> data;
        data.reserve(array->size);
        //gosat求解出来的是double实数    得到double实数的bvfp,    push到values中
        getDataBytes(realRes,varType,data);
        values.push_back(data);
      }
    }
    return true;
  }
  return false;
}

}