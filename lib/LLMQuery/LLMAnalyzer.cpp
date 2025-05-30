//
// Created by prophe cheng on 2025/5/28.
//
#include "LLMQuery/LLMAnalyzer.h"
#include "Utils/Tool/Common.h"
#include <chrono>
#include <iostream>

string LLMAnalyzer::queryLLM(string& SysPrompt, string& UserPrompt, vector<string>& curLogs) {
    json payload = {
            {"model", model},
            {"messages", json::array()} // 初始化为空数组
    };
    // 如果 SysPrompt 非空，添加 system 消息
    if (!SysPrompt.empty())
        payload["messages"].push_back({{"role", "system"}, {"content", SysPrompt}});
    // 无论 SysPrompt 是否为空，都添加 user 消息
    payload["messages"].push_back({{"role", "user"},{"content", UserPrompt}});

    if (temperature != -1)
        payload["temperature"] = temperature;

    json resp;
    string content = "<Error>";
    auto start = chrono::high_resolution_clock::now();
    unsigned queries = 0;
    while (queries < retry) {
        if (!httpPost(query_url, payload, resp)) {
            ++queries;
            continue;
        }

        content = resp["choices"][0]["message"]["content"];
        string reasoning = resp["choices"][0]["message"]["reasoning_content"];
        int input_tokens = resp["usage"]["prompt_tokens"];
        int output_tokens = resp["usage"]["completion_tokens"];

        string totalLog;
        totalLog.append("************response*************\n");
        totalLog.append(content);
        totalLog.append("************reasoning****************\n");
        totalLog.append(reasoning);
        totalLog.append("\n");

        curLogs.emplace_back(totalLog);
        totalQueryNum += 1;
        totalInputTokenNum += input_tokens;
        totalOutputTokenNum += output_tokens;
        break;
    }
    auto end = chrono::high_resolution_clock::now();
    // 计算耗时（毫秒）
    long duration_s = chrono::duration_cast<chrono::seconds>(end - start).count();
    totalLLMTime += duration_s;

    return strip(content);
}


bool LLMAnalyzer::classify(string& SysPrompt, string& UserPrompt, string SummarizingTemplate, vector<string>& curLogs) {
    unsigned yesTime = 0;
    curLogs.emplace_back("*********query**************\n" + UserPrompt + "\n");
    for (unsigned i = 0; i < voteTime; ++i) {
        string content = queryLLM(SysPrompt, UserPrompt, curLogs);
        if (CmpFirst(content, "yes")) {
            yesTime += 1;
            continue;
        }
        else if (CmpFirst(content, "no"))
            continue;

        SummarizingTemplate = replaceAll(SummarizingTemplate, "{answer}", content);
        // summarize the results
        string empty;

        curLogs.emplace_back("*********summarizing**************:\n" + content + "\n");
        content = queryLLM(empty, content, curLogs);
        if (CmpFirst(content, "yes"))
            yesTime += 1;
    }
    bool isSimple = yesTime > (voteTime / 2);
    curLogs.emplace_back(isSimple ? "final answer: yes" : "final answer: no");
    return isSimple;
}