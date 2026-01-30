//
// Created on 25-9-24.
//

#include <llvm/IR/DebugInfo.h>
#include <llvm/BinaryFormat/Dwarf.h>

#include "Passes/CallGraph/CppCGPass.h"
#include "Utils/Tool/CppUtil.h"

const string pureVirtualFunName = "__cxa_pure_virtual";
const string ztiLabel = "_ZTI";

static const ConstantExpr* isCastConstantExpr(const Value* val) {
    if (const ConstantExpr* ce = dyn_cast<ConstantExpr>(val)) {
        if (ce->getOpcode() == Instruction::BitCast)
            return ce;
    }
    return nullptr;
}

bool CppCGPass::doInitialization(Module* M) {
    KELPPass::doInitialization(M);

    // collect class names in vtables
    for (Module::const_global_iterator I = M->global_begin(), E = M->global_end(); I != E; ++I) {
        const GlobalVariable* GV = &*I;
        if (CppUtil::isValVtbl(GV) && GV->getNumOperands() > 0) {
            const ConstantStruct *vtblStruct = CppUtil::getVtblStruct(GV);
            string className = CppUtil::getClassNameFromVtblObj(GV->getName().str());
            ClassNames.insert(className);

            for (unsigned int ei = 0; ei < vtblStruct->getNumOperands(); ++ei) {
                const ConstantArray* vtbl = dyn_cast<ConstantArray>(vtblStruct->getOperand(ei));
                assert(vtbl && "Element of initializer not an array?");
                for (unsigned i = 0; i < vtbl->getNumOperands(); ++i) {
                    if(const ConstantExpr* ce = isCastConstantExpr(vtbl->getOperand(i))) {
                        const Value* bitcastValue = ce->getOperand(0);
                        if (const  Function* func = dyn_cast<Function>(bitcastValue)) {
                            DemangledName dname = CppUtil::demangle(func->getName().str());
                            ClassNames.insert(dname.className);
                        }
                    }
                }
            }
        }
    }

    // cllect class names and inherit relationship between classes, preserve in string
    for (Module::const_iterator it = M->begin(); it != M->end(); ++it) {
        const Function* F = &*it;
        // if this is constructor or destructor of a class
        if (CppUtil::isConstructor(F) || CppUtil::isDestructor(F)) {
            // collect class name
            DemangledName dname = CppUtil::demangle(F->getName().str());
            DBG << "\t build CHANode for class " + dname.className + "...\n";
            ClassNames.insert(dname.className);

            // collect super class information
            for (Function::const_iterator B = F->begin(), E = F->end(); B != E; ++B) {
                for (BasicBlock::const_iterator I = B->begin(); I != B->end(); ++I) {
                    if (const CallBase* CB = dyn_cast<CallBase>(I))
                        connectInheritEdgeViaCall(F, CB);
                    else if (const StoreInst* SI = dyn_cast<StoreInst>(I))
                        connectInheritEdgeViaStore(F, SI);
                }
            }
        }
    }

    // analyze vtables
    analyzeVTables(M);
    return false;
}

// check whether it calls constructor of other classes
void CppCGPass::connectInheritEdgeViaCall(const Function* F, const CallBase* CB) {
    // should be a direct call
    if (!CB->getCalledFunction())
        return;

    const Function* callee = CB->getCalledFunction();
    DemangledName dname = CppUtil::demangle(F->getName().str());
    if (CppUtil::isConstructor(callee) || CppUtil::isDestructor(callee)) {
        if (CB->arg_size() < 1 || (CB->arg_size() < 2 && CB->paramHasAttr(0, Attribute::StructRet)))
            return;
        const Value* csThisPtr = CppUtil::getVCallThisPtr(CB);
        // must use memory to register optimization
        const Argument* consThisPtr = CppUtil::getConstructorThisPtr(F);
        bool samePtr = CppUtil::isSameThisPtrInConstructor(consThisPtr, csThisPtr);
        // make sure the called constructor has the same this ptr with this constructor
        if (csThisPtr != nullptr && samePtr) {
            DemangledName basename = CppUtil::demangle(callee->getName().str());
            if (!isa<CallBase>(csThisPtr) && !basename.className.empty()) {
                BottomUpClassHierarchyChain[dname.className].insert(basename.className);
                TopDownClassHierarchyChain[basename.className].insert(dname.className);
            }
        }
    }
}

// F is a constructor or destructor
void CppCGPass::connectInheritEdgeViaStore(const Function* F, const StoreInst* SI) {
    DemangledName dname = CppUtil::demangle(F->getName().str());
    if (const ConstantExpr* ce = dyn_cast<ConstantExpr>(SI->getValueOperand())) {
        if (ce->getOpcode() == Instruction::BitCast) {
            const Value* bitcastval = ce->getOperand(0);
            if (const ConstantExpr* bcce = dyn_cast<ConstantExpr>(bitcastval)) {
                if (bcce->getOpcode() == Instruction::GetElementPtr) {
                    const Value* gepval = bcce->getOperand(0);
                    if (CppUtil::isValVtbl(gepval)) {
                        string vtblClassName = CppUtil::getClassNameFromVtblObj(gepval->getName().str());
                        if (!vtblClassName.empty() && dname.className.compare(vtblClassName) != 0){
                            BottomUpClassHierarchyChain[dname.className].insert(vtblClassName);
                            TopDownClassHierarchyChain[vtblClassName].insert(dname.className);
                        }
                    }
                }
            }
        }
    }
}

/*
 * do the following things:
 * 1. initialize virtual Functions for each class
 * 2. mark multi-inheritance classes
 * 3. mark pure abstract classes
 *
 * Layout of VTables:
 *
 * 1. single inheritance
 * class A {...};
 * class B: public A {...};
 * B's vtable: {i8 *null, _ZTI1B, ...}
 *
 * 2. normal multiple inheritance
 * class A {...};
 * class B {...};
 * class C: public A, public B {...};
 * C's vtable: {i8 *null, _ZTI1C, ..., inttoptr xxx, _ZTI1C, ...}
 * "inttoptr xxx" servers as a delimiter for dividing virtual methods inherited
 * from "class A" and "class B"
 *
 * 3. virtual diamond inheritance
 * class A {...};
 * class B: public virtual A {...};
 * class C: public virtual A {...};
 * class D: public B, public C {...};
 * D's vtable: {i8 *null, _ZTI1C, ..., inttoptr xxx, _ZTI1C, i8 *null, ...}
 * there will several "i8 *null" following "inttoptr xxx, _ZTI1C,", and the
 * number of "i8 *null" is the same as the number of virtual methods in
 * "class A"
 */
void CppCGPass::analyzeVTables(Module* M) {
    for (Module::const_global_iterator I = M->global_begin(), E = M->global_end(); I != E; ++I) {
        // vtable variables
        const GlobalValue* globalvalue = dyn_cast<const GlobalValue>(&(*I));
        if (CppUtil::isValVtbl(globalvalue) && globalvalue->getNumOperands() > 0) {
            const ConstantStruct* vtblStruct = CppUtil::getVtblStruct(globalvalue);
            string vtblClassName = CppUtil::getClassNameFromVtblObj(globalvalue->getName().str());
            vtables[vtblClassName] = globalvalue;
            for (unsigned int ei = 0; ei < vtblStruct->getNumOperands(); ++ei) {
                const ConstantArray* vtbl = dyn_cast<ConstantArray>(vtblStruct->getOperand(ei));
                assert(vtbl && "Element of initializer not an array?");

                /*
                 * items in vtables can be classified into 3 categories:
                 * 1. i8* null
                 * 2. i8* inttoptr xxx
                 * 3. i8* bitcast xxx
                 */
                bool pure_abstract = true;
                unsigned i = 0;
                while (i < vtbl->getNumOperands()) {
                    vector<const Function*> virtualFunctions;
                    bool is_virtual = false; // virtual inheritance
                    int null_ptr_num = 0;
                    for (; i < vtbl->getNumOperands(); ++i) {
                        Constant* operand = vtbl->getOperand(i);
                        if (isa<ConstantPointerNull>(operand)) {
                            if (i > 0 && !isa<ConstantPointerNull>(vtbl->getOperand(i-1))) {
                                auto foo = [&is_virtual, &null_ptr_num, &vtbl, &i](const Value* val) {
                                    if (val->getName().str().compare(0, ztiLabel.size(), ztiLabel) == 0) {
                                        is_virtual = true;
                                        null_ptr_num = 1;
                                        while (i+null_ptr_num < vtbl->getNumOperands()) {
                                            if (isa<ConstantPointerNull>(vtbl->getOperand(i+null_ptr_num)))
                                                null_ptr_num++;
                                            else
                                                break;
                                        }
                                    }
                                };
                                if (const ConstantExpr* ce = dyn_cast<ConstantExpr>(vtbl->getOperand(i-1))) {
                                    if(ce->getOpcode() == Instruction::BitCast)
                                        foo(ce->getOperand(0));
                                }
                                // opaque pointer mode
                                else
                                    foo(vtbl->getOperand(i - 1));
                            }
                            continue;
                        }

                        auto foo = [this, &virtualFunctions, &pure_abstract, &vtblClassName](const Value* operand) {
                            if (const Function* f = dyn_cast<Function>(operand)) {
                                addFuncToFuncVector(virtualFunctions, f);
                                if (f->getName().str().compare(pureVirtualFunName) == 0)
                                    pure_abstract &= true;
                                else
                                    pure_abstract &= false;
                                DemangledName dname = CppUtil::demangle(f->getName().str());
                                if (!dname.className.empty() && vtblClassName.compare(dname.className) != 0) {
                                    BottomUpClassHierarchyChain[vtblClassName].insert(dname.className);
                                    TopDownClassHierarchyChain[dname.className].insert(vtblClassName);
                                }
                            }
                            else {
                                if (const GlobalAlias* alias = dyn_cast<GlobalAlias>(operand)) {
                                    const Constant* aliasValue = alias->getAliasee();
                                    if (const Function* aliasFunc = dyn_cast<Function>(aliasValue))
                                        addFuncToFuncVector(virtualFunctions, aliasFunc);
                                    else if (const ConstantExpr *aliasconst = dyn_cast<ConstantExpr>(aliasValue)) {
                                        (void)aliasconst; // Suppress warning of unused variable under release build
                                        assert(aliasconst->getOpcode() == Instruction::BitCast &&
                                               "aliased constantexpr in vtable not a bitcast");
                                        const Function* aliasbitcastfunc = dyn_cast<Function>(aliasconst->getOperand(0));
                                        assert(aliasbitcastfunc &&
                                               "aliased bitcast in vtable not a function");
                                        addFuncToFuncVector(virtualFunctions, aliasbitcastfunc);
                                    }
                                    else
                                        assert(false && "alias not function or bitcast");
                                    pure_abstract &= false;
                                }
                                else if (operand->getName().str().compare(0, ztiLabel.size(), ztiLabel) == 0) {
                                }
                                else {
                                    assert("what else can be in bitcast of a vtable?");
                                }
                            }
                        };

                        /*!
                         * vtable in llvm 16 does not have bitcast:
                         * e.g.,
                         * @_ZTV1B = linkonce_odr dso_local unnamed_addr constant
                         *      { [4 x ptr] } { [4 x ptr] [ptr null, ptr @_ZTI1B, ptr @_ZN1B1fEPi, ptr @_ZN1B1gEPi] }, comdat, align 8
                         * compared to its llvm 13 version:
                         * @_ZTV1B = linkonce_odr dso_local unnamed_addr constant { [4 x i8*] } { [4 x i8*] [i8* null,
                         *      i8* bitcast ({ i8*, i8*, i8* }* @_ZTI1B to i8*), i8* bitcast (void (%class.B*, i32*)* @_ZN1B1fEPi to i8*),
                         *              i8* bitcast (void (%class.B*, i32*)* @_ZN1B1gEPi to i8*)] }, comdat, align 8
                         *
                         * For llvm 13, we need to cast the operand into a constant expr and then process the first operand of that constant expr
                         * For llvm 16, things get simpler. We can directly process each operand
                         *
                         * for inttoptr in llvm 16, the handling method is the same as before
                         */
                        if (const ConstantExpr* ce = dyn_cast<ConstantExpr>(operand)) {
                            unsigned opcode = ce->getOpcode();
                            assert(ce->getNumOperands() == 1 &&
                                   "cast operand num not 1");
                            if (opcode == Instruction::IntToPtr) {
                                ClassHierarchy[vtblClassName] |= MULTI_INHERITANCE;
                                ++i;
                                break;
                            }
                            else if (opcode == Instruction::BitCast)
                                foo(ce->getOperand(0));
                        }
                        else
                            foo(operand);
                    }
                    if (is_virtual && !virtualFunctions.empty()) {
                        for (int j = 0; j < null_ptr_num; ++j) {
                            const Function* fun = virtualFunctions[j];
                            virtualFunctions.insert(virtualFunctions.begin(), fun);
                        }
                    }
                    if (!virtualFunctions.empty())
                        virtualFuncVecs[vtblClassName].push_back(virtualFunctions);

                }
                if (pure_abstract == true)
                    ClassHierarchy[vtblClassName] |= PURE_ABSTRACT;
            }
        }
    }
}

void CppCGPass::addFuncToFuncVector(vector<const Function*> &v, const Function* fun) {
    if (CppUtil::isCPPThunkFunction(fun)) {
        if (const Function* tf = CppUtil::getThunkTarget(fun))
            v.push_back(tf);
    }
    else
        v.push_back(fun);
}

void CppCGPass::analyzeVirtualCall(CallBase* CB, FuncSet* FS) {
    // 获取this指针
//    const Value* vtable = CppUtil::getVCallVtblPtr(CB);
//    set<const GlobalValue*> vtbls;
//
//    // 根据this指针获取class
//    const Value* thisPtr = CppUtil::getVCallThisPtr(CB);
//    Type* thisType = thisPtr->getType();
//    assert(thisType->isPointerTy() && "this ptr should be a point\n");
//    Type* classType = thisType->getNonOpaquePointerElementType();
//    string typeName = classType->getStructName().str();
//    getVFnsFromVtbls(CB, vtbls, FS);
}