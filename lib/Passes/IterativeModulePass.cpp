//
// Created by prophe cheng on 2025/4/9.
//
#include "Passes/IterativeModulePass.h"

void IterativeModulePass::run(ModuleList &modules) {
    ModuleList::iterator i, e;
    for (i = modules.begin(), e = modules.end(); i != e; ++i) {
        Module* M = i->first;
        DLMap[M] = &(M->getDataLayout());
        Int8PtrTy[M] = Type::getInt8PtrTy(M->getContext()); // int8 type id: 15
        IntPtrTy[M] = DLMap[M]->getIntPtrType(M->getContext()); // int type id: 13
    }
    OP << "[" << ID << "] Initializing " << modules.size() << " modules ";
    bool again = true;
    while (again) {
        again = false;
        for (i = modules.begin(), e = modules.end(); i != e; ++i) {
            again |= doInitialization(i->first); // type-hierachy and type-propagation
            OP << ".";
        }
        MIdx = 0;
    }
    OP << "\n";

    unsigned iter = 0, changed = 1;
    while (changed) {
        ++iter;
        changed = 0;
        unsigned counter_modules = 0;
        unsigned total_modules = modules.size();
        for (i = modules.begin(), e = modules.end(); i != e; ++i) {
            OP << "[" << ID << " / " << iter << "] ";
            OP << "[" << ++counter_modules << " / " << total_modules << "] ";
            OP << "[" << i->second << "]\n";

            bool ret = doModulePass(i->first);
            if (ret) {
                ++changed;
                OP << "\t [CHANGED]\n";
            } else
                OP << "\n";
        }
        MIdx = 0;
        OP << "[" << ID << "] Updated in " << changed << " modules.\n";
    }

    OP << "[" << ID << "] Postprocessing ...\n";
    again = true;
    while (again) {
        again = false;
        for (i = modules.begin(), e = modules.end(); i != e; ++i) {
            // TODO: Dump the results.
            again |= doFinalization(i->first);
        }
        MIdx = 0;
    }

    OP << "[" << ID << "] Done!\n\n";
}