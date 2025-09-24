//
// Created by prophe cheng on 2025/4/9.
//

#ifndef WRAPPERDETECT_CALLGRAPHPASS_H
#define WRAPPERDETECT_CALLGRAPHPASS_H

#include "Passes/IterativeModulePass.h"

class CallGraphPass: public IterativeModulePass {
public:
    set<CallBase*> CallSet;
    set<CallBase*> ICallSet;
    set<CallBase*> VCallSet;
    set<CallBase*> MatchedICallSet;

    CallGraphPass(GlobalContext* Ctx_): IterativeModulePass(Ctx_) {
        ID = "base call graph pass";
    }

    // Run on each module before iterative pass.
    virtual bool doInitialization(Module* M) override;

    // Run on each module after iterative pass.
    virtual bool doFinalization(Module* M) override;

    // Iterative pass.
    virtual bool doModulePass(Module* M) override;

    virtual void analyzeIndCall(CallBase* callInst, FuncSet* FS) {}

    virtual void analyzeVirtualCall(CallBase* callInst, FuncSet* FS) {}

    void unrollLoops(Function* F);

    bool isVirtualCall(CallBase* CI);

    bool isVirtualFunction(Function* F);
};

#endif //WRAPPERDETECT_CALLGRAPHPASS_H
