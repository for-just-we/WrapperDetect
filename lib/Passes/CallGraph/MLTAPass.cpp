//
// Created on 2025/4/10.
//
#include <llvm/IR/Operator.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/DebugInfo.h>
#include <string>

#include "Utils/Basic/Config.h"
#include "Passes/CallGraph/MLTAPass.h"

using namespace std;

bool MLTAPass::doInitialization(Module* M) {
    OP<< "#" << MIdx <<" Initializing: "<<M->getName()<<"\n";
    CallGraphPass::doInitialization(M);
    ++MIdx;

    DLMap[M] = &M->getDataLayout();
    Int8PtrTy[M] = Type::getInt8PtrTy(M->getContext()); // int8 type id: 15
    IntPtrTy[M] = DLMap[M]->getIntPtrType(M->getContext()); // int type id: 13

    set<User*> CastSet;

    unsigned dbgNum = 0;
    // Iterate and process globals
    for (Module::global_iterator gi = M->global_begin(); gi != M->global_end(); ++gi) {
        GlobalVariable* GV = &*gi;

        if (GV->hasInitializer()) {
            Type *ITy = GV->getInitializer()->getType();
            if (!ITy->isPointerTy() && !isCompositeType(ITy)) // 如果不是指针类型或者复杂数据类型，跳过
                continue;
            Ctx->Globals[GV->getGUID()] = GV;
            typeConfineInInitializer(GV);
        }
    }

    // Iterate functions and instructions
    for (Function &F : *M) {
        // Collect address-taken functions.
        // NOTE: declaration functions can also have address taken
        if (F.hasAddressTaken() && !isVirtualFunction(&F)) {
            Ctx->AddressTakenFuncs.insert(&F);
            size_t FuncHash = CommonUtil::funcHash(&F, false);
            // function的hash
            Ctx->sigFuncsMap[FuncHash].insert(&F);
        }

        // The following only considers actual functions with body
        if (F.isDeclaration())
            continue;

        collectAliasStructPtr(&F);
        typeConfineInFunction(&F);
        typePropInFunction(&F);

        // Collect global function definitions.
        if (F.hasExternalLinkage())
            Ctx->GlobalFuncMap[F.getGUID()] = &F;
    }

    if (Ctx->Modules.size() == MIdx) {
        // Map the declaration functions to actual ones
        // NOTE: to delete an item, must iterate by reference
        for (auto &SF : Ctx->sigFuncsMap) {
            // traverse all external link function
            for (auto F : SF.second) {
                if (!F)
                    continue;
                // external linked functions
                if (F->isDeclaration()) {
                    SF.second.erase(F);
                    if (Function *AF = Ctx->GlobalFuncMap[F->getGUID()])
                        SF.second.insert(AF);
                }
            }
        }

        for (auto &TF: typeIdxFuncsMap) {
            for (auto &IF : TF.second) {
                for (auto F : IF.second) {
                    if (F->isDeclaration()) {
                        IF.second.erase(F);
                        if (Function *AF = Ctx->GlobalFuncMap[F->getGUID()])
                            IF.second.insert(AF);
                    }
                }
            }
        }
    }

    return false;
}

void MLTAPass::analyzeIndCall(CallBase* CI, FuncSet* FS) {
    // Initial set: first-layer results
    // TODO: handling virtual functions
    FLTAPass::analyzeIndCall(CI, FS);

    // No need to go through MLTA if the first layer is empty
    if (FS->empty())
        return;

    FuncSet FS1, FS2;
    Type* PrevLayerTy = (dyn_cast<CallBase>(CI))->getFunctionType();
    int PrevIdx = -1;
    // callee expression
    Value* CV = CI->getCalledOperand();
    Value* NextV = nullptr;
    int LayerNo = 1;

    // Get the next-layer type
    list<typeidx_t> TyList;
    bool ContinueNextLayer = true;
    DBG << "analyzing call: " << getInstructionText(CI) << "\n";

    auto *Scope = cast<DIScope>(CI->getDebugLoc().getScope());
    string callsiteFile = Scope->getFilename().str();
    int line = CI->getDebugLoc().getLine();
    int col = CI->getDebugLoc().getCol();
    string content = callsiteFile + ":" + itostr(line) + ":" + itostr(col) + "|";

    while (ContinueNextLayer) {
        // Check conditions
        if (LayerNo >= max_type_layer)
            break;

        if (typeCapSet.find(CommonUtil::typeHash(PrevLayerTy)) != typeCapSet.end())
            break;

        set<Value*> Visited;
        nextLayerBaseType(CV, TyList, NextV, Visited);
        if (TyList.empty())
            break;
        // B.a(A).f，TyList is (A, f), (B, a)
        for (typeidx_t TyIdx: TyList) {
            if (LayerNo >= max_type_layer)
                break;

            size_t TyIdxHash = CommonUtil::typeIdxHash(TyIdx.first, TyIdx.second);
            // -1 represents all possible fields of a struct
            size_t TyIdxHash_1 = CommonUtil::typeIdxHash(TyIdx.first, -1);

            // Caching for performance
            if (MatchedFuncsMap.find(TyIdxHash) != MatchedFuncsMap.end())
                FS1 = MatchedFuncsMap[TyIdxHash];
            else {
                // CurType ∈ escaped-type
                if (typeEscapeSet.find(TyIdxHash) != typeEscapeSet.end()
                    || typeEscapeSet.find(TyIdxHash_1) != typeEscapeSet.end()) {
                    ContinueNextLayer = false;
                    break;
                }
                getTargetsWithLayerType(CommonUtil::typeHash(TyIdx.first), TyIdx.second, FS1);
                // Collect targets from dependent types that may propagate
                // targets to it
                set<hashidx_t> PropSet;
                getDependentTypes(TyIdx.first, TyIdx.second, PropSet);
                // for each PropType in type-propagation[CurType] do
                for (auto Prop: PropSet) {
                    // fromTypeIdx --> curTypeIDx
                    getTargetsWithLayerType(Prop.first, Prop.second, FS2);
                    FS1.insert(FS2.begin(), FS2.end());
                }
                MatchedFuncsMap[TyIdxHash] = FS1;
            }

            // Next layer may not always have a subset of the previous layer
            // because of casting, so let's do intersection
            intersectFuncSets(FS1, *FS, FS2); // FS2 = FS & FS1
            *FS = FS2; // FS = FS & FS1
            if (FS2.empty()) {
                ContinueNextLayer = false;
                break;
            }
            CV = NextV;

            // b.a = a2 in test13; B::a not confine to function，marked escaped，B::a not a function field。
            if (typeCapSet.find(CommonUtil::typeHash(TyIdx.first)) != typeCapSet.end()) {
                ContinueNextLayer = false;
                DBG << "found escaped type: " << getInstructionText(TyIdx.first) << " stop\n";
                break;
            }

            ++LayerNo;
            PrevLayerTy = TyIdx.first;
            PrevIdx = TyIdx.second;
        }
        TyList.clear();
    }

    if (LayerNo > 1) {
        Ctx->NumSecondLayerTypeCalls++;
        Ctx->NumSecondLayerTargets += FS->size();
    }
    else {
        Ctx->NumFirstLayerTargets += Ctx->sigFuncsMap[CommonUtil::callHash(CI)].size();
        Ctx->NumFirstLayerTypeCalls += 1;
    }
}

bool MLTAPass::typeConfineInInitializer(GlobalVariable *GV) {
    DBG << "Evaluation for global variable: " << GV->getName().str() << "\n";
    Constant *Ini = GV->getInitializer();
    if (!isa<ConstantAggregate>(Ini))
        return false;

    list<pair<Type*, int>> NestedInit;
    map<Value*, pair<Value*, int>> ContainersMap; // key为value->first的第value->second个子常量
    set<Value*> FuncOperands;
    list<User*> LU;
    set<Value*> Visited;
    LU.push_back(Ini);

    while (!LU.empty()) {
        User* U = LU.front();
        LU.pop_front();
        if (Visited.find(U) != Visited.end())
            continue;
        
        Visited.insert(U);
        Type* UTy = U->getType();
        assert(!UTy->isFunctionTy());
        if (StructType *STy = dyn_cast<StructType>(UTy)) {
            if (U->getNumOperands() > 0)
                assert(STy->getNumElements() == U->getNumOperands());
            else
                continue;
        }
        for (auto oi = U->op_begin(), oe = U->op_end(); oi != oe; ++oi) {
            Value* O = *oi;
            Type* OTy = O->getType(); // 该常量的类型
            ContainersMap[O] = make_pair(U, oi->getOperandNo());
            string subConstantText = getInstructionText(O);

            Function* FoundF = nullptr; // child constant of Function Pointer
            // Case 1: function address is assigned to a type
            if (Function* F = dyn_cast<Function>(O))
                FoundF = F; // O is function type
            //  a composite-type object (value) is assigned to a
            // field of another composite-type object
            else if (isCompositeType(OTy)) {
                // recognize nested composite types
                User* OU = dyn_cast<User>(O);
                LU.push_back(OU);
            }
            // case2: pointer cast to int, function pointer cast to intptr_t/uintptr_t
            // 1.(int)func 2.(int)&{...} aggregated contant cast to int
            else if (PtrToIntOperator *PIO = dyn_cast<PtrToIntOperator>(O)) {
                Function* FF = dyn_cast<Function>(PIO->getOperand(0));
                if (FF)
                    FoundF = FF;
                else {
                    User* OU = dyn_cast<User>(PIO->getOperand(0)); // 如果指针指向聚合常量
                    if (isa<GlobalVariable>(OU)) {
                        if (OU->getType()->isStructTy())
                            typeCapSet.insert(CommonUtil::typeHash(U->getType()));
                    }
                    else
                        LU.push_back(OU);
                }
            }
            // case3，将函数指针cast到void*或者char*类型
            // 比如 1.(void*)func 2.(int)&{...} 将聚合常量地址cast到void*
            else if (BitCastOperator *CO = dyn_cast<BitCastOperator>(O)) {
                // Virtual functions will always be cast by inserting the first parameter
                Function *CF = dyn_cast<Function>(CO->getOperand(0));
                if (CF)
                    FoundF = CF;
                else {
                    User* OU = dyn_cast<User>(CO->getOperand(0));
                    if (isa<GlobalVariable>(OU)) {
                        if (OU->getType()->isStructTy())
                            typeCapSet.insert(CommonUtil::typeHash(U->getType()));
                    }
                    else
                        LU.push_back(OU);
                }
            }
            // Case 3: a reference (i.e., pointer) of a composite-type
            // object is assigned to a field of another composite-type
            // object
            else if (PointerType *POTy = dyn_cast<PointerType>(OTy)) {
                if (isa<ConstantPointerNull>(O))
                    continue;
                // if the pointer points a composite type, conservatively
                // treat it as a type cap (we cannot get the next-layer type
                // if the type is a cap)
                User* OU = dyn_cast<User>(O);
                // test4.c => struct B b = { .a = &ba };
                if (GlobalVariable* GO = dyn_cast<GlobalVariable>(OU)) {
                    DBG << "subconstant: " << subConstantText << " point to global variable: "
                        << GO->getName().str() << "\n";
                    Type* Ty = POTy->getNonOpaquePointerElementType();
                    // FIXME: take it as a confinement instead of a cap
                    if (Ty->isStructTy()) {
                        typeCapSet.insert(CommonUtil::typeHash(Ty));
                    }

                }
                else
                    LU.push_back(OU);
            }
            else {
                // TODO: Type escaping?
            }

            // Found a function
            if (FoundF && !FoundF->isIntrinsic()) {
                // "llvm.compiler.used" indicates that the linker may touch
                // it, so do not apply MLTA against them
                if (GV->getName() != "llvm.compiler.used")
                    StoredFuncs.insert(FoundF);

                // Add the function type to all containers
                Value* CV = O;
                set<Value *> Visited_; // to avoid loop
                while (ContainersMap.find(CV) != ContainersMap.end()) {
                    auto Container = ContainersMap[CV];

                    Type* CTy = Container.first->getType();
                    set<size_t> TyHS;
                    TyHS.insert(CommonUtil::typeHash(CTy));
                    for (auto TyH: TyHS)
                        typeIdxFuncsMap[TyH][Container.second].insert(FoundF);

                    Visited_.insert(CV);
                    if (Visited_.find(Container.first) != Visited_.end())
                        break;

                    CV = Container.first;
                }
            }
        }
    }

    return true;
}

// This function precisely collect alias types for general pointers
// collect char*, void* => other pointer type
// FromValue must be callsite receiver。callee is void* type
void MLTAPass::collectAliasStructPtr(Function *F) {
    map<Value*, Value*> &AliasMap = AliasStructPtrMap[F];
    set<Value*> ToErase;
    for (inst_iterator i = inst_begin(F), e = inst_end(F); i != e; ++i) {
        Instruction *I = &*i;
        if (CastInst *CI = dyn_cast<CastInst>(I)) {
            Value* FromV = CI->getOperand(0);
            // TODO: we only consider calls for now
            if (!isa<CallBase>(FromV))
                continue;

            Type* FromTy = FromV->getType();
            Type* ToTy = CI->getType();
            if (Int8PtrTy[F->getParent()] != FromTy)
                continue;
            if (!ToTy->isPointerTy())
                continue;
            if (!isCompositeType(ToTy->getPointerElementType()))
                continue;

            if (AliasMap.find(FromV) != AliasMap.end()) {
                ToErase.insert(FromV);
                continue;
            }
            AliasMap[FromV] = CI;
        }
    }

    for (auto Erase: ToErase)
        AliasMap.erase(Erase);
}

// analyze field => address-taken function
bool MLTAPass::typeConfineInFunction(Function *F) {
    DBG << "analyzing type confine in function: " << F->getName().str() << "\n";
    for (inst_iterator i = inst_begin(F), e = inst_end(F); i != e; ++i) {
        Instruction *I = &*i;
        if (StoreInst *SI = dyn_cast<StoreInst>(I))
            typeConfineInStore(SI);
        else if (CallBase *CI = dyn_cast<CallBase>(I)) {
            Function* CF = CommonUtil::getBaseFunction(CI->getCalledOperand()); // CF为被调用的函数
            for (User::op_iterator OI = CI->op_begin(), OE = CI->op_end(); OI != OE; ++OI) {
                if (Function* FF = dyn_cast<Function>(*OI)) {
                    if (FF->isIntrinsic())
                        continue;
                    if (CI->isIndirectCall()) {
                        confineTargetFunction(*OI, FF);
                        continue;
                    }
                    // not indirect-call
                    if (!CF)
                        continue;
                    // call target
                    if (CF->isDeclaration())
                        CF = Ctx->GlobalFuncMap[CF->getGUID()];
                    if (!CF)
                        continue;
                    if (Argument* Arg = CommonUtil::getParamByArgNo(CF, OI->getOperandNo())) {
                        for (auto U: Arg->users()) {
                            if (StoreInst* _SI = dyn_cast<StoreInst>(U))
                                confineTargetFunction(_SI->getPointerOperand(), FF);
                            else if (isa<BitCastOperator>(U))
                                confineTargetFunction(U, FF);
                        }
                    }
                }
            }
        }
    }

    return true;
}

void MLTAPass::typeConfineInStore(StoreInst* SI) {
    Value* PO = SI->getPointerOperand();
    Value* VO = SI->getValueOperand();

    // store a function
    Function* CF = getBaseFunction(VO->stripPointerCasts());
    if (!CF)
        return;
    // ToDo: verify this is F or CF
    if (CF->isIntrinsic())
        return;

    confineTargetFunction(PO, CF);
}

// cast有3种情况：
// 1.store ptr value 2.struct assignment 3.cast type1->type2
bool MLTAPass::typePropInFunction(Function *F) {
    // Two cases for propagation: store and cast.
    // For store, LLVM may use memcpy
    set<User*> CastSet;
    for (inst_iterator i = inst_begin(F), e = inst_end(F); i != e; ++i) {
        Instruction *I = &*i;
        Value *PO = nullptr, *VO = nullptr;

        // case1: store
        // *PO = VO
        if (StoreInst *SI = dyn_cast<StoreInst>(I)) {
            PO = SI->getPointerOperand();
            VO = SI->getValueOperand();
            DBG << "store inst: " << getInstructionText(SI) << "\n";
        }
        else if (CallBase *CI = dyn_cast<CallBase>(I)) {
            Function* CF = CommonUtil::getBaseFunction(CI->getCalledOperand()); // called function
            if (CF) {
                // LLVM may optimize struct assignment into a call to
                // intrinsic memcpy
                if (CF->getName() == "llvm.memcpy.p0i8.p0i8.i64") {
                    PO = CI->getOperand(0);
                    VO = CI->getOperand(1);
                    DBG << "memcpy store: " << getInstructionText(CI) << "\n";
                }
            }
        }
        // *PO = VO
        if (PO && VO) {
            // TODO: if VO is a global with an initializer, this should be
            // taken as a confinement instead of propagation, which can
            // improve the precision
            if (isa<ConstantAggregate>(VO) || isa<ConstantData>(VO))
                continue;

            list<typeidx_t> TyList;
            Value* NextV = nullptr;
            set<Value*> Visited;
            nextLayerBaseType(VO, TyList, NextV, Visited);
            if (!TyList.empty()) {
                for (auto TyIdx : TyList)
                    propagateType(PO, TyIdx.first, TyIdx.second);
                continue;
            }

            Visited.clear();
            Type* BTy = getBaseType(VO, Visited);
            // Composite type，llvm.memcpy
            if (BTy) {
                propagateType(PO, BTy);
                continue;
            }

            Type* FTy = getFuncPtrType(VO->stripPointerCasts());
            // Function-pointer type
            if (FTy) {
                if (!getBaseFunction(VO)) {
                    // FTy cast => PO
                    propagateType(PO, FTy);
                    // PO takes function pointer variable instead of function constant, should be deemed escaped.
                    escapeFuncPointer(PO, I);
                    continue;
                }
                else
                    continue;
            }
            // skip if VO not a pointer
            if (!VO->getType()->isPointerTy())
                continue;
            else
                // General-pointer type for escaping
                escapeType(PO);
        }


        // case3: Handle casts
        if (CastInst *CastI = dyn_cast<CastInst>(I))
            // Record the cast, handle later
            CastSet.insert(CastI);

        // Operands of instructions can be BitCastOperator
        for (User::op_iterator OI = I->op_begin(), OE = I->op_end();
             OI != OE; ++OI) {
            if (BitCastOperator *CO = dyn_cast<BitCastOperator>(*OI))
                CastSet.insert(CO);
        }
    }

    for (User* Cast: CastSet) {
        // TODO: we may not need to handle casts as casts are already
        // stripped out in confinement and propagation analysis. Also for
        // a function pointer to propagate, it is supposed to be stored
        // in memory.

        // The conservative escaping policy can be optimized，cast from struct type1 to struct type2
        Type* FromTy = Cast->getOperand(0)->getType();
        Type* ToTy = Cast->getType();

        // Update escaped-type set
        if (FromTy->isPointerTy() && ToTy->isPointerTy()) {
            // 可能有多层指针
            Type* EFromTy = FromTy->getPointerElementType();
            while (EFromTy->isPointerTy())
                EFromTy = EFromTy->getPointerElementType();

            Type *EToTy = ToTy->getPointerElementType();
            while (EToTy->isPointerTy())
                EToTy = EToTy->getPointerElementType();
            // struct pointer to void*, int*, char*
            if (EFromTy->isStructTy() && (EToTy->isVoidTy() || EToTy->isIntegerTy())) {
                typeCapSet.insert(CommonUtil::typeHash(EFromTy));

            }
                // int*, char* to struct*
            else if (EToTy->isStructTy() && (EFromTy->isVoidTy() || EFromTy->isIntegerTy())) {
                typeCapSet.insert(CommonUtil::typeHash(EToTy));
            }
        }

        else if (FromTy->isPointerTy() && ToTy->isIntegerTy()) {
            Type* EFromTy = FromTy->getPointerElementType();
            while (EFromTy->isPointerTy())
                EFromTy = EFromTy->getPointerElementType();

            if (EFromTy->isStructTy())
                typeCapSet.insert(CommonUtil::typeHash(EFromTy));
        }

        else if (ToTy->isPointerTy() && FromTy->isIntegerTy()) {
            Type *EToTy = ToTy->getPointerElementType();
            while (EToTy->isPointerTy())
                EToTy = EToTy->getPointerElementType();

            if (EToTy->isStructTy())
                typeCapSet.insert(CommonUtil::typeHash(EToTy));
        }
    }

    return true;
}

// function F assigned to Value V，equal to, v = F
void MLTAPass::confineTargetFunction(Value* V, Function* F) {
    if (F->isIntrinsic())
        return;
    StoredFuncs.insert(F);

    list<typeidx_t> TyChain;
    bool Complete = true;
    // type hierachy of V
    getBaseTypeChain(TyChain, V, Complete);
    for (auto TI : TyChain) {
        DBG << TI.second << " field of Type: " << getInstructionText(TI.first) <<
            " add function: " << F->getName().str() << "\n";
        typeIdxFuncsMap[CommonUtil::typeHash(TI.first)][TI.second].insert(F);
    }
    if (!Complete) {
        if (!TyChain.empty()) {
            DBG << "add escape type in confining function : " << getInstructionText(TyChain.back().first) << "\n";
            typeCapSet.insert(CommonUtil::typeHash(TyChain.back().first));
        }
        else {
            // ToDo: verify is this necessary.
            // typeCapSet.insert(funcHash(F));
        }

    }
}