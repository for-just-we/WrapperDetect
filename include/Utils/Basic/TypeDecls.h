

#ifndef WRAPPERDETECT_TYPEDECLS_H
#define WRAPPERDETECT_TYPEDECLS_H

#include <llvm/IR/Module.h>
#include <llvm/IR/InstrTypes.h>

#include <vector>

using namespace llvm;
using namespace std;

const string znLabel = "_ZN";


// 常用类型定义
typedef vector<pair<Module*, StringRef>> ModuleList; // 模块列表类型，每个模块对应一个Module*对象以及一个模块名
// Mapping module to its file name.
typedef unordered_map<Module*, StringRef> ModuleNameMap; // 将模块对象映射为模块名的类型
// The set of all functions.
typedef SmallPtrSet<Function*, 8> FuncSet; // 函数集合类型
typedef SmallPtrSet<CallBase*, 8> CallInstSet; // Call指令集合类型
typedef DenseMap<Function*, CallInstSet> CallerMap; // 将Function对象映射为对应的callsite集合
typedef DenseMap<CallBase*, FuncSet> CalleeMap; // 将Call指令映射为对应的函数集合
typedef unordered_map<Function*, set<Function*>> CallMap; // 将Function映射为它调用的函数
typedef DenseMap<Function*, FuncSet> CalledMap; // 将Function对象映射为调用了它的函数

#endif //WRAPPERDETECT_TYPEDECLS_H
