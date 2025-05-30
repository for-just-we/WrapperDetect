//
// Created by prophe cheng on 2025/4/10.
//

#ifndef WRAPPERDETECT_KELPPASS_H
#define WRAPPERDETECT_KELPPASS_H

#include "Passes/CallGraph/MLTADFPass.h"

class KELPPass: public MLTADFPass {
private:
    set<CallInst*> simpleIndCalls;
    set<Function*> confinedAddrTakenFuncs;
    map<GlobalVariable*, set<Function*>> confinedGlobs2Funcs;
    map<string, int> sysAPIs = {
            {"pthread_create", 2},
            {"thread", 0},
            {"signal", 1},
            {"qsort", 2},
            {"bsearch", 2},
    };

public:
    KELPPass(GlobalContext *Ctx_): MLTADFPass(Ctx_) {
        ID = "KELP pass";
    }

    bool doInitialization(Module*) override;

    bool doFinalization(Module *M) override;

    void analyzeIndCall(CallInst* CI, FuncSet* FS) override;

    bool forwardAnalyze(Value* V);

    bool resolveSFP(Value* User, Value* V, set<Function*>& callees, set<Value*>& defUseSites,
                    set<Function*>& visitedFuncs) override;
};


#endif //WRAPPERDETECT_KELPPASS_H
