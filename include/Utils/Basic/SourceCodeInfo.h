//
// Created on 2025/5/23.
//

#ifndef WRAPPERDETECT_SOURCECODEINFO_H
#define WRAPPERDETECT_SOURCECODEINFO_H

#define extractKey(fileName, line, funcName) (fileName + ":" + itostr(line) + ":" + funcName)

#include <string>
#include <unordered_map>
#include <tuple>

using namespace std;

using FunctionInfo = pair<string, string>; // (code, preprocessed_code)

unordered_map<string, FunctionInfo> parseSourceFileInfo(const string &sourceFilePath);

#endif //WRAPPERDETECT_SOURCECODEINFO_H
