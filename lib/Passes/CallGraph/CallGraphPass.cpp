//
// Created by prophe cheng on 2025/4/9.
//
#include <llvm/Analysis/LoopPass.h>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>
#include <llvm/IR/InstIterator.h>

#include "Passes/CallGraph/CallGraphPass.h"
#include "Utils/Basic/TypeDecls.h"


// first analyze direct calls
bool CallGraphPass::doInitialization(Module* M) {
    // resolve direct calls
    for (Module::iterator f = M->begin(), fe = M->end(); f != fe; ++f) {
        Function *F = &*f;
        if (F->isDeclaration())
            continue;
        unrollLoops(F);
        for (inst_iterator i = inst_begin(F), e = inst_end(F); i != e; ++i) {
            // Map callsite to possible callees.
            if (CallBase *CI = dyn_cast<CallBase>(&*i)) {
                CallSet.insert(CI);
                if (CI->isIndirectCall())
                    continue;
                Function* CF = CommonUtil::getBaseFunction(CI->getCalledOperand());
                // not InlineAsm
                if (CF) {
                    // Call external functions
                    if (CF->isDeclaration()) {
                        if (Function *GF = Ctx->GlobalFuncMap[CF->getGUID()])
                            CF = GF;
                    }

                    Ctx->Callees[CI].insert(CF);
                    Ctx->Callers[CF].insert(CI);
                    Ctx->CallMaps[CI->getFunction()].insert(CF);
                    Ctx->CalledMaps[CF].insert(CI->getFunction());
                }
            }
        }
    }
    return false;
}

bool CallGraphPass::doModulePass(Module *M) {
    ++MIdx;
    //
    // Iterate and process globals
    //
    for (Module::global_iterator gi = M->global_begin(); gi != M->global_end(); ++gi) {
        GlobalVariable* GV = &*gi;

        Type* GTy = GV->getType();
        assert(GTy->isPointerTy());

    }
    if (MIdx == Ctx->Modules.size()) {
    }

    for (Module::iterator f = M->begin(), fe = M->end(); f != fe; ++f) {
        Function *F = &*f;
        if (F->isDeclaration())
            continue;
        for (inst_iterator i = inst_begin(F), e = inst_end(F); i != e; ++i) {
            if (CallBase* CI = dyn_cast<CallBase>(&*i)) {
                if (!CI->isIndirectCall())
                    continue;
                // skip virtual call for now
                FuncSet* FS = &Ctx->Callees[CI];
                if (isVirtualCall(CI)) {
                    VCallSet.insert(CI);
                    analyzeVirtualCall(CI, FS);
                    Ctx->VirtualCallInsts.push_back(CI);
                }
                else {
                    ICallSet.insert(CI);
                    analyzeIndCall(CI, FS);
                    // Save called values for future uses.
                    Ctx->IndirectCallInsts.push_back(CI);
                }

                for (Function* Callee : *FS) {
                    // OP << "**** solving callee: " << Callee->getName().str() << "\n";
                    Ctx->Callers[Callee].insert(CI);
                    Ctx->CallMaps[CI->getFunction()].insert(Callee);
                    Ctx->CalledMaps[Callee].insert(CI->getFunction());
                }
                if (!FS->empty()) {
                    MatchedICallSet.insert(CI);
                    Ctx->NumIndirectCallTargets += FS->size();
                    Ctx->NumValidIndirectCalls++;
                }
            }
        }
    }

    Ctx->NumVirtualCalls += VCallSet.size();
    return false;
}

bool CallGraphPass::doFinalization(llvm::Module *M) {
    ++MIdx;
    return false;
}

void CallGraphPass::unrollLoops(Function* F) {
    if (F->isDeclaration())
        return;

    DominatorTree DT = DominatorTree();
    DT.recalculate(*F);
    LoopInfo *LI = new LoopInfo();
    LI->releaseMemory();
    LI->analyze(DT);

    // Collect all loops in the function
    set<Loop *> LPSet;
    for (LoopInfo::iterator i = LI->begin(), e = LI->end(); i!=e; ++i) {
        Loop *LP = *i;
        LPSet.insert(LP);

        list<Loop *> LPL;
        LPL.push_back(LP);
        while (!LPL.empty()) {
            LP = LPL.front();
            LPL.pop_front();
            vector<Loop *> SubLPs = LP->getSubLoops();
            for (auto SubLP : SubLPs) {
                LPSet.insert(SubLP);
                LPL.push_back(SubLP);
            }
        }
    }

    for (Loop* LP : LPSet) {
        // Get the header,latch block, exiting block of every loop
        BasicBlock *HeaderB = LP->getHeader();
        unsigned NumBE = LP->getNumBackEdges();
        SmallVector<BasicBlock *, 4> LatchBS;

        LP->getLoopLatches(LatchBS);
        for (BasicBlock *LatchB : LatchBS) {
            if (!HeaderB || !LatchB) {
                OP<<"ERROR: Cannot find Header Block or Latch Block\n";
                continue;
            }
            // Two cases:
            // 1. Latch Block has only one successor:
            // 	for loop or while loop;
            // 	In this case: set the Successor of Latch Block to the
            //	successor block (out of loop one) of Header block
            // 2. Latch Block has two successor:
            // do-while loop:
            // In this case: set the Successor of Latch Block to the
            //  another successor block of Latch block

            // get the last instruction in the Latch block
            Instruction *TI = LatchB->getTerminator();
            // Case 1:
            if (LatchB->getSingleSuccessor() != NULL) {
                for (succ_iterator sit = succ_begin(HeaderB);
                     sit != succ_end(HeaderB); ++sit) {

                    BasicBlock *SuccB = *sit;
                    BasicBlockEdge BBE = BasicBlockEdge(HeaderB, SuccB);
                    // Header block has two successor,
                    // one edge dominate Latch block;
                    // another does not.
                    if (DT.dominates(BBE, LatchB))
                        continue;
                    else
                        TI->setSuccessor(0, SuccB);
                }
            }
                // Case 2:
            else {
                for (succ_iterator sit = succ_begin(LatchB);
                     sit != succ_end(LatchB); ++sit) {
                    BasicBlock *SuccB = *sit;
                    // There will be two successor blocks, one is header
                    // we need successor to be another
                    if (SuccB == HeaderB)
                        continue;
                    else
                        TI->setSuccessor(0, SuccB);
                }
            }
        }
    }
}

bool CallGraphPass::isVirtualCall(CallBase* CI) {
    // the callsite must be an indirect one with at least one argument (this
    // ptr)
    if (CommonUtil::getBaseFunction(CI->getCalledOperand()) != nullptr || CI->arg_empty())
        return false;

    // the first argument (this pointer) must be a pointer type and must be a
    // class name
    if (CI->getArgOperand(0)->getType()->isPointerTy() == false)
        return false;

    const Value* vfunc = CI->getCalledOperand();
    if (const LoadInst* vfuncloadinst = dyn_cast<LoadInst>(vfunc)) {
        const Value* vfuncptr = vfuncloadinst->getPointerOperand();
        if (const GetElementPtrInst* vfuncptrgepinst = dyn_cast<GetElementPtrInst>(vfuncptr)) {
            if (vfuncptrgepinst->getNumIndices() != 1)
                return false;
            const Value* vtbl = vfuncptrgepinst->getPointerOperand();
            if (isa<LoadInst>(vtbl) && vtbl->getName().count("vtable"))
                return true;
        }
    }
    return false;
}


bool CallGraphPass::isVirtualFunction(Function* F) {
    if (F->getName().startswith(znLabel))
        return true;
    return false;
}