//
// Created on 2025/4/10.
//

#include "Passes/CallGraph/MLTAPass.h"
#include "Utils/Basic/Config.h"

//
// Implementation
//
pair<Type*, int> typeidx_c(Type* Ty, int Idx) {
    return make_pair(Ty, Idx);
}
pair<size_t, int> hashidx_c(size_t Hash, int Idx) {
    return make_pair(Hash, Idx);
}

bool MLTAPass::getTargetsWithLayerType(size_t TyHash, int Idx, FuncSet &FS) {
    // Get the direct funcset in the current layer, which
    // will be further unioned with other targets from type
    // casting
    if (Idx == -1) {
        for (const auto& FSet : typeIdxFuncsMap[TyHash])
            FS.insert(FSet.second.begin(), FSet.second.end());
    }
    else {
        FS = typeIdxFuncsMap[TyHash][Idx];
        FS.insert(typeIdxFuncsMap[TyHash][-1].begin(), typeIdxFuncsMap[TyHash][-1].end());
    }

    return true;
}

// whether is composite type
bool MLTAPass::isCompositeType(Type *Ty) {
    if (Ty->isStructTy() || Ty->isArrayTy() || Ty->isVectorTy())
        return true;
    return false;
}

Type* MLTAPass::getFuncPtrType(Value *V) {
    Type *Ty = V->getType();
    if (PointerType *PTy = dyn_cast<PointerType>(Ty)) {
        Type *ETy = PTy->getNonOpaquePointerElementType();
        if (ETy->isFunctionTy())
            return ETy;
    }

    return nullptr;
}

// value's base type
Value* MLTAPass::recoverBaseType(Value *V) {
    if (Instruction *I = dyn_cast<Instruction>(V)) {
        map<Value*, Value*> &AliasMap = AliasStructPtrMap[I->getFunction()];
        // int8* cast to other composite pointer type
        if (AliasMap.find(V) != AliasMap.end())
            return AliasMap[V];
    }
    return nullptr;
}

Function* MLTAPass::getBaseFunction(Value *V) {
    if (Function *F = dyn_cast<Function>(V))
        if (!F->isIntrinsic())
            return F;

    Value *CV = V;

    // function pointer cast to other type
    // For example, fptr_int f = (fptr_int)&f1 => IR, store void (i32)* bitcast (void (i64)* @f1 to void (i32)*), void (i32)** %f
    while (BitCastOperator *BCO = dyn_cast<BitCastOperator>(CV)) {
        Value *O = BCO->getOperand(0);
        if (Function *F = dyn_cast<Function>(O))
            if (!F->isIntrinsic())
                return F;
        CV = O;
    }
    return nullptr;
}


void MLTAPass::escapeType(Value *V) {
    list<typeidx_t> TyChain;
    bool Complete = true;
    getBaseTypeChain(TyChain, V, Complete);
    for (auto T : TyChain) {
        DBG << "[Escape] Type: " << *(T.first)<< "; Idx: " << T.second<< "\n";
        typeEscapeSet.insert(CommonUtil::typeIdxHash(T.first, T.second));
    }
}

// var1.f1.f2 = (FromTy)var.
// typeIdxPropMap[type(var1)][f1_idx], typeIdxPropMap[type(var1.f2)][f2_idx] add (FromTy, Idx)
void MLTAPass::propagateType(Value *ToV, Type *FromTy, int Idx) {
    list<typeidx_t> TyChain;
    bool Complete = true;
    getBaseTypeChain(TyChain, ToV, Complete); // 获取ToV的type chain
    DBG << "From type: " << getInstructionText(FromTy) << "\n";
    DBG << "To Value: " << getInstructionText(ToV) << "\n";

    for (auto T : TyChain) {
        if (CommonUtil::typeHash(T.first) == CommonUtil::typeHash(FromTy) && T.second == Idx)
            continue;

        typeIdxPropMap[CommonUtil::typeHash(T.first)][T.second].insert(
                hashidx_c(CommonUtil::typeHash(FromTy), Idx));
        DBG << "[PROP] " << *(FromTy) << ": " << Idx << "\n\t===> " << *(T.first) << " " << T.second << "\n";
    }
}

// This function is to get the base type in the current layer.
// To get the type of next layer (with GEP and Load), use
// nextLayerBaseType() instead.
Type* MLTAPass::getBaseType(Value* V, set<Value*> &Visited) {
    if (!V)
        return nullptr;

    if (Visited.find(V) != Visited.end())
        return nullptr;
    Visited.insert(V);

    Type *Ty = V->getType();

    if (isCompositeType(Ty))
        return Ty;

        // The value itself is a pointer to a composite type
    else if (Ty->isPointerTy()) {
        Type* ETy = Ty->getPointerElementType();
        if (isCompositeType(ETy))
            return ETy;
        else if (Value *BV = recoverBaseType(V))
            return BV->getType()->getPointerElementType();
    }

    if (BitCastOperator *BCO = dyn_cast<BitCastOperator>(V))
        return getBaseType(BCO->getOperand(0), Visited); // return source type

    else if (SelectInst *SelI = dyn_cast<SelectInst>(V))
        // Assuming both operands have same type, so pick the first
        // operand
        return getBaseType(SelI->getTrueValue(), Visited);

    else if (PHINode *PN = dyn_cast<PHINode>(V))
        // TODO: tracking incoming values
        return _getPhiBaseType(PN, Visited);

    else if (LoadInst *LI = dyn_cast<LoadInst>(V))
        return getBaseType(LI->getPointerOperand(), Visited);

    else if (Type *PTy = dyn_cast<PointerType>(Ty)) {
        // ??
    }
    else {
    }
    return nullptr;
}


// Get the chain of base types for V
// Complete: whether the chain's end is not escaping --- it won't
// propagate further
// Chain: value V's type chain， V：value，Complete
bool MLTAPass::getBaseTypeChain(list<typeidx_t> &Chain, Value *V, bool &Complete) {
    Complete = true;
    Value *CV = V, *NextV = nullptr;
    list<typeidx_t> TyList;
    set<Value*> Visited;

    Type* BTy = getBaseType(V, Visited);
    if (BTy) {
        // 0 vs. -1?
        Chain.push_back(typeidx_c(BTy, 0));
    }
    Visited.clear();
    while (nextLayerBaseType(CV, TyList, NextV, Visited))
        CV = NextV;

    for (auto TyIdx : TyList)
        Chain.push_back(typeidx_c(TyIdx.first, TyIdx.second));

    // Checking completeness
    if (!NextV) {
        Complete = false;
    }

    else if (isa<Argument>(NextV) && NextV->getType()->isPointerTy()) {
        Complete = false;
    }

    else {
        for (auto U: NextV->users()) {
            if (StoreInst *SI = dyn_cast<StoreInst>(U)) {
                if (NextV == SI->getPointerOperand()) {
                    Complete = false;
                    break;
                }
            }
        }
        // TODO: other cases like store?
    }

    if (!Chain.empty() && !Complete) {
        DBG << "add escape type in get base chain: " << getInstructionText(Chain.back().first) << "\n";
        typeCapSet.insert(CommonUtil::typeHash(Chain.back().first));
    }

    return true;
}


Type* MLTAPass::_getPhiBaseType(PHINode *PN, set<Value *> &Visited) {
    for (unsigned i = 0, e = PN->getNumIncomingValues(); i != e; ++i) {
        Value *IV = PN->getIncomingValue(i);

        Type *BTy = getBaseType(IV, Visited);
        if (BTy)
            return BTy;
    }

    return nullptr;
}


// Get the composite type of the lower layer. Layers are split by memory loads or GEP
bool MLTAPass::nextLayerBaseType(Value* V, list<typeidx_t> &TyList,
                                 Value* &NextV, set<Value*> &Visited) {
    if (!V || isa<Argument>(V)) {
        NextV = V;
        return false;
    }

    if (Visited.find(V) != Visited.end()) {
        NextV = V;
        return false;
    }
    Visited.insert(V);

    // The only way to get the next layer type: GetElementPtrInst or GEPOperator
    // gep格式为getelementptr inbounds %struct.ST, ptr %s, f1, f2, f3, f4, ...
    if (GEPOperator *GEP = dyn_cast<GEPOperator>(V)) {
        NextV = GEP->getPointerOperand(); // 返回s
        bool ret = getGEPLayerTypes(GEP, TyList);
        if (!ret)
            NextV = nullptr;
        return ret;
    }
    else if (LoadInst* LI = dyn_cast<LoadInst>(V)) {
        NextV = LI->getPointerOperand();
        return nextLayerBaseType(LI->getOperand(0), TyList, NextV, Visited);
    }
    else if (BitCastOperator* BCO = dyn_cast<BitCastOperator>(V)) {
        NextV = BCO->getOperand(0); // 求source type的层次
        return nextLayerBaseType(BCO->getOperand(0), TyList, NextV, Visited);
    }
        // Phi and Select
    else if (PHINode *PN = dyn_cast<PHINode>(V)) {
        // FIXME: tracking incoming values
        bool ret = false;
        set<Value*> NVisited;
        list<typeidx_t> NTyList;
        for (unsigned i = 0, e = PN->getNumIncomingValues(); i != e; ++i) {
            Value *IV = PN->getIncomingValue(i);
            NextV = IV;
            NVisited = Visited;
            NTyList = TyList;
            ret = nextLayerBaseType(IV, NTyList, NextV, NVisited);
            if (NTyList.size() > TyList.size())
                break;
        }
        TyList = NTyList;
        Visited = NVisited;
        return ret;
    }
    else if (SelectInst *SelI = dyn_cast<SelectInst>(V)) {
        // Assuming both operands have same type, so pick the first
        // operand
        NextV = SelI->getTrueValue();
        return nextLayerBaseType(SelI->getTrueValue(), TyList, NextV, Visited);
    }
        // Other unary instructions
        // FIXME: may introduce false positives
    else if (UnaryOperator *UO = dyn_cast<UnaryOperator>(V)) {
        NextV = UO->getOperand(0);
        return nextLayerBaseType(UO->getOperand(0), TyList, NextV, Visited);
    }

    NextV = nullptr;
    return false;
}

bool MLTAPass::getGEPLayerTypes(GEPOperator *GEP, list<typeidx_t> &TyList) {
    Value* PO = GEP->getPointerOperand(); // base结构体变量
    Type* ETy = GEP->getSourceElementType(); // 通常是base结构体类型

    vector<int> Indices;
    list<typeidx_t> TmpTyList;
    // FIXME: handle downcasting: the GEP may get a field outside the base type Or use O0 to avoid this issue
    ConstantInt *ConstI = dyn_cast<ConstantInt>(GEP->idx_begin()->get());
    if (ConstI && ConstI->getSExtValue() != 0) {
        // FIXME: The following is an attempt to handle the intentional
        // out-of-bound access; however, it is not fully working, so I
        // skip it for now
        Instruction *I = dyn_cast<Instruction>(PO);
        Value *BV = recoverBaseType(PO);
        if (BV) {
            ETy = BV->getType()->getPointerElementType();
            APInt Offset (ConstI->getBitWidth(),
                          ConstI->getZExtValue());
            Type *BaseTy = ETy;
            SmallVector<APInt> IndiceV = DLMap[I->getModule()]->getGEPIndicesForOffset(BaseTy, Offset);
            for (auto Idx : IndiceV)
                Indices.push_back(*Idx.getRawData());
        }
        else if (StructType *STy = dyn_cast<StructType>(ETy)) {
            bool OptGEP = false;
            for (auto User: GEP->users()) {
                if (BitCastOperator* BCO = dyn_cast<BitCastOperator>(User)) {
                    OptGEP = true;
                    // TODO: This conservative decision results may cases
                    // disqualifying MLTA. Need an analysis to recover the base
                    // types, or use O0 to avoid the optimization
                    // return false;
                }
            }
        }
    }

    // Indices is index of getelementptr, like getelementptr inbounds %struct.ST, ptr %s, f1, f2, f3, f4 => f1, f2, f3, f4
    if (Indices.empty()) {
        for (auto it = GEP->idx_begin(); it != GEP->idx_end(); it++) {
            ConstantInt *ConstII = dyn_cast<ConstantInt>(it->get());
            if (ConstII)
                Indices.push_back(ConstII->getSExtValue());
            else
                Indices.push_back(-1);
        }
    }

    // ignore the first index, refer to：https://llvm.org/docs/GetElementPtr.html#what-is-the-first-index-of-the-gep-instruction
    for (auto it = Indices.begin() + 1; it != Indices.end(); it++) {
        int Idx = *it;
        TmpTyList.push_front(typeidx_c(ETy, Idx));
        // Continue to parse subty
        Type* SubTy = nullptr;
        if (StructType *STy = dyn_cast<StructType>(ETy))
            SubTy = STy->getElementType(Idx);
        else if (ArrayType *ATy = dyn_cast<ArrayType>(ETy))
            SubTy = ATy->getElementType();
        else if (VectorType *VTy = dyn_cast<VectorType>(ETy))
            SubTy = VTy->getElementType();
        assert(SubTy);
        ETy = SubTy;
    }
    // This is a trouble caused by compiler optimization that
    // eliminates the access path when the index of a field is 0.
    // Conservatively assume a base-struct pointer can serve as a
    // pointer to its first field
    StructType *STy = dyn_cast<StructType>(ETy);
    if (STy && STy->getNumElements() > 0) {
        // Get the type of its first field
        Type *Ty0 = STy->getElementType(0);
        for (auto U : GEP->users()) {
            if (BitCastOperator *BCO = dyn_cast<BitCastOperator>(U)) {
                if (PointerType *PTy = dyn_cast<PointerType>(BCO->getType())) {
                    Type *ToTy = PTy->getPointerElementType();
                    if (Ty0 == ToTy)
                        TmpTyList.push_front(typeidx_c(ETy, 0));
                }
            }
        }
    }

    if (!TmpTyList.empty()) {
        // Reorder
        for (auto TyIdx : TmpTyList)
            TyList.push_back(TyIdx);
        return true;
    }
    else
        return false;
}


bool MLTAPass::getDependentTypes(Type* Ty, int Idx, set<hashidx_t> &PropSet) {
    list<hashidx_t> LT;
    LT.push_back(hashidx_c(CommonUtil::typeHash(Ty), Idx));
    set<hashidx_t> Visited;

    while (!LT.empty()) {
        hashidx_t TI = LT.front();
        LT.pop_front();
        if (Visited.find(TI) != Visited.end())
            continue;
        Visited.insert(TI);

        for (hashidx_t Prop: typeIdxPropMap[TI.first][TI.second]) {
            PropSet.insert(Prop);
            LT.push_back(Prop);
        }
        for (hashidx_t Prop: typeIdxPropMap[TI.first][-1]) {
            PropSet.insert(Prop);
            LT.push_back(Prop);
        }
    }
    return true;
}

void MLTAPass::escapeFuncPointer(Value* PO, Instruction* I) {
    escapeType(PO);
}

// FS = FS1 & FS2
void MLTAPass::intersectFuncSets(FuncSet &FS1, FuncSet &FS2, FuncSet &FS) {
    FS.clear();
    for (auto F : FS1) {
        if (FS2.find(F) != FS2.end())
            FS.insert(F);
    }
}