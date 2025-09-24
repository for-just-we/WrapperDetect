//
// Created by prophe cheng on 2025/5/23.
//

#ifndef WRAPPERDETECT_EHAWDPASS_H
#define WRAPPERDETECT_EHAWDPASS_H

// enhanced-HAWD pass
#include "Passes/AllocWrapperDetect/Heuristic/HAWDPass.h"

typedef enum SideEffectType { Store, Call, SysCall } SideEffectType;

// LLM-enhanced allocation wrapper detection pass
class EHAWDPass: public HAWDPass {
public:
    map<Function*, set<pair<Instruction*, SideEffectType>>> func2SideEffectOps;

    EHAWDPass(GlobalContext* GCtx_): HAWDPass(GCtx_) {
        ID = "simple alloc wrapper detection pass version2";
    }

    void identifySideEffectFunctions() override;

    bool doModulePass(Module* M) override;

    bool checkSimpleAlloc(Function* F, bool &simpleRet, set<CallBase*>& potentialAllocs) override;
};

#endif //WRAPPERDETECT_EHAWDPASS_H
