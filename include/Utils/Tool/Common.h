//
// Created by prophe cheng on 2025/4/9.
//

#ifndef WRAPPERDETECT_COMMON_H
#define WRAPPERDETECT_COMMON_H

#include "llvm/IR/Module.h"
#include "GlobalContext.h"
#include <string>

using namespace std;
using namespace llvm;

string getInstructionText(Value* inst);

string getInstructionText(Type* type);

string removeFuncNumberSuffix(const string& funcName);

string getNormalizedPath(const DISubprogram *DIS);

void dumpAllocationWrapperInfo(const map<Function*, const set<CallInst*>>& function2AllocCalls, GlobalContext* Ctx);

string strip(const string& s);

string replaceAll(string str, const string& from, const string& to);

bool CmpFirst(const string& str, const string target);

void log(const string& file, const vector<string>& curLogs);

#define OP errs()
#define DBG if (debug_mode) OP

#endif //WRAPPERDETECT_COMMON_H
