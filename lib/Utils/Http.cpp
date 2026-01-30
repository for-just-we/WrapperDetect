//
// Created on 2025/5/28.
//

#include <curl/curl.h>
#include "Utils/Tool/Http.h"

size_t WriteCallback(void* contents, size_t size, size_t nmemb, string* s) {
    size_t newLength = size * nmemb;
    s->append((char*)contents, newLength);
    return newLength;
}
// 省略其他代码，和之前类似


// HTTP GET请求，成功返回true，结果解析到out_json
bool httpGet(const string& url, json& out_json) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        return false;
    }

    string response_data;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_easy_cleanup(curl);

    if (res != CURLE_OK)
        return false;

    if (http_code != 200)
        return false;

    try {
        out_json = json::parse(response_data);
    } catch (const exception& e) {
        return false;
    }
    return true;
}

// HTTP POST请求，发送json格式payload，成功返回true，响应json解析到out_json
bool httpPost(const string& url, const json& payload, json& out_json) {
    CURL* curl = curl_easy_init();
    if (!curl)
        return false;

    string response_data;
    string payload_str = payload.dump();

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload_str.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK)
        return false;
    if (http_code != 200)
        return false;

    try {
        out_json = json::parse(response_data);
    } catch (const exception& e) {
        return false;
    }
    return true;
}