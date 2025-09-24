//
// Created by prophe cheng on 2025/4/15.
//
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Operator.h>

#include <queue>

#include "Passes/AllocWrapperDetect/Heuristic/HAWDPass.h"
#include "Utils/Basic/Tarjan.h"


bool HAWDPass::doInitialization(Module* M) {
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

// we deem pointer type data store cause side-effect cause it brings hard-predicted alias relationships
bool HAWDPass::doModulePass(Module* M) {
    // analyze strong connected component
    Tarjan tarjan(Ctx->CallMaps);
    tarjan.getSCC(Ctx->SCC);
    // first identify side-effect functions
    identifySideEffectFunctions();

    // identify simple allocation wrappers
    for (const vector<Function*>& sc: Ctx->SCC) {
        queue<Function*> worklist;
        set<Function*> inWorklist;

        for (Function* F: sc) {
            if (sideEffectFuncs.count(F))
                continue;
            worklist.push(F);
            inWorklist.insert(F);
        }

        while (!worklist.empty()) {
            Function* F = worklist.front();
            worklist.pop();
            inWorklist.erase(F);

            bool isAlloc = false;
            bool everyAllocReturned = true;
            set<CallBase*> visitedAllocCalls;
            checkWhetherAlloc(F, isAlloc, everyAllocReturned, visitedAllocCalls);
            if (!isAlloc || !everyAllocReturned)
                continue;

            set<CallBase*> potentialAllocs;
            potentialAllocs.insert(visitedAllocCalls.begin(), visitedAllocCalls.end());
            processPotentialAllocs(F, potentialAllocs);

            bool simpleRet = true;
            checkSimpleAlloc(F, simpleRet, potentialAllocs);
            if (!simpleRet)
                continue;

            // F is simple function wrapper
            // found new simple allocation wrapper, push upper caller to worklist
            promoteToCaller(F, visitedAllocCalls, worklist, inWorklist);
        }
    }
    return false;
}

// make sure all the return value come from current function
bool HAWDPass::analyzeReturn(ReturnInst* RI, set<CallBase*>& visitAllocCalls) {
    if (!RI->getReturnValue())
        return true;
    set<Value*> visitedV;
    queue<Value*> worklist;
    worklist.push(RI->getReturnValue());

    while (!worklist.empty()) {
        Value* curV = worklist.front();
        worklist.pop();
        if (visitedV.count(curV))
            continue;
        visitedV.insert(curV);

        // return a null value, no side-effect return.
        // if return a call value, we would verify the call later. For now, we deem it as non-side effect
        if (isa<ConstantPointerNull>(curV))
            continue;

        else if (isa<CallBase>(curV)) {
            CallBase* CI = dyn_cast<CallBase>(curV);
            Function* CF = CommonUtil::getBaseFunction(CI->getCalledOperand());
            if (visitAllocCalls.count(CI))
                continue;
            else if (CF) {
                if (copyAPI.count(CF->getName().str())){
                    worklist.push(CI->getArgOperand(0));
                    continue;
                }

                else if (func2args.count(CF)) {
                    for (unsigned argNo: func2args[CF])
                        worklist.push(CI->getArgOperand(argNo));
                    continue;
                }
            }
            return false;
        }

        // copy inst: curV = V, we keep trace V
        else if (isa<BitCastInst>(curV) || isa<PtrToIntInst>(curV) || isa<IntToPtrInst>(curV)) {
            Instruction* I = dyn_cast<Instruction>(curV);
            worklist.push(I->getOperand(0));
        }
        // copy operator: curV = V, we keep trace V
        else if (isa<BitCastOperator>(curV) || isa<PtrToIntOperator>(curV)) {
            Operator* O = dyn_cast<Operator>(curV);
            worklist.push(O->getOperand(0));
        }

        else if (isa<PHINode>(curV)) {
            PHINode* PN = dyn_cast<PHINode>(curV);
            for (unsigned i = 0; i < PN->getNumIncomingValues(); ++i)
                worklist.push(PN->getIncomingValue(i));
        }

        // could be Argument, GlobalVariable, Load, Gep
        else
            return false;
    }
    return true;
}


void HAWDPass::identifySideEffectFunctions() {
    OP << "HAWD Pass: analyze side-effect function start\n";

    for (const vector<Function*>& scc: Ctx->SCC) {
        for (Function* F: scc) {
            if (sideEffectFuncs.count(F))
                continue;
            if (F->isDeclaration())
                continue;
            bool hasSideEffect = false;

            for (inst_iterator i = inst_begin(F), e = inst_end(F); i != e; ++i) {
                if (StoreInst* SI = dyn_cast<StoreInst>(&*i)) {
                    // store non-null pointer data should be conservatively deemed as side-effect
                    if (analyzeStore(SI)) {
                        hasSideEffect = true;
                        break;
                    }
                }

                else if (CallBase* CI = dyn_cast<CallBase>(&*i)) {
                    Function* Callee = CommonUtil::getBaseFunction(CI->getCalledOperand());
                    // recursive call, skip
                    if (Callee == F)
                        continue;
                    // call sensitive API
                    if (Callee && Callee->isDeclaration()) {
                        if (sensiAPI.count(Callee->getName().str()) || deallocAPI.count(Callee->getName().str())) {
                            hasSideEffect = true;
                            break;
                        }

                        // if copy cal like: memcpy(dst, src). check whether dst is complex pointer
                        else if (copyAPI.count(Callee->getName().str())) {
                            // dst could store other pointer
                            if (analyzeCopyArg(CI->getArgOperand(0)) || analyzeCopyArg(CI->getArgOperand(1))) {
                                hasSideEffect = true;
                                break;
                            }
                        }
                    }

                    // multiple potential callees， if one is side-effect function.
                    // then this function is side-effect.
                    for (Function* CF: Ctx->Callees[CI]) {
                        if (sideEffectFuncs.count(CF)) {
                            hasSideEffect = true;
                            break;
                        }
                    }
                    if (hasSideEffect)
                        break;
                }
            }

            // for side-effect function F, promote side-effect to its caller.
            if (hasSideEffect) {
                queue<Function*> worklist;
                worklist.push(F);

                while (!worklist.empty()) {
                    Function* curF = worklist.front();
                    worklist.pop();
                    // since all traversed function must be side-effect, sideEffectFuncs can be used as visit set.
                    if (sideEffectFuncs.count(curF))
                        continue;

                    sideEffectFuncs.insert(curF);
                    for (Function* calledF: Ctx->CalledMaps[curF])
                        worklist.push(calledF);
                }
            }
        }
    }

    OP << "HAWD Pass: analyze side-effect function done\n";
}

// return true means has side-effect
bool HAWDPass::analyzeCopyArg(Value* dstV) {
    assert(dstV->getType()->isPointerTy());
    Type* dstEleTy = dstV->getType()->getNonOpaquePointerElementType();
    if (dstEleTy->isPointerTy())
        return true;

    // may be cast from another pointer type
    if (BitCastInst* BI = dyn_cast<BitCastInst>(dstV)) {
        Type* castEleTy = BI->getSrcTy()->getNonOpaquePointerElementType();
        if (isComplexType(castEleTy))
            return true;
    }
    return false;
}

bool HAWDPass::analyzeStore(StoreInst* SI) {
    Type* valTy = SI->getValueOperand()->getType();
    // store non-null pointer data
    if (valTy->isPointerTy() && !isa<ConstantPointerNull>(SI->getValueOperand()))
        return true;
    // array or vector element must be simple
    else if (valTy->isArrayTy() || valTy->isVectorTy()) {
        Type* EleTy = nullptr;
        if (valTy->isArrayTy())
            EleTy = valTy->getArrayElementType();
        else if (valTy->isVectorTy()) {
            if (auto* fixedVecTy = dyn_cast<FixedVectorType>(valTy))
                EleTy = fixedVecTy->getElementType();
            else if (auto* scalableVecTy = dyn_cast<ScalableVectorType>(valTy))
                EleTy = scalableVecTy->getElementType();
        }

        if (EleTy && (EleTy->isPointerTy() || EleTy->isArrayTy() || EleTy->isStructTy() || EleTy->isVectorTy()))
            return true;
    }
    return false;
}


void HAWDPass::promoteToCaller(Function* F, set<CallBase*>& visitedAllocCalls,
                                queue<Function*>& worklist, set<Function*>& inWorklist) {
    for (CallBase* CI: visitedAllocCalls)
        callInWrappers[F].insert(CI);
    if (!AllocWrappers.count(F)) {
        AllocWrappers.insert(F);
        for (CallBase* callerCI: Ctx->Callers[F]) {
            bool isSimpleWrapper = true;
            for (Function* _Callee: Ctx->Callees[callerCI]) {
                if (!AllocWrappers.count(_Callee)) {
                    isSimpleWrapper = false;
                    break;
                }
            }
            if (!isSimpleWrapper)
                continue;
            Function* callerFunc = callerCI->getFunction();
            function2AllocCalls[callerFunc].insert(callerCI);
            //
            if (!inWorklist.count(callerFunc)) {
                worklist.push(callerFunc);
                inWorklist.insert(callerFunc);
            }
        }
    }
}


void HAWDPass::checkWhetherAlloc(Function* F, bool& isAlloc, bool& everyAllocReturned,
                                 set<CallBase*>& visitedAllocCalls) {
    if (function2AllocCalls.count(F)) {
        // iterate every simple alloc call in F
        for (CallBase* curCI: function2AllocCalls[F]) {
            set<Value*> visitedValues;
            // if this simple alloc call could flow to return
            if (traceValueFlow(curCI, visitedValues)) {
                isAlloc = true;
                visitedAllocCalls.insert(curCI);
            }
                // this alloc call is not returned, memory leak could exist
            else
                everyAllocReturned = false;
        }
    }
}


void HAWDPass::processPotentialAllocs(Function* F, set<CallBase*>& potentialAllocs) {
    for (inst_iterator i = inst_begin(F), e = inst_end(F); i != e; ++i) {
        Instruction* I = &*i;
        if (CallBase* CI = dyn_cast<CallBase>(I)) {
            if (CI->getCalledFunction() && CI->getCalledFunction()->isIntrinsic())
                continue;
            bool selfCall = true;
            for (Function* _Callee: Ctx->Callees[CI]) {
                if (_Callee != F) {
                    selfCall = false;
                    break;
                }
            }
            if (selfCall)
                potentialAllocs.insert(CI);
        }
    }
}


bool HAWDPass::checkSimpleAlloc(Function* F, bool &simpleRet, set<CallBase*>& potentialAllocs) {
    for (inst_iterator i = inst_begin(F), e = inst_end(F); i != e; ++i) {
        Instruction* I = &*i;
        if (ReturnInst* RI = dyn_cast<ReturnInst>(I)) {
            if (!analyzeReturn(RI, potentialAllocs)) {
                simpleRet = false;
                break;
            }
        }
    }
    return false;
}