//
// Created by prophe cheng on 2025/4/11.
//
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Operator.h>
#include <llvm/IR/DebugInfoMetadata.h>
#include <queue>

#include "Passes/AllocWrapperDetect/Heuristic/AWDPass.h"

bool AWDPass::doInitialization(Module* M) {
    // collect internal defined function names
    for (Function &F : *M) {
        if (F.isDeclaration())
            continue;
        if (allocFuncsNames.count(F.getName().str()))
            allocFuncsNames.erase(F.getName().str());
    }

    OP << "alloc function include:\n";
    for (const string& funcName: allocFuncsNames)
        OP << funcName << " , ";
    OP << "\n";

    // collect malloc sources
    for (Module::iterator f = M->begin(), fe = M->end(); f != fe; ++f) {
        Function *F = &*f;
        if (F->isDeclaration())
            continue;
        for (inst_iterator i = inst_begin(F), e = inst_end(F); i != e; ++i) {
            // if call to malloc
            if (CallBase* CI = dyn_cast<CallBase>(&*i)) {
                Function* CF = CommonUtil::getBaseFunction(CI->getCalledOperand());
                if (CF && allocFuncsNames.count(CF->getName().str())) {
                    baseAllocCalls.insert(CI);
                    continue;
                }
                // indirect-call
                if (CI->isIndirectCall()) {
                    for (Function* callee: Ctx->Callees[CI]) {
                        if (allocFuncsNames.count(callee->getName().str())) {
                            baseAllocCalls.insert(CI);
                            break;
                        }
                    }
                }
            }
        }
    }

    return false;
}

// 只要发现了新的wrapper，就返回true
bool AWDPass::doModulePass(Module* M) {
    set<CallBase*> visitedCallInsts;
    for (CallBase* baseAllocCI: baseAllocCalls) {
        // 对于每个alloc call，检查其是否流入return value
        queue<CallBase*> worklist;
        worklist.push(baseAllocCI);
        while (!worklist.empty()) {
            CallBase* curCI = worklist.front();
            Function* curF = curCI->getFunction();
            worklist.pop();
            if (visitedCallInsts.count(curCI))
                continue;
            visitedCallInsts.insert(curCI);
            set<Value*> visitedValues;
            // current alloc call could be returned
            if (traceValueFlow(curCI, visitedValues)) {
                callInWrappers[curF].insert(curCI);
                // it has not been processed yet
                if (!AllocWrappers.count(curF)) {
                    AllocWrappers.insert(curF);
                    // check whether the caller could be returned in that function
                    for (CallBase* caller: Ctx->Callers[curF])
                        worklist.push(caller);
                }
            }
        }
    }
    return false;
}

bool AWDPass::doFinalization(Module* M) {
    Ctx->AllocWrappers.merge(AllocWrappers);
    Ctx->callInWrappers.merge(callInWrappers);

    for (Function* F: Ctx->AllocWrappers) {
        // getNormalizedPath(F->getSubprogram())
        string funcKey = removeFuncNumberSuffix(F->getName().str()) + "<" +
                F->getSubprogram()->getFilename().str() + "<" +
                itostr(F->getSubprogram()->getLine());
        Ctx->AllocWrapperKeys.insert(funcKey);
    }
    return false;
}

// does not trace along indirect value flow, consider flow to
bool AWDPass::traceValueFlow(Value* V, set<Value*>& visitedValues) {
    if (visitedValues.count(V))
        return false;
    visitedValues.insert(V);
    for (User* U: V->users()) {
        // if (p = malloc()) --> return p
        if (isa<ReturnInst>(U))
            return true;

        // only need one user flow to return
        // if "p1 = malloc(), ... p = phi(p1, p2), ... return p", still return true
        // phi可能会出现环路，导致递归
        else if (isa<BitCastInst>(U) || isa<PtrToIntInst>(U) || isa<IntToPtrInst>(U)
                || isa<BitCastOperator>(U) || isa<PtrToIntOperator>(U) || isa<PHINode>(U)) {
            if (traceValueFlow(U, visitedValues))
                return true;
        }

        else if (CallBase* CI = dyn_cast<CallBase>(U)) {
            Function* CF = CommonUtil::getBaseFunction(CI->getCalledOperand());
            if (CF) {
                // p = memcpy(p1, ...) 与p = p1等效
                if (copyAPI.count(CF->getName().str())) {
                    if (CI->getNumOperands() > 2 && CI->getArgOperand(0) == V)
                        if (traceValueFlow(CI, visitedValues))
                            return true;
                }
                // p = func(p1, ...), p1能够reach返回值，有效
                else if (!CF->isDeclaration() && !CF->getReturnType()->isVoidTy()) {
                    // 计算参数索引
                    unsigned argNo = -1;
                    for (unsigned i = 0; i < CI->getNumOperands() - 1; ++i)
                        if (CI->getArgOperand(i) == V) {
                            argNo = i;
                            break;
                        }

                    // 考虑到可变参数函数的存在
                    if (argNo != -1 && argNo < CF->arg_size() && checkFuncArgRet(CF, argNo))
                        return true;
                }
            }
        }
    }
    return false;
}

bool AWDPass::checkFuncArgRet(Function* F, unsigned argNo) {
    Argument* arg = F->getArg(argNo);
    for (inst_iterator i = inst_begin(F), e = inst_end(F); i != e; ++i) {
        Instruction *I = &*i;
        if (ReturnInst* retInst = dyn_cast<ReturnInst>(I)) {
            if (retInst->getReturnValue() != arg)
                return false;
        }
    }
    func2args[F].insert(argNo);
    return true;
}