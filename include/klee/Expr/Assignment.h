//===-- Assignment.h --------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_ASSIGNMENT_H
#define KLEE_ASSIGNMENT_H

#include "klee/Expr/Constraints.h"
#include "klee/Expr/ExprEvaluator.h"

#include <map>

namespace klee {
  class Array;

  class Assignment {
  public:
    typedef std::map<const Array*, std::vector<unsigned char> > bindings_ty;

    bool allowFreeValues;
    bindings_ty bindings;
    
  public:
    Assignment(bool _allowFreeValues=false)
      : allowFreeValues(_allowFreeValues) {}
    Assignment(const std::vector<const Array*> &objects,
               std::vector< std::vector<unsigned char> > &values,
               bool _allowFreeValues=false) 
      : allowFreeValues(_allowFreeValues){
      assert(objects.size() == values.size());
      std::vector< std::vector<unsigned char> >::iterator valIt = values.begin();
      for (std::vector<const Array*>::const_iterator it = objects.begin(),
             ie = objects.end(); it != ie; ++it) {
        const Array *os = *it;
        std::vector<unsigned char> &arr = *valIt;
        bindings.insert(std::make_pair(os, arr));
        ++valIt;
      }
    }

    
    ref<Expr> evaluate(const Array *mo, unsigned index) const;
    ref<Expr> evaluate(ref<Expr> e);
    ConstraintSet createConstraintsFromAssignment() const;

    // add by zgf : reuse old seed, but update new symbolic values
    void updateValues(const Assignment &newAssign){
      for (auto const &newBinding : newAssign.bindings){
        bool updateFlag = false;
        for (auto & binding : bindings){
          if (binding.first->name == newBinding.first->name){
            binding.second = newBinding.second;
            updateFlag = true;
            break;
          }
        }
        //assert(updateFlag && "symbol name must be matched.");
        if (!updateFlag){
          bindings.insert(newBinding);
        }
      }
    }

    template<typename InputIterator>
    bool satisfies(InputIterator begin, InputIterator end);
    void dump();
  };
  
  class AssignmentEvaluator : public ExprEvaluator {
    const Assignment &a;
  protected:
    ref<Expr> getInitialValue(const Array &mo, unsigned index) {
      return a.evaluate(&mo, index);
    }
  public:
    AssignmentEvaluator(const Assignment &_a) : a(_a) {}    
  };

  // add by zgf : to evaluate 'SFC'
  class SFCAssignmentEvaluator : public SFCExprEvaluator {
    const Assignment &a;
  protected:
    ref<Expr> getInitialValue(const Array &mo, unsigned index) {
      return a.evaluate(&mo, index);
    }
  public:
    SFCAssignmentEvaluator(const Assignment &_a) : a(_a) {}

  };

  /***/
  inline ref<Expr> Assignment::evaluate(const Array *array, 
                                        unsigned index) const {
    assert(array);
    auto it = bindings.find(array);
    if (it!=bindings.end() && index<it->second.size()) {
      return ConstantExpr::alloc(it->second[index], array->getRange());
    } else {
      if (allowFreeValues) {
        return ReadExpr::create(UpdateList(array, ref<UpdateNode>(nullptr)),
                                ConstantExpr::alloc(index, array->getDomain()));
      } else {
        return ConstantExpr::alloc(0, array->getRange());
      }
    }
  }

  inline ref<Expr> Assignment::evaluate(ref<Expr> e) {
    AssignmentEvaluator v(*this);
    return v.visit(e);
  }

  template<typename InputIterator>
  inline bool Assignment::satisfies(InputIterator begin, InputIterator end) {
    AssignmentEvaluator v(*this);
    for (; begin!=end; ++begin)
      if (!v.visit(*begin)->isTrue())
        return false;
    return true;
  }
}

#endif /* KLEE_ASSIGNMENT_H */
