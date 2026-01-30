//
// Created on 2025/5/28.
//

#ifndef WRAPPERDETECT_HTTP_H
#define WRAPPERDETECT_HTTP_H

#include <string>
#include "nlohmann/json.hpp"

using namespace std;
using namespace nlohmann;

size_t WriteCallback(void* contents, size_t size, size_t nmemb, string* s);
// 省略其他代码，和之前类似

// HTTP GET请求，成功返回true，结果解析到out_json
bool httpGet(const string& url, json& out_json);

// HTTP POST请求，发送json格式payload，成功返回true，响应json解析到out_json
bool httpPost(const string& url, const json& payload, json& out_json);

#endif //WRAPPERDETECT_HTTP_H
