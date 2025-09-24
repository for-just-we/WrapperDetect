//
// Created by prophe cheng on 2025/5/31.
//

#ifndef WRAPPERDETECT_DEBUGPASS_H
#define WRAPPERDETECT_DEBUGPASS_H

#include <llvm/IR/InstIterator.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/DebugInfoMetadata.h>


#include "Passes/AllocWrapperDetect/Heuristic/EHAWDPass.h"

class DebugPass: public EHAWDPass {
public:
    set<const Function*> preAnalyzedWrappers;

    DebugPass(GlobalContext* GCtx_, set<const Function*> _preAnalyzed): EHAWDPass(GCtx_) {
        ID = "debug alloc wrapper detection pass";
        preAnalyzedWrappers.insert(_preAnalyzed.begin(), _preAnalyzed.end());
    }

    bool doInitialization(Module* M) override;

    bool doModulePass(Module* M) override;
};

#endif //WRAPPERDETECT_DEBUGPASS_H
