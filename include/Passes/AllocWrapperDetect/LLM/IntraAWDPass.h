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
    string logDir;

    IntraAWDPass(GlobalContext* GCtx_, unordered_map<string, FunctionInfo>& _sourceInfos, string _summarizingTemplate,
                 LLMAnalyzer* _analyzer, string _intraSysPrompt = "", string _intraUserPrompt = "", string _logDir = ""):
        EHAWDPass(GCtx_), sourceInfos(_sourceInfos), SummarizingTemplate(_summarizingTemplate), llmAnalyzer(_analyzer),
        IntraSysPrompt(_intraSysPrompt), IntraUserTemplate(_intraUserPrompt), logDir(_logDir)
        {
        ID = "LLM-enhanced simple alloc wrapper detection pass";
    }

    bool doModulePass(Module* M) override;
};

#endif //WRAPPERDETECT_INTRAAWDPASS_H
