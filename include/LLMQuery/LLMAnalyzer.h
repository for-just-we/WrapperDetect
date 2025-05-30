//
// Created by prophe cheng on 2025/5/28.
//

#ifndef WRAPPERDETECT_LLMANALYZER_H
#define WRAPPERDETECT_LLMANALYZER_H

#include "Utils/Tool/Http.h"
#include "Utils/Tool/Common.h"
#include "llvm/ADT/StringExtras.h"
#include <tuple>
#include <utility>

using namespace std;
// interact with LLM by curl

class LLMAnalyzer {
public:
    string query_url;
    string model;
    float temperature;
    unsigned totalQueryNum = 0;
    long totalLLMTime = 0;
    unsigned totalInputTokenNum = 0;
    unsigned totalOutputTokenNum = 0;
    unsigned retry;
    unsigned voteTime;

    explicit LLMAnalyzer(const string& addr, float _temparature = -1, unsigned _retry = 3, unsigned _vote = 5, string _logDir = ""):
        temperature(_temparature), retry(_retry), voteTime(_vote) {
        string base_url = "http://" + addr + "/v1";
        string check_model_url = base_url + "/models";
        query_url = base_url + "/chat/completions";

        json models;
        if (!httpGet(base_url + "/models", models)) {
            OP << "unable to get model information\n";
            exit(1);
        }
        model = models["data"][0]["id"];
        OP << "model is: " << model << "\n";
    }

    string queryLLM(string& SysPrompt, string& UserPrompt, vector<string>& curLogs);

    bool classify(string& SysPrompt, string& UserPrompt, string SummarizingTemplate, vector<string>& curLogs);
};

#endif //WRAPPERDETECT_LLMANALYZER_H
