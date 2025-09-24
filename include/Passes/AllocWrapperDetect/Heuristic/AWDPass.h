//
// Created by prophe cheng on 2025/4/10.
//

#ifndef WRAPPERDETECT_AWDPASS_H
#define WRAPPERDETECT_AWDPASS_H

#include "Passes/IterativeModulePass.h"

// find all alloc wrapper, ignore side-effect. Follow the design of SVF
class AWDPass: public IterativeModulePass {
private:
    set<CallInst*> baseAllocCalls;
public:
    set<Function*> AllocWrappers;
    map<Function*, set<CallInst*>> callInWrappers;

    set<string> allocFuncsNames = {"malloc", "calloc", "safe_calloc", "safe_malloc",
                                   "safecalloc", "safemalloc", "safexcalloc", "safexmalloc",
                                   "savealloc", "xalloc", "xmalloc", "xcalloc", "alloc", "alloc_check",
                                   "alloc_clear", "permalloc", "memalign", "aligned_alloc",

                                   "realloc", "reallocarray", "safe_realloc", "saferealloc", "safexrealloc", "mem_realloc", "xrealloc",
                                   "strdup", "strndup", "__strdup"};

    set<string> copyAPI = {"strcpy", "memcpy", "llvm.memcpy.p0i8.p0i8.i64", "llvm.memcpy.p0.p0.i64", "llvm.memcpy.p0i8.p0i8.i32",
                           "llvm.memcpy.p0.p0.i32", "llvm.memcpy", "llvm.memmove", "llvm.memmove.p0i8.p0i8.i64", "llvm.memmove.p0.p0.i64",
                           "llvm.memmove.p0i8.p0i8.i32", "llvm.memmove.p0.p0.i32", "__memcpy_chk", "memmove", "memccpy",
                           "__strcpy_chk", "stpcpy", "wcscpy"};

    set<string> deallocAPI = {"free"};

    // preserve function return value to args
    unordered_map<Function*, set<unsigned>> func2args;

    AWDPass(GlobalContext* GCtx_): IterativeModulePass(GCtx_) {
        ID = "base alloc wrapper detection pass";
    }

    bool doInitialization(Module* M) override;

    bool doModulePass(Module* M) override;

    bool doFinalization(Module* M) override;

    virtual bool traceValueFlow(Value* V, set<Value*>& visitedValues);

    // for: p = func(p1, ...), we check whether p1 could flow to p
    bool checkFuncArgRet(Function* F, unsigned argNo);
};

#endif //WRAPPERDETECT_AWDPASS_H
