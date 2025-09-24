//
// Created by prophe on 25-9-24.
//

#ifndef CPPUTILS_H
#define CPPUTILS_H

#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Constants.h>
#include <string>

using namespace llvm;
using namespace std;

typedef struct DemangledName {
    string className;
    string funcName;
    bool isThunkFunc;
} DemangledName;

class CppUtil {
public:
    static DemangledName demangle(const string& name);

    static string getBeforeBrackets(const string& name);

    static void stripBracketsAndNamespace(DemangledName& dname);

    static bool isConstructor(const Function* F);

    static bool isDestructor(const Function* F);

    static const Value* getVCallThisPtr(const CallBase* cs);

    static bool isValVtbl(const Value* val);

    static string getClassNameFromVtblObj(const string& vtblName);

    static const Argument* getConstructorThisPtr(const Function* fun);

    static bool isSameThisPtrInConstructor(const Argument* thisPtr1, const Value* thisPtr2);

    static const ConstantStruct* getVtblStruct(const GlobalValue* vtbl);

    static Function* getThunkTarget(const Function* F);

    static bool isCPPThunkFunction(const Function* F);
};

#endif //CPPUTILS_H
