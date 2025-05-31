//
// Created by prophe cheng on 2025/5/23.
//

#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"

#include "Passes/AllocWrapperDetect/Heuristic/EHAWDPass.h"
#include "Utils/Basic/Tarjan.h"
#include <queue>

#include "llvm/IR/DebugInfoMetadata.h"

void EHAWDPass::identifySideEffectFunctions() {
    OP << "LLM-enhanced Pass: analyze side-effect function start\n";

    for (vector<Function*> scc: Ctx->SCC) {
        set<Function*> sccSet(scc.begin(), scc.end());
        for (Function* F: scc) {
            if (F->isDeclaration())
                continue;

            // traversing every instruction
            for (inst_iterator i = inst_begin(F), e = inst_end(F); i != e; ++i) {
                if (StoreInst* SI = dyn_cast<StoreInst>(&*i)) {
                    // store non-null pointer data should be conservatively deemed as side-effect
                    if (analyzeStore(SI))
                        func2SideEffectOps[F].insert(make_pair(SI, SideEffectType::Store));
                }

                else if (CallInst* CI = dyn_cast<CallInst>(&*i)) {
                    Function* Callee = CommonUtil::getBaseFunction(CI->getCalledOperand());
                    // recursive call, skip
                    if (Callee == F)
                        continue;
                    // call sensitive API
                    if (Callee && Callee->isDeclaration()) {
                        if (sensiAPI.count(Callee->getName().str()) || deallocAPI.count(Callee->getName().str()))
                            func2SideEffectOps[F].insert(make_pair(CI, SideEffectType::SysCall));

                            // if copy cal like: memcpy(dst, src). check whether dst is complex pointer
                        else if (copyAPI.count(Callee->getName().str())) {
                            // dst could store other pointer
                            if (analyzeCopyArg(CI->getArgOperand(0)) || analyzeCopyArg(CI->getArgOperand(1)))
                                func2SideEffectOps[F].insert(make_pair(CI, SideEffectType::Store));
                        }
                        continue;
                    }

                    // multiple potential callees， if one is side-effect function.
                    // then this function is side-effect.
                    // 如果参数和返回值都是非complex类型并且没有load全局变量的操作，可以跳过
                    for (Function* CF: Ctx->Callees[CI]) {
                        if (func2SideEffectOps.count(CF)) {
                            func2SideEffectOps[F].insert(make_pair(CI, SideEffectType::Call));
                            break;
                        }
                    }
                }
            }

            // for side-effect function F, promote side-effect to its caller in current SCC.
            if (func2SideEffectOps.count(F)) {
                queue<Function*> worklist;
                worklist.push(F);
                set<Function*> visited;
                while (!worklist.empty()) {
                    Function* curF = worklist.front();
                    worklist.pop();
                    // caller map is constant so we don't need to visit a Function twice.
                    if (visited.count(curF))
                        continue;
                    visited.insert(curF);

                    // all caller instruction
                    for (CallInst* CI: Ctx->Callers[curF]) {
                        Function* CallerF = CI->getFunction();
                        // if CallerF not in current SCC
                        if (!sccSet.count(CallerF))
                            continue;
                        pair<Instruction*, SideEffectType> item = make_pair(CI, SideEffectType::Call);
                        // if CI has already been recorded
                        if (func2SideEffectOps[CallerF].count(item))
                            continue;

                        func2SideEffectOps[CallerF].insert(item);
                        worklist.push(CallerF);
                    }
                }
            }
        }

    }

    OP << "LLM-enhanced Pass: analyze side-effect function done\n";
}

bool EHAWDPass::doModulePass(Module* M) {
    // analyze strong connected component
    Tarjan tarjan(Ctx->CallMaps);
    tarjan.getSCC(Ctx->SCC);
    // first identify side-effect functions
    identifySideEffectFunctions();
    // identify simple allocation wrappers
    for (const vector<Function*>& sc: Ctx->SCC) {
        queue<Function*> worklist;
        set<Function*> inWorklist;

        for (Function* F : sc) {
            worklist.push(F);
            inWorklist.insert(F);
        }

        while (!worklist.empty()) {
            Function* F = worklist.front();
            worklist.pop();
            inWorklist.erase(F);

            bool isAlloc = false;
            bool everyAllocReturned = true;
            set<CallInst*> visitedAllocCalls;
            checkWhetherAlloc(F, isAlloc, everyAllocReturned, visitedAllocCalls);
            if (!isAlloc || !everyAllocReturned)
                continue;

            set<CallInst*> potentialAllocs;
            potentialAllocs.insert(visitedAllocCalls.begin(), visitedAllocCalls.end());
            processPotentialAllocs(F, potentialAllocs);

            // determine whether F could be a simple alloc function
            bool simpleRet = true;
            bool operateGlob = checkSimpleAlloc(F, simpleRet, potentialAllocs);

            if (!simpleRet)
                continue;

            // if F has side-effect, check whether side-effect affect
            if (func2SideEffectOps.count(F)) {
                // check whether current function load from global variable
                // count side-effect instructions
                bool sideEffectIgnorable = true;
                for (pair<Instruction*, SideEffectType> sideEffect: func2SideEffectOps[F]) {
                    if (sideEffect.second == SideEffectType::Store) {
                        sideEffectIgnorable = false;
                        break;
                    }
                    CallInst* CI = dyn_cast<CallInst>(sideEffect.first);
                    if (!potentialAllocs.count(CI)) {
                        bool allArgSimpleType = !isComplexType(CI->getType());
                        for (unsigned i = 0; i < CI->getNumOperands() - 1; ++i) {
                            if (isComplexType(CI->getArgOperand(i)->getType())) {
                                allArgSimpleType = false;
                                break;
                            }
                        }
                        if (!allArgSimpleType || operateGlob) {
                            sideEffectIgnorable = false;
                            break;
                        }
                    }
                }

                if (!sideEffectIgnorable)
                    continue;
            }

            // F is simple function wrapper
            // found new simple allocation wrapper, push upper caller to worklist
            promoteToCaller(F, visitedAllocCalls, worklist, inWorklist);
        }
    }
    return false;
}

bool EHAWDPass::checkSimpleAlloc(Function* F, bool &simpleRet, set<CallInst*>& potentialAllocs) {
    bool operateGlob = false;
    for (inst_iterator i = inst_begin(F), e = inst_end(F); i != e; ++i) {
        Instruction* I = &*i;
        if (ReturnInst* RI = dyn_cast<ReturnInst>(I)) {
            if (!analyzeReturn(RI, potentialAllocs)) {
                simpleRet = false;
                break;
            }
        }

        for (unsigned ii = 0; ii < I->getNumOperands(); ++ii)
            if (GlobalVariable* GV = dyn_cast<GlobalVariable>(I->getOperand(ii))) {
                if (GV->getType()->isPointerTy() && isComplexType(GV->getType()->getNonOpaquePointerElementType()))
                    operateGlob = true;
            }
    }
    return operateGlob;
}