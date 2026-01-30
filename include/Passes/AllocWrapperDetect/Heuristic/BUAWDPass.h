//
// Created on 2025/4/13.
//

#ifndef WRAPPERDETECT_BUAWDPASS_H
#define WRAPPERDETECT_BUAWDPASS_H

#include "Passes/AllocWrapperDetect/Heuristic/AWDPass.h"

// bottom up alloc wrapper detection pass, which does the same thing as AWDPass
// different is that this pass use a bottom-up style algorithm.
class BUAWDPass: public AWDPass {
public:
    map<Function*, set<CallBase*>> function2AllocCalls;

    BUAWDPass(GlobalContext* GCtx_): AWDPass(GCtx_) {
            ID = "bottom-up style alloc wrapper detection pass";
    }

    bool doInitialization(Module* M) override;

    bool doModulePass(Module* M) override;
};

#endif //WRAPPERDETECT_BUAWDPASS_H
