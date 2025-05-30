//
// Created by prophe cheng on 2025/5/16.
//
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/DebugInfoMetadata.h"

#include "Passes/AllocWrapperDetect/LLM/IntraAWDPass.h"
#include "Utils/Basic/Tarjan.h"
#include "Utils/Tool/Common.h"
#include <queue>
#include <filesystem>

bool IntraAWDPass::doModulePass(Module* M) {
    // analyze strong connected component
    Tarjan tarjan(Ctx->CallMaps);
    tarjan.getSCC(Ctx->SCC);
    // first identify side-effect functions
    identifySideEffectFunctions();
    // identify simple allocation wrappers
    set<string> visitedKeys;
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

            bool isAlloc = false;
            bool everyAllocReturned = true;
            set<CallInst*> visitedAllocCalls;
            checkWhetherAlloc(F, isAlloc, everyAllocReturned, visitedAllocCalls);
            if (!isAlloc || !everyAllocReturned)
                continue;

            set<CallInst*> potentialAllocs;
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
                string key = extractKey(fileName, F->getSubprogram()->getLine(), funcName);
                FunctionInfo info;
                if (sourceInfos.count(key))
                    info = sourceInfos[key];
                else {
                    key = extractKey(fileName, F->getSubprogram()->getLine() - 1, funcName);
                    if (!sourceInfos.count(key))
                        continue;
                    info = sourceInfos[key];
                }

                if (visitedKeys.count(key))
                    continue;
                visitedKeys.insert(key);

                // count side-effect instructions
                bool sideEffectIgnorable = true;
                bool llmEnable = true;
                set<string> sideEffectCalled;
                set<string> directAllocCalled;
                set<string> indirectAllocCalled;
                // traverse every side-effect instruction
                for (pair<Instruction*, SideEffectType> sideEffect: func2SideEffectOps[F]) {
                    if (sideEffect.second == SideEffectType::Store) {
                        sideEffectIgnorable = false;
                        llmEnable = false;
                        break;
                    }
                    CallInst* CI = dyn_cast<CallInst>(sideEffect.first);
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
                            Function* sideEffectCallee = CI->getCalledFunction();
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
                        bool preprocessed = false;
                        if (!info.second.empty()) {
                            preprocessed = true;
                            code = info.second;
                        }
                        else
                            code = info.first;
                        string sideEffectCalledStr;
                        for (const string& _funcName: sideEffectCalled)
                            sideEffectCalledStr += (_funcName + ",");
                        if (!sideEffectCalledStr.empty())
                            sideEffectCalledStr = sideEffectCalledStr.substr(0, sideEffectCalledStr.size() - 1);

                        for (CallInst* allocCI: visitedAllocCalls) {
                            if (allocCI->isIndirectCall()) {
                                for (Function* allocCallee: Ctx->Callees[allocCI])
                                    indirectAllocCalled.insert(removeFuncNumberSuffix(allocCallee->getName().str()));
                            }
                            else {
                                Function* allocFunc = allocCI->getCalledFunction();
                                directAllocCalled.insert(removeFuncNumberSuffix(allocFunc->getName().str()));
                            }
                        }

                        string preprocessed_text =  preprocessed ? "preprocessed " : "";
                        string userPrompt = IntraUserTemplate;
                        userPrompt = replaceAll(userPrompt, "{function_name}", funcName);
                        userPrompt = replaceAll(userPrompt, "{preprocessed}", preprocessed_text);
                        userPrompt = replaceAll(userPrompt, "{side_effects}", sideEffectCalledStr);
                        userPrompt = replaceAll(userPrompt, "{function_code}", code);
                        vector<string> curLogs;
                        curLogs.emplace_back("key: " + key);
                        isSimple = llmAnalyzer->classify(IntraSysPrompt, userPrompt, SummarizingTemplate, curLogs);

                        if (!logDir.empty()) {
                            curLogs.emplace_back(isSimple ? "final answer: yes" : "final answer: no");
                            string file = "cout";
                            if (logDir != "cout") {
                                int existingFiles = 0;
                                for (const auto& entry: filesystem::directory_iterator(logDir)) {
                                    if (entry.is_regular_file() && entry.path().extension() == ".txt")
                                        existingFiles++;
                                }
                                file = logDir + "/" + to_string(existingFiles + 1) + ".txt";
                            }
                            log(file, curLogs);
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