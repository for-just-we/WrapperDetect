//
// Created on 2025/5/28.
//
#include "LLMQuery/LLMAnalyzer.h"
#include "Utils/Tool/Common.h"
#include <future>
#include <mutex>
#include <chrono>

mutex log_mutex;
mutex stats_mutex;

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
    unsigned queries = 0;
    while (queries < retry) {
        if (!httpPost(query_url, payload, resp)) {
            ++queries;
            continue;
        }

        content = resp["choices"][0]["message"]["content"];
        int input_tokens = resp["usage"]["prompt_tokens"];
        int output_tokens = resp["usage"]["completion_tokens"];

        string totalLog;
        totalLog.append("************response*************\n");
        totalLog.append(content);
        if (resp["choices"][0]["message"]["reasoning_content"].is_string()) {
            string reasoning = resp["choices"][0]["message"]["reasoning_content"];
            totalLog.append("\n************reasoning****************\n");
            totalLog.append(reasoning);
        }
        totalLog.append("\n");
        curLogs.emplace_back(totalLog);
        {
            lock_guard<mutex> lock(stats_mutex);
            totalQueryNum += 1;
            totalInputTokenNum += input_tokens;
            totalOutputTokenNum += output_tokens;
        }
        break;
    }

    return strip(content);
}


bool LLMAnalyzer::classify(string& SysPrompt, string& UserPrompt, string SummarizingTemplate, vector<string>& curLogs) {
    unsigned yesTime = 0;
    unsigned requiredTime = voteTime / 2;
    curLogs.emplace_back("*********query**************\n" + UserPrompt + "\n");
    auto start = chrono::high_resolution_clock::now();
    vector<future<bool>> futures;
    for (unsigned i = 0; i < voteTime; ++i) {
        futures.emplace_back(async(launch::async, [&]() -> bool {
            vector<string> localLogs;
            string content = queryLLM(SysPrompt, UserPrompt, localLogs);
            if (CmpFirst(content, "yes")) {
                lock_guard<mutex> log_lock(log_mutex);
                curLogs.insert(curLogs.end(), localLogs.begin(), localLogs.end());
                return true;
            }
            else if (CmpFirst(content, "no")) {
                lock_guard<mutex> log_lock(log_mutex);
                curLogs.insert(curLogs.end(), localLogs.begin(), localLogs.end());
                return false;
            }

            SummarizingTemplate = replaceAll(SummarizingTemplate, "{answer}", content);
            localLogs.emplace_back("*********summarizing**************:\n" + content + "\n");

            string empty;
            string summarized = queryLLM(empty, content, localLogs);
            {
                lock_guard<mutex> log_lock(log_mutex);
                curLogs.insert(curLogs.end(), localLogs.begin(), localLogs.end());
            }

            return CmpFirst(summarized, "yes");
        }));
    }

    for (auto& f : futures)
        if (f.get())
            yesTime++;
    auto end = chrono::high_resolution_clock::now();
    // 计算耗时（毫秒）
    long duration_s = chrono::duration_cast<chrono::seconds>(end - start).count();
    totalLLMTime += duration_s;
    bool isSimple = yesTime > requiredTime;
    curLogs.emplace_back(isSimple ? "final answer: yes" : "final answer: no");
    return isSimple;
}