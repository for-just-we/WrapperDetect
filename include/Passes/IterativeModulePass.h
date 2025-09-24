//
// Created by prophe cheng on 2025/4/9.
//

#ifndef WRAPPERDETECT_ITERATIVEMODULEPASS_H
#define WRAPPERDETECT_ITERATIVEMODULEPASS_H

#include <llvm/IR/Module.h>
#include "Utils/Tool/Common.h"
#include "Utils/Basic/TypeDecls.h"
#include "Utils/Tool/GlobalContext.h"

using namespace llvm;

// 主要关注3个函数，doInitialization、doFinalization、doModulePass
// doInitialization完成type-hierachy分析以及type-propagation
// doFinalization将结果dump出
// doModulePass进行间接调用分析
class IterativeModulePass {
public:
    unsigned MIdx;
    string ID;
    GlobalContext* Ctx;
    // General pointer types like char * and void *
    map<Module*, Type*> Int8PtrTy;
    // long interger type
    map<Module*, Type*> IntPtrTy;
    map<Module*, const DataLayout*> DLMap;

    IterativeModulePass(GlobalContext* Ctx_): Ctx(Ctx_), MIdx(0) {
        ID = "base iterative pass";
    }

    virtual ~IterativeModulePass() = default;

    // Run on each module before iterative pass.
    virtual bool doInitialization(Module* M) {
        ++MIdx;
        return false;
    }

    // Run on each module after iterative pass.
    virtual bool doFinalization(Module* M) {
        ++MIdx;
        return false;
    }

    // Iterative pass.
    virtual bool doModulePass(Module* M) {
        ++MIdx;
        return false;
    }

    void run(ModuleList &modules);
};

#endif //WRAPPERDETECT_ITERATIVEMODULEPASS_H
