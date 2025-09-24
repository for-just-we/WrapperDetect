//
// Created by prophe cheng on 2025/4/10.
//

#ifndef WRAPPERDETECT_FLTAPASS_H
#define WRAPPERDETECT_FLTAPASS_H

#include "Passes/CallGraph/CallGraphPass.h"

class FLTAPass: public CallGraphPass {
public:
    DenseMap<size_t, FuncSet> MatchedFuncsMap;
    DenseMap<size_t, FuncSet> MatchedICallTypeMap;

    FLTAPass(GlobalContext *Ctx_): CallGraphPass(Ctx_) {
        ID = "FLTA pass";
    }

    bool fuzzyTypeMatch(Type *Ty1, Type *Ty2, Module *M1, Module *M2);

    void findCalleesWithType(CallBase*, FuncSet&);

    void analyzeIndCall(CallBase* CI, FuncSet* FS) override;

    bool doInitialization(Module *M) override;
};

#endif //WRAPPERDETECT_FLTAPASS_H
