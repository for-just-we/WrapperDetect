//
// Created by prophe cheng on 2025/4/9.
//

#ifndef WRAPPERDETECT_GLOBALCONTEXT_H
#define WRAPPERDETECT_GLOBALCONTEXT_H

#include "Utils/Basic/TypeDecls.h"
#include <map>

class CommonUtil {
public:
    // Map from struct elements to its name
    static map<string, set<StringRef>> elementsStructNameMap;

    //
    // Common functions
    //
    static string getValidStructName(string structName);

    static string getValidStructName(StructType* Ty);

    // 获取函数F的第ArgNo个参数对象
    static Argument* getParamByArgNo(Function* F, int8_t ArgNo);

    // 根据函数F的FunctionType (返回类型、参数类型、是否支持可变参数)计算F的hash值
    static size_t funcHash(Function* F, bool withName = false);

    // 根据callsite对应的FunctionType计算hash
    static size_t callHash(CallInst* CI);

    static size_t typeHash(Type* Ty);

    static size_t typeIdxHash(Type* Ty, int Idx = -1);

    static size_t hashIdxHash(size_t Hs, int Idx = -1);

    static string structTyStr(StructType* STy);

    static int64_t getGEPOffset(const Value* V, const DataLayout* DL);

    // 从所有模块加载结构体信息，初始化使用
    static void LoadElementsStructNameMap(vector<pair<Module*, StringRef>> &Modules);
};

// 保存中间及最终结果的结构体
struct GlobalContext {
    GlobalContext() = default;

    // Statistics
    unsigned NumVirtualCalls = 0;
    unsigned NumFunctions = 0;
    unsigned NumFirstLayerTypeCalls = 0;
    unsigned NumSecondLayerTypeCalls = 0;
    unsigned NumSecondLayerTargets = 0;
    unsigned NumValidIndirectCalls = 0;
    unsigned NumIndirectCallTargets = 0;
    unsigned NumFirstLayerTargets = 0;
    unsigned NumConfinedFuncs = 0;
    unsigned NumSimpleIndCalls = 0;

    // 全局变量，将变量的hash值映射为变量对象，只保存有initializer的全局变量
    DenseMap<size_t, GlobalVariable*> Globals;

    // 将一个global function的id(uint64_t) 映射到实际Function对象.
    map<uint64_t, Function*> GlobalFuncMap;

    // address-taken函数集合
    FuncSet AddressTakenFuncs;

    // 将一个indirect-callsite映射到target function集合，Map a callsite to all potential callee functions.
    CalleeMap Callees;

    // 将一个function映射到对应的caller集合.
    CallerMap Callers;

    // Map a function to the functions it calls
    CallMap CallMaps;

    // Map a function to the functions who call it
    CalledMap CalledMaps;

    // Call Graph SCC
    vector<vector<Function*>> SCC;

    // 将一个函数签名映射为对应函数集合s
    DenseMap<size_t, FuncSet> sigFuncsMap;

    // Indirect call instructions.
    vector<CallInst*> IndirectCallInsts;

    // Modules.
    ModuleList Modules;
    ModuleNameMap ModuleMaps;
    set<string> InvolvedModules;

    set<Function*> AllocWrappers;
    map<Function*, set<CallInst*>> callInWrappers;
    set<string> AllocWrapperKeys;
};


#endif //WRAPPERDETECT_GLOBALCONTEXT_H
