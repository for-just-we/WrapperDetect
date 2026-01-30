//
// Created on 2025/5/23.
//

#include <llvm/Support/JSON.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/ADT/Optional.h>
#include <fstream>
#include <sstream>

#include "Utils/Basic/SourceCodeInfo.h"

using namespace llvm;
using namespace llvm::json;

unordered_map<string, FunctionInfo> parseSourceFileInfo(const string &sourceFilePath) {
    unordered_map<string, FunctionInfo> result;
    ifstream inputFile(sourceFilePath);
    string line;
    int lineCount = 0;

    while (getline(inputFile, line)) {
        ++lineCount;

        Expected<json::Value> parsed = parse(line);
        if (!parsed) {
            errs() << "Failed to parse JSON on line " << lineCount << "\n";
            consumeError(parsed.takeError());
            continue;
        }

        json::Object* obj = parsed->getAsObject();
        if (!obj) {
            errs() << "Line " << lineCount << " is not a JSON object.\n";
            continue;
        }

        Optional<StringRef> codeVal = obj->getString("code");
        Optional<StringRef> fileVal = obj->getString("file");
        Optional<int64_t> lineVal = obj->getInteger("line");
        Optional<StringRef> nameVal = obj->getString("name");
        Optional<StringRef> preVal  = obj->getString("preprocessed_code");

        if (!codeVal || !fileVal || !lineVal || !nameVal) {
            errs() << "Missing required fields on line " << lineCount << "\n";
            continue;
        }

        string key = extractKey(fileVal->str(), *lineVal, nameVal->str());
        string preprocessed = preVal ? string(*preVal) : "";
        result[key] = {string(*codeVal), preprocessed};
    }

    return result;
}