//
//
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Operator.h>
#include <llvm/IR/IntrinsicInst.h>
#include <queue>

#include "Passes/AllocWrapperDetect/Heuristic/KMeldPass.h"

using namespace std;

bool KMeldPass::doModulePass(Module* M) {
    for (Function& F: *M) {
        if (!backwardAnalysis(&F))
            continue;
        // potentially a allocation function
        bool flag = true;
        for (CallBase* CB: Ctx->Callers[&F]) {
            // no null check or initialization
            if (!forwardAnalysis(CB)) {
                flag = false;
                break;
            }
        }

        if (flag)
            AllocWrappers.insert(&F);
    }
    return false;
}

// check F 1.return a pointer 2.return value not refer GetElementPtr and Argument
bool KMeldPass::backwardAnalysis(Function* F) {
    // return type not pointer
    if (!F->getReturnType()->isPointerTy())
        return false;
    for (const BasicBlock& BB: *F) {
        for (const Instruction& I: BB) {
            // this return value is invalid
            if (const ReturnInst* RI = dyn_cast<ReturnInst>(&I)) {
                Value* retV = RI->getReturnValue();
                queue<Value*> q;
                q.push(retV);
                bool flag = true;
                while (q.empty()) {
                    Value* cur = q.front();
                    q.pop();
                    // The type of Copy coule be: bitcast, ptrtoint, inttoptr
                    if (isa<BitCastInst>(cur) || isa<PtrToIntInst>(cur) || isa<IntToPtrInst>(cur)) {
                        Instruction* VI = dyn_cast<Instruction>(cur);
                        q.push(VI->getOperand(0));
                    }
                        // nested cast could appear in instructions:
                        // for example: %fadd.1 = phi i64 (i32, i32)* [ bitcast (i32 (i32, i32)* @add1 to i64 (i32, i32)*), %if.then ], [ %fadd.0, %if.end ]
                    else if (isa<BitCastOperator>(cur) || isa<PtrToIntOperator>(cur)) {
                        Operator* O = dyn_cast<Operator>(cur);
                        q.push(O->getOperand(0));
                    }

                    else if (PHINode* PN = dyn_cast<PHINode>(cur)) {
                        for (unsigned i = 0; i < PN->getNumIncomingValues(); ++i) {
                            Value* IV = PN->getIncomingValue(i);
                            q.push(IV);
                        }
                    }

                    else if (isa<GetElementPtrInst>(cur) || isa<Argument>(cur)) {
                        flag = false;
                        break;
                    }
                }

                if (!flag)
                    return false;
            }
        }
    }
    return true;
}


// check whether a callsite of F: 1.null check, 2.initialization
bool KMeldPass::forwardAnalysis(CallBase* CB) {
    bool hasNullCheck = false;
    bool hasInitialization = false;

    BasicBlock::iterator it(CB);
    ++it;

    if (it != CB->getParent()->end()) {
        Instruction* nextInst = &*it;
        if (auto* icmp = dyn_cast<ICmpInst>(nextInst)) {
            if (isNullComparison(icmp, CB)) {
                hasNullCheck = true;
                ++it;
                if (it != CB->getParent()->end()) {
                    Instruction* afterIcmp = &*it;
                    BranchInst* nullCheckBr = dyn_cast<BranchInst>(afterIcmp);

                    if (nullCheckBr && nullCheckBr->isConditional())
                        hasInitialization = checkInitializationInSuccessorBlocks(nullCheckBr, CB);
                    else
                        hasInitialization = checkImmediateInitialization(afterIcmp, CB);
                }
            }
        }
    }

    if (!hasNullCheck)
        return false;

    if (!hasInitialization)
        return false;

    return true;
}


bool KMeldPass::doFinalization(Module* M) {
    Ctx->AllocWrappers.merge(AllocWrappers);

    for (Function* F: Ctx->AllocWrappers) {
        // getNormalizedPath(F->getSubprogram())
        string funcKey = removeFuncNumberSuffix(F->getName().str()) + "<" +
                         F->getSubprogram()->getFilename().str() + "<" +
                         itostr(F->getSubprogram()->getLine());
        Ctx->AllocWrapperKeys.insert(funcKey);
    }
    return false;
}

bool KMeldPass::isNullValue(Value *v) {
    if (isa<ConstantPointerNull>(v))
        return true;
    // 检查常量0
    if (auto *ci = dyn_cast<ConstantInt>(v))
        return ci->isZero();

    return false;
}

bool KMeldPass::isNullComparison(ICmpInst *icmp, CallBase *targetCB) {
    if (!icmp || !icmp->isEquality())
        return false;

    Value* op0 = icmp->getOperand(0);
    Value* op1 = icmp->getOperand(1);

    // 检查是否与null比较，且其中一个操作数是targetCB
    if ((isNullValue(op0) && op1 == targetCB) ||
        (isNullValue(op1) && op0 == targetCB))
        return true;

    return false;
}


// check initialization（memcpy/memset）
bool KMeldPass::isInitializationCall(CallBase *call, Value *targetValue) {
    if (!call)
        return false;

    Function *calledFunc = call->getCalledFunction();
    if (!calledFunc)
        return false;

    StringRef funcName = calledFunc->getName();

    if (funcName.startswith("memcpy") || funcName.startswith("llvm.memcpy"))
        return call->getArgOperand(0) == targetValue;

    if (funcName.startswith("memset") || funcName.startswith("llvm.memset"))
        return call->getArgOperand(0) == targetValue;

    if (auto *intrinsic = dyn_cast<IntrinsicInst>(call)) {
        Intrinsic::ID id = intrinsic->getIntrinsicID();
        if (id == Intrinsic::memcpy || id == Intrinsic::memset)
            return intrinsic->getArgOperand(0) == targetValue;

    }

    return false;
}


bool KMeldPass::checkInitializationInSuccessorBlocks(BranchInst *br, CallBase *targetCB) {
    if (!br || !br->isConditional())
        return false;

    // check null-check condition
    BasicBlock *nonNullSucc = getNonNullSuccessor(br, targetCB);
    if (!nonNullSucc)
        return false;

    return checkInitializationInBasicBlock(nonNullSucc, targetCB);
}

BasicBlock* KMeldPass::getNonNullSuccessor(BranchInst *br, CallBase *targetCB) {
    if (!br->isConditional()) return nullptr;

    ICmpInst *icmp = dyn_cast<ICmpInst>(br->getCondition());
    if (!icmp || !icmp->isEquality()) return nullptr;

    Value *op0 = icmp->getOperand(0);
    Value *op1 = icmp->getOperand(1);

    bool isNotNullCheck = false;
    BasicBlock *nonNullSucc = nullptr;

    // icmp eq/ne targetCB, null
    if (op0 == targetCB && isNullValue(op1)) {
        isNotNullCheck = (icmp->getPredicate() == CmpInst::ICMP_NE);
        nonNullSucc = isNotNullCheck ? br->getSuccessor(0) : br->getSuccessor(1);
    }
    // icmp eq/ne null, targetCB
    else if (op1 == targetCB && isNullValue(op0)) {
        isNotNullCheck = (icmp->getPredicate() == CmpInst::ICMP_NE);
        nonNullSucc = isNotNullCheck ? br->getSuccessor(0) : br->getSuccessor(1);
    }

    return nonNullSucc;
}

bool KMeldPass::checkInitializationInBasicBlock(BasicBlock *bb, CallBase *targetCB) {
    if (!bb)
        return false;

    for (auto &inst : *bb) {
        if (isa<PHINode>(&inst)) continue;

        if (auto *store = dyn_cast<StoreInst>(&inst)) {
            if (store->getValueOperand() == targetCB) {
                return true;
            }
        }

        if (auto *call = dyn_cast<CallBase>(&inst)) {
            if (isInitializationCall(call, targetCB)) {
                return true;
            }
        }

        if (auto *bitcast = dyn_cast<BitCastInst>(&inst)) {
            if (bitcast->getOperand(0) == targetCB) {
                for (auto *user : bitcast->users()) {
                    if (auto *store = dyn_cast<StoreInst>(user)) {
                        if (store->getValueOperand() == bitcast) {
                            return true;
                        }
                    }
                    if (auto *userCall = dyn_cast<CallBase>(user)) {
                        if (isInitializationCall(userCall, bitcast))
                            return true;
                    }
                }
            }
        }

        if (isa<BranchInst>(&inst) || isa<ReturnInst>(&inst) ||
            isa<SwitchInst>(&inst) || isa<IndirectBrInst>(&inst))
            break;
    }

    return false;
}

bool KMeldPass::checkImmediateInitialization(Instruction *inst, CallBase *targetCB) {
    if (!inst) return false;

    BasicBlock::iterator it(inst);
    while (it != inst->getParent()->end()) {
        Instruction *currentInst = &*it;
        ++it;

        if (auto *storeInst = dyn_cast<StoreInst>(currentInst)) {
            if (storeInst->getValueOperand() == targetCB)
                return true;
        }
        if (auto *call = dyn_cast<CallBase>(currentInst)) {
            if (isInitializationCall(call, targetCB))
                return true;
        }
    }

    return false;
}
