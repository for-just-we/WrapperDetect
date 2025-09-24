//
// Created by prophe cheng on 2025/4/15.
//

#ifndef WRAPPERDETECT_HAWDPASS_H
#define WRAPPERDETECT_HAWDPASS_H

#include <vector>
#include <queue>
#include "Passes/AllocWrapperDetect/Heuristic/BUAWDPass.h"

// heuristic simple alloc wrapper detection
// ToDo: API side effect: calling free and simple alloc not to be returned;
class HAWDPass: public BUAWDPass {
private:
    // side effect functions could have part complex alloc calls
    set<Function*> sideEffectFuncs;

public:
    set<string> sensiAPI = {"pthread_mutex_init"};

    // 辅助变量
    set<Function*> visiting;

    explicit HAWDPass(GlobalContext* GCtx_): BUAWDPass(GCtx_) {
        ID = "heuristic simple alloc wrapper detection pass";
    }

    bool doInitialization(Module* M) override;

    bool doModulePass(Module* M) override;

    virtual void identifySideEffectFunctions();

    // analyze whether return value could flow from argument or global variable
    bool analyzeReturn(ReturnInst* RI, set<CallBase*>& visitAllocCalls);

    static bool analyzeStore(StoreInst* SI);

    static bool analyzeCopyArg(Value* dstV);

    static bool isComplexType(Type* Ty) {
        return Ty->isPointerTy() || Ty->isStructTy() || Ty->isArrayTy() || Ty->isVectorTy();
    }

    void promoteToCaller(Function* F, set<CallBase*>& visitedAllocCalls,
                         queue<Function*>& worklist, set<Function*>& inWorklist);

    void checkWhetherAlloc(Function* F, bool& isAlloc, bool& everyAllocReturned, set<CallBase*>& visitedAllocCalls);

    void processPotentialAllocs(Function* F, set<CallBase*>& potentialAllocs);

    virtual bool checkSimpleAlloc(Function* F, bool &simpleRet, set<CallBase*>& potentialAllocs);
};

#endif //WRAPPERDETECT_HAWDPASS_H
