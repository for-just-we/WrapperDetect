//
// Created on 25-9-24.
//

#ifndef CHAPASS_H
#define CHAPASS_H

#include "Passes/CallGraph/KELPPass.h"

typedef enum {
    PURE_ABSTRACT = 0x1, // pure virtual abstract class
    MULTI_INHERITANCE = 0x2, // muli inheritance class
    TEMPLATE = 0x4 // template class
} CLASSATTR;

// support virtual call analysis, using CHA
class CppCGPass: public KELPPass {
private:
    set<string> ClassNames;
    /// Save class name and all of it's base class name.
    /// For example: <SubClass, <BaseClass1, BaseClass2, ...>>
    unordered_map<string, set<string>> BottomUpClassHierarchyChain;

    /// Save class name and all of it's sub class name.
    /// For example: <BaseClass, <SubClass1, SubClass2, ...>>
    unordered_map<string, set<string>> TopDownClassHierarchyChain;

    unordered_map<string, size_t> ClassHierarchy;

    unordered_map<string, vector<vector<const Function*>>> virtualFuncVecs;

    unordered_map<string, const GlobalValue*> vtables;


public:
    CppCGPass(GlobalContext* Ctx_): KELPPass(Ctx_) {
        ID = "cpp call graph pass\n";
    }

    // class hierachy analysis
    bool doInitialization(Module*) override;

    // virtual call analysis
    void analyzeVirtualCall(CallBase* callInst, FuncSet* FS) override;

    // F is a constructor or destructor
    void connectInheritEdgeViaCall(const Function* F, const CallBase* cs);

    // F is a constructor or destructor
    void connectInheritEdgeViaStore(const Function* F, const StoreInst* SI);

    // analyze vtables
    void analyzeVTables(Module* M);

    void addFuncToFuncVector(vector<const Function*> &v, const Function* fun);
};

#endif //CHAPASS_H
