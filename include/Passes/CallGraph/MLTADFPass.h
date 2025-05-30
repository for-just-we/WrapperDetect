//
// Created by prophe cheng on 2025/4/10.
//

#ifndef WRAPPERDETECT_MLTADFPASS_H
#define WRAPPERDETECT_MLTADFPASS_H

#include "Passes/CallGraph/MLTAPass.h"
#include "Utils/Basic/Config.h"

class MLTADFPass: public MLTAPass {
private:
    set<Instruction*> nonEscapeStores;

public:
    MLTADFPass(GlobalContext *Ctx_): MLTAPass(Ctx_) {
        ID = "data flow enhanced MLTA pass";
    }

    void typeConfineInStore(StoreInst* SI) override;

    void escapeFuncPointer(Value* PO, Instruction* I) override;

    // resolve simple function pointer: v = f(a1, ...).
    // I: v = f(...), V: f, callees: potential targets, return value: whether this is simple indirect-call
    // The last argument is used to process recursive call
    virtual bool resolveSFP(Value* User, Value* V, set<Function*>& callees, set<Value*>& defUseSites,
                    set<Function*>& visitedFuncs);

    bool justifyUsers(Value* value, Value* curUser);
};

#endif //WRAPPERDETECT_MLTADFPASS_H
