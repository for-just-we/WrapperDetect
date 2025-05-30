//
// Created by prophe cheng on 2025/5/15.
//

#ifndef WRAPPERDETECT_INTRAAWDPASS_H
#define WRAPPERDETECT_INTRAAWDPASS_H

#include "Passes/AllocWrapperDetect/Heuristic/EHAWDPass.h"
#include "Utils/Basic/SourceCodeInfo.h"

#include "LLMQuery/LLMAnalyzer.h"


// Intra-procedural LLM-enhanced allocation wrapper detection pass
class IntraAWDPass: public EHAWDPass {
private:
    unordered_map<string, FunctionInfo>& sourceInfos;
    string IntraSysPrompt;
    string IntraUserTemplate;

public:
    string SummarizingTemplate;
    LLMAnalyzer* llmAnalyzer;

    IntraAWDPass(GlobalContext* GCtx_, unordered_map<string, FunctionInfo>& _sourceInfos, string _summarizingTemplate,
                 LLMAnalyzer* _analyzer, string _intraSysPrompt = "", string _intraUserPrompt = ""):
        EHAWDPass(GCtx_), sourceInfos(_sourceInfos), SummarizingTemplate(_summarizingTemplate), llmAnalyzer(_analyzer),
        IntraSysPrompt(_intraSysPrompt), IntraUserTemplate(_intraUserPrompt)
        {
        ID = "LLM-enhanced simple alloc wrapper detection pass";
    }

    bool doModulePass(Module* M) override;
};

#endif //WRAPPERDETECT_INTRAAWDPASS_H
