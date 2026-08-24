//===-- DRealBuilder.cpp ------------------------------------------*- C++ -*-====//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#include "klee/Core/JsonParser.h"
//#include "JFSBuilder.h"
#include "llvm/Support/CommandLine.h"
#include "MathSAT5Builder.h"
using namespace klee;



namespace klee {

    void MathSAT5Builder::initSolver(){
      /* first, we create a configuration */
      msat_config cfg = msat_create_config();
      /* enable model generation */
      msat_set_option(cfg, "model_generation", "true");
//      msat_set_option(cfg, "interpolation", "true");
//      msat_set_option(cfg, "unsat_core_generation", "1");
//      msat_set_option(cfg, "proof_generation", "true");
//      msat_set_option(cfg, "preprocessor.toplevel_propagation", "false");
//      msat_set_option(cfg, "preprocessor.simplification", "0");
//      msat_set_option(cfg, "theory.bv.eager",  /* for BV, only the lazy */
//                      "false");                /* solver is proof-producing */
//      msat_set_option(cfg, "theory.fp.mode", "2");
      env = msat_create_env(cfg);
      assert(!MSAT_ERROR_ENV(env));

      msat_destroy_config(cfg);
    }

    void MathSAT5Builder::deleteSolver(){
//      msat_destroy_config(cfg);
      msat_destroy_env(env);
    }

    bool MathSAT5Builder::invokeMathSAT5GetResult(const std::string& smtlibStr,
                                                 std::map<std::string,std::string> &varTypes,
                                                 const std::vector<const Array *> &objects,
                                                 std::vector<std::vector<unsigned char>> &values){

      initSolver();

//      int group = msat_create_itp_group(env);
//      assert(group!=-1);
//      int res = msat_set_itp_group(env, group);
//      assert(res == 0);
      msat_term msatTerm = msat_from_smtlib2(env, smtlibStr.c_str());
      assert(MSAT_ERROR_TERM(msatTerm)==0);

      int flag = msat_assert_formula(env, msatTerm);
      assert(!flag);

      msat_result result = msat_solve(env);

      if (result==MSAT_SAT){
        /* display the model */
        std::map<std::string, std::string> model_map = get_model(env);
        for (const auto &array : objects) {
          std::string arrName = array->name;
//          auto varsVec = ir_gen.getVars();
          for (auto it=model_map.begin(); it!=model_map.end(); it++){
            std::string msatVarName = it->first;
            if (!matchObjDeclVarName(arrName,msatVarName,false))
              continue;
            std::string strRes = it->second;//这里的model_vec是gosat求解出的symblic直,double形式
            std::string varType = varTypes[arrName];

//            long realRes = std::stol(strRes);
//            int realRes = std::stoi(strRes, 0 ,2);
            llvm::outs()<<"solution : "<<arrName<<"  type: "<<varType<<" val : "<<strRes<<"\n";

            std::vector<unsigned char> data;
            data.reserve(array->size);
            //gosat求解出来的是double实数    得到double实数的bvfp,    push到values中
            getDataBytes(strRes,varType,data);
            for(auto it = data.begin(); it!=data.end(); it++){
              llvm::outs()<<(int)*it<<" ";
            }
            llvm::outs()<<"\n";
            values.push_back(data);
          }
        }
        deleteSolver();
        return true;
      }
      deleteSolver();
      return false;
    }


    std::map<std::string, std::string> MathSAT5Builder::get_model(msat_env env)
    {
      /* we use a model iterator to retrieve the model values for all the
       * variables, and the necessary function instantiations */
      msat_model_iterator iter = msat_create_model_iterator(env);
      assert(!MSAT_ERROR_MODEL_ITERATOR(iter));
      std::map<std::string, std::string> model_vec;
//      printf("Model:\n");
      while (msat_model_iterator_has_next(iter)) {
        msat_term t, v;
        char *str_t;
        msat_model_iterator_next(iter, &t, &v);
        str_t = msat_term_repr(t);
        assert(str_t);
//        printf(" %s = ", str_t);

        char *str_v;
        str_v = msat_term_repr(v);
        assert(str_v);
//        printf("%s\n", str_v);

        model_vec[str_t] = str_v;

        msat_free(str_t);
        msat_free(str_v);
      }
      msat_destroy_model_iterator(iter);
      return model_vec;
    }

}

