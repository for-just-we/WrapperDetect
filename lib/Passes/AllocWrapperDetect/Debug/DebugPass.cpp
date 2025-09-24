//
// Created by prophe cheng on 2025/5/31.
//

#include <llvm/IR/InstIterator.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/DebugInfoMetadata.h>
#include <queue>

#include "Passes/AllocWrapperDetect/Debug/DebugPass.h"
#include "Utils/Basic/Tarjan.h"
#include "Utils/Tool/Common.h"


bool DebugPass::doInitialization(Module* M) {
    for (Function &F : *M) {
        if (F.isDeclaration())
            continue;
        if (allocFuncsNames.count(F.getName().str()))
            allocFuncsNames.erase(F.getName().str());
    }

    // collect malloc sources
    for (auto &f: *M) {
        Function *F = &f;
        if (F->isDeclaration())
            continue;
        for (inst_iterator i = inst_begin(F), e = inst_end(F); i != e; ++i) {
            // if call to malloc
            if (CallBase* CI = dyn_cast<CallBase>(&*i)) {
                Function* CF = CommonUtil::getBaseFunction(CI->getCalledOperand());
                if (CF && (allocFuncsNames.count(CF->getName().str()) || preAnalyzedWrappers.count(CF))) {
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

bool DebugPass::doModulePass(Module *M) {
    // analyze strong connected component
    Tarjan tarjan(Ctx->CallMaps);
    tarjan.getSCC(Ctx->SCC);
    // first identify side-effect functions
    identifySideEffectFunctions();

    set<string> interestingFuncs = {"evp_md_ctx_new_ex"};
    // identify simple allocation wrappers

    for (vector<Function*> sc: Ctx->SCC) {
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

            if (preAnalyzedWrappers.count(F))
                continue;

            if (interestingFuncs.count(removeFuncNumberSuffix(F->getName().str()))) {
                for (auto iter: func2SideEffectOps[F]) {
                    OP << iter.second << " ," << getInstructionText(iter.first) << "\n";
                    if (CallBase* _CI = dyn_cast<CallBase>(iter.first))
                        OP << "indirect-call: " << _CI->isIndirectCall() << "\n";
                }
            }

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
            bool operateGlob = checkSimpleAlloc(F, simpleRet, potentialAllocs);
            if (!simpleRet)
                continue;

            // has side-effect
            if (func2SideEffectOps.count(F)) {
                // generate query for LLM
                string fileName = getNormalizedPath(F->getSubprogram());
                string funcName = removeFuncNumberSuffix(F->getName().str());

                // count side-effect instructions
                bool sideEffectIgnorable = true;
                bool llmEnable = true;
                set<string> sideEffectCalled;
                set<string> directAllocCalled;
                set<string> indirectAllocCalled;

                if (interestingFuncs.count(removeFuncNumberSuffix(F->getName().str()))) {
                    for (CallBase* _CI: potentialAllocs)
                        OP << "potential call: " << getInstructionText(_CI) << "\n";
                }

                // traverse every side-effect instruction
                for (pair<Instruction*, SideEffectType> sideEffect: func2SideEffectOps[F]) {
                    if (sideEffect.second == SideEffectType::Store) {
                        sideEffectIgnorable = false;
                        llmEnable = false;
                        break;
                    }
                    CallBase* CI = dyn_cast<CallBase>(sideEffect.first);
                    // unknown side-effect indirect-call
                    if (CI->isIndirectCall() && !potentialAllocs.count(CI))
                        llmEnable = false;

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
                            Function* sideEffectCallee = CommonUtil::getBaseFunction(CI->getCalledOperand());
                            if (sideEffectCallee)
                                sideEffectCalled.insert(removeFuncNumberSuffix(sideEffectCallee->getName().str()));
                        }
                    }
                }

                // if this is a indirect-call, first conservatively treat it.
                // this function can not be simple treat as simple wrapper
                if (!sideEffectIgnorable) {
                    bool isSimple = false;
                    if (llmEnable) {
                        string code;
                        string sideEffectCalledStr;
                        for (const string& _funcName: sideEffectCalled)
                            sideEffectCalledStr += (_funcName + ",");
                        if (!sideEffectCalledStr.empty())
                            sideEffectCalledStr = sideEffectCalledStr.substr(0, sideEffectCalledStr.size() - 1);

                        for (CallBase* allocCI: visitedAllocCalls) {
                            if (allocCI->isIndirectCall()) {
                                for (Function* allocCallee: Ctx->Callees[allocCI])
                                    indirectAllocCalled.insert(removeFuncNumberSuffix(allocCallee->getName().str()));
                            }
                            else {
                                Function* allocFunc = CommonUtil::getBaseFunction(allocCI->getCalledOperand());
                                directAllocCalled.insert(removeFuncNumberSuffix(allocFunc->getName().str()));
                            }
                        }

                    }

                    if (!isSimple)
                        continue;
                }
            }

            // F is simple function wrapper
            // found new simple allocation wrapper, push upper caller to worklist
            promoteToCaller(F, visitedAllocCalls, worklist, inWorklist);
        }
    }

    return false;
}