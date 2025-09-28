//
//

#ifndef WRAPPERDETECT_KMELDPASS_H
#define WRAPPERDETECT_KMELDPASS_H

#include <llvm/IR/Instructions.h>
#include <llvm/IR/BasicBlock.h>
#include "Passes/IterativeModulePass.h"

class KMeldPass: public IterativeModulePass {
public:
    set<Function*> AllocWrappers;

    KMeldPass(GlobalContext* GCtx_): IterativeModulePass(GCtx_) {
        ID = "base alloc wrapper detection pass";
    }

    bool doModulePass(Module* M) override;

    bool doFinalization(Module* M) override;

    // check F 1.return a pointer 2.return value not refer GetElementPtr and Argument
    bool backwardAnalysis(Function* F);

    // check whether a callsite of F: 1.null check, 2.initialization
    bool forwardAnalysis(CallBase* CB);

    bool isNullValue(Value *v);

    bool isNullComparison(ICmpInst *icmp, CallBase *targetCB);

    bool isInitializationCall(CallBase *call, Value *targetValue);

    bool checkInitializationInSuccessorBlocks(BranchInst *br, CallBase *targetCB);

    BasicBlock* getNonNullSuccessor(BranchInst *br, CallBase *targetCB);

    bool checkInitializationInBasicBlock(BasicBlock *bb, CallBase *targetCB);

    bool checkImmediateInitialization(Instruction *inst, CallBase *targetCB);
};

#endif //WRAPPERDETECT_KMELDPASS_H
