//
// Created by prophe cheng on 2025/4/13.
//
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/Instructions.h>

#include "Passes/AllocWrapperDetect/Heuristic/BUAWDPass.h"
#include "Utils/Basic/Tarjan.h"

bool BUAWDPass::doInitialization(Module* M) {
    // collect internal defined function names
    for (Function &F : *M) {
        if (F.isDeclaration())
            continue;
        if (allocFuncsNames.count(F.getName().str()))
            allocFuncsNames.erase(F.getName().str());
    }

    // collect malloc sources
    for (auto &f : *M) {
        Function *F = &f;
        if (F->isDeclaration())
            continue;
        for (inst_iterator i = inst_begin(F), e = inst_end(F); i != e; ++i) {
            // if call to malloc
            if (CallBase* CI = dyn_cast<CallBase>(&*i)) {
                Function* CF = CommonUtil::getBaseFunction(CI->getCalledOperand());
                if (CF && allocFuncsNames.count(CF->getName().str())) {
                    function2AllocCalls[F].insert(CI);
                    continue;
                }

                // indirect-call
                if (CI->isIndirectCall()) {
                    for (Function* callee: Ctx->Callees[CI]) {
                        if (allocFuncsNames.count(callee->getName().str())) {
                            function2AllocCalls[F].insert(CI);
                            break;
                        }
                    }
                }
            }
        }
    }
    return false;
}

bool BUAWDPass::doModulePass(Module* M) {
    Tarjan tarjan(Ctx->CallMaps);
    tarjan.getSCC(Ctx->SCC);

    for (vector<Function*> sc: Ctx->SCC) {
        bool changed;
        do {
            changed = false;
            for (Function* F: sc) {
                // potential alloc wrapper function
                if (function2AllocCalls.count(F)) {
                    for (CallBase* curCI: function2AllocCalls[F]) {
                        set<Value*> visitedValues;
                        if (traceValueFlow(curCI, visitedValues)) {
                            if (!callInWrappers[F].count(curCI)) {
                                callInWrappers[F].insert(curCI);
                                changed = true;
                            }
                            if (!AllocWrappers.count(F)) {
                                AllocWrappers.insert(F);
                                changed = true;
                                for (CallBase* caller: Ctx->Callers[F])
                                    function2AllocCalls[caller->getFunction()].insert(caller);
                            }
                        }
                    }
                }
            }
        } while (changed);
    }
    return false;
}

