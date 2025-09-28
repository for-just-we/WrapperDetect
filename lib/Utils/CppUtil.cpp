//
// Created by prophe on 25-9-24.
//
#include <cxxabi.h> // for demangling
#include "Utils/Tool/CppUtil.h"

// label for global vtbl value before demangle
const string vtblLabelAfterDemangle = "vtable for ";

// label for multi inheritance virtual function
const string NVThunkFunLabel = "non-virtual thunk to ";
const string VThunkFuncLabel = "virtual thunk to ";

// label for global vtbl value before demangle
const string vtblLabelBeforeDemangle = "_ZTV";

// label for virtual functions
const string vfunPreLabel = "_Z";

const string clsName = "class.";
const string structName = "struct.";
const string vtableType = "(...)**";

const string znwm = "_Znwm";
const string zn1Label = "_ZN1"; // c++ constructor
const string znstLabel = "_ZNSt";
const string znst5Label = "_ZNSt5"; // _ZNSt5dequeIPK1ASaIS2_EE5frontEv -> deque<A const*, allocator<A const*> >::front()
const string znst12Label = "_ZNSt12"; // _ZNSt12forward_listIPK1ASaIS2_EEC2Ev -> forward_list<A const*, allocator<A const*> >::forward_list()
const string znst6Label = "_ZNSt6"; // _ZNSt6vectorIP1ASaIS1_EEC2Ev -> vector<A*, allocator<A*> >::vector()
const string znst7Label = "_ZNSt7"; // _ZNSt7__cxx114listIPK1ASaIS3_EEC2Ev -> __cxx11::list<A const*, allocator<A const*> >::list()
const string znst14Label = "_ZNSt14"; // _ZNSt14_Fwd_list_baseI1ASaIS0_EEC2Ev -> _Fwd_list_base<A, allocator<A> >::_Fwd_list_base()


const string znkstLabel = "_ZNKSt";
const string znkst5Label = "_ZNKSt15_"; // _ZNKSt15_Deque_iteratorIPK1ARS2_PS2_EdeEv -> _Deque_iterator<A const*, A const*&, A const**>::operator*() const
const string znkst20Label = "_ZNKSt20_"; // _ZNKSt20_List_const_iteratorIPK1AEdeEv -> _List_const_iterator<A const*>::operator*() const

const string znkst23Label = "_ZNKSt23_"; // _ZNKSt23_Rb_tree_const_iteratorISt4pairIKi1AEEptEv -> _List_const_iterator<A const*>::operator*() const


const string znkLabel = "_ZNK";
const string znk9Label = "_ZNK9"; // _ZNK9__gnu_cxx17__normal_iteratorIPK1ASt6vectorIS1_SaIS1_EEEdeEv -> __gnu_cxx::__normal_iterator<A const*, vector<A, allocator<A> > >::operator*() const

const string ztilabel = "_ZTI";
const string ztiprefix = "typeinfo for ";
const string dyncast = "__dynamic_cast";


static bool isOperOverload(const string& name) {
    unsigned leftnum = 0, rightnum = 0;
    string subname = name;
    size_t leftpos, rightpos;
    leftpos = subname.find('<');
    while (leftpos != string::npos)
    {
        subname = subname.substr(leftpos + 1);
        leftpos = subname.find('<');
        leftnum++;
    }
    subname = name;
    rightpos = subname.find('>');
    while (rightpos != string::npos)
    {
        subname = subname.substr(rightpos + 1);
        rightpos = subname.find('>');
        rightnum++;
    }
    return leftnum != rightnum;
}

static string getBeforeParenthesis(const string& name) {
    size_t lastRightParen = name.rfind(')');
    assert(lastRightParen > 0);

    signed paren_num = 1, pos;
    for (pos = lastRightParen - 1; pos >= 0; pos--) {
        if (name[pos] == ')')
            paren_num++;
        if (name[pos] == '(')
            paren_num--;
        if (paren_num == 0)
            break;
    }
    return name.substr(0, pos);
}


static void handleThunkFunction(DemangledName& dname) {
    // when handling multi-inheritance,
    // the compiler may generate thunk functions
    // to perform `this` pointer adjustment
    // they are indicated with `virtual thunk to `
    // and `nun-virtual thunk to`.
    // if the classname starts with part of a
    // demangled name starts with
    // these prefixes, we need to remove the prefix
    // to get the real class name
    static vector<string> thunkPrefixes = {VThunkFuncLabel,NVThunkFunLabel };
    for (unsigned i = 0; i < thunkPrefixes.size(); i++) {
        auto prefix = thunkPrefixes[i];
        if (dname.className.size() > prefix.size() &&
                dname.className.compare(0, prefix.size(), prefix) == 0) {
            dname.className = dname.className.substr(prefix.size());
            dname.isThunkFunc = true;
            return;
        }
    }
}

/// get class name before brackets
/// e.g., for `namespace::A<...::...>::f', we get `namespace::A'
string CppUtil::getBeforeBrackets(const string& name) {
    if (name.empty() || name[name.size() - 1] != '>')
        return name;
    signed bracket_num = 1, pos;
    for (pos = name.size() - 2; pos >= 0; pos--) {
        if (name[pos] == '>')
            bracket_num++;
        if (name[pos] == '<')
            bracket_num--;
        if (bracket_num == 0)
            break;
    }
    return name.substr(0, pos);
}


/// strip off brackets and namespace from classname
/// e.g., for `namespace::A<...::...>::f', we get `A' by stripping off namespace and <>
void CppUtil::stripBracketsAndNamespace(DemangledName& dname) {
    dname.funcName = getBeforeBrackets(dname.funcName);
    dname.className = getBeforeBrackets(dname.className);
    size_t colon = dname.className.rfind("::");
    if (colon == string::npos)
        dname.className = getBeforeBrackets(dname.className);
    // strip off namespace
    else
        dname.className = getBeforeBrackets(dname.className.substr(colon + 2));
}

/*
 * input: _ZN****
 * after abi::__cxa_demangle:
 * namespace::A<...::...>::f<...::...>(...)
 *                       ^
 *                    delimiter
 *
 * step1: getBeforeParenthesis
 * namespace::A<...::...>::f<...::...>
 *
 * step2: getBeforeBrackets
 * namespace::A<...::...>::f
 *
 * step3: find delimiter
 * namespace::A<...::...>::
 *                       ^
 *
 * className: namespace::A<...::...>
 * functionName: f<...::...>
 */
DemangledName CppUtil::demangle(const string& name) {
    DemangledName dname;
    dname.isThunkFunc = false;

    signed status;
    char* realname = abi::__cxa_demangle(name.c_str(), 0, 0, &status);
    if (realname == nullptr) {
        dname.className = "";
        dname.funcName = "";
    }
    else {
        string realnameStr = string(realname);
        string beforeParenthesis = getBeforeParenthesis(realnameStr);
        if (beforeParenthesis.find("::") == string::npos || isOperOverload(beforeParenthesis)) {
            dname.className = "";
            dname.funcName = "";
        }
        else {
            string beforeBracket = getBeforeBrackets(beforeParenthesis);
            size_t colon = beforeBracket.rfind("::");
            if (colon == string::npos) {
                dname.className = "";
                dname.funcName = "";
            }
            else {
                dname.className = beforeParenthesis.substr(0, colon);
                dname.funcName = beforeParenthesis.substr(colon + 2);
            }
        }
        free(realname);
    }

    handleThunkFunction(dname);

    return dname;
}


bool CppUtil::isConstructor(const Function* F) {
    if (F->isDeclaration())
        return false;
    string funcName = F->getName().str();
    if (funcName.compare(0, vfunPreLabel.size(), vfunPreLabel) != 0)
        return false;
    DemangledName dname = demangle(funcName.c_str());
    if (dname.className.size() == 0)
        return false;
    stripBracketsAndNamespace(dname);
    /// TODO: on mac os function name is an empty string after demangling
    return dname.className.size() > 0 &&
           dname.className.compare(dname.funcName) == 0;
}

bool CppUtil::isDestructor(const Function* F) {
    if (F->isDeclaration())
        return false;
    string funcName = F->getName().str();
    if (funcName.compare(0, vfunPreLabel.size(), vfunPreLabel) != 0)
        return false;
    DemangledName dname = CppUtil::demangle(funcName.c_str());
    if (dname.className.size() == 0)
        return false;
    stripBracketsAndNamespace(dname);
    return (dname.className.size() > 0 && dname.funcName.size() > 0 &&
            dname.className.size() + 1 == dname.funcName.size() &&
            dname.funcName.compare(0, 1, "~") == 0 &&
            dname.className.compare(dname.funcName.substr(1)) == 0);
}


const Value* CppUtil::getVCallThisPtr(const CallBase* cs) {
    if (cs->paramHasAttr(0, Attribute::StructRet))
        return cs->getArgOperand(1);
    return cs->getArgOperand(0);
}

bool CppUtil::isValVtbl(const Value* val) {
    if (!isa<GlobalVariable>(val))
        return false;
    string valName = val->getName().str();
    return valName.compare(0, vtblLabelBeforeDemangle.size(),
                           vtblLabelBeforeDemangle) == 0;
}

string CppUtil::getClassNameFromVtblObj(const string& vtblName) {
    string className = "";
    signed status;
    char* realname = abi::__cxa_demangle(vtblName.c_str(), 0, 0, &status);
    if (realname != nullptr){
        string realnameStr = string(realname);
        if (realnameStr.compare(0, vtblLabelAfterDemangle.size(),
                                vtblLabelAfterDemangle) == 0)
            className = realnameStr.substr(vtblLabelAfterDemangle.size());
        free(realname);
    }
    return className;
}

const Argument* CppUtil::getConstructorThisPtr(const Function* fun) {
    assert((isConstructor(fun) || isDestructor(fun)) && "not a constructor?");
    assert(fun->arg_size() >= 1 && "argument size >= 1?");
    const Argument* thisPtr = &*(fun->arg_begin());
    return thisPtr;
}


/*!
 * Given a inheritance relation B is a child of A
 * We assume B::B(thisPtr1){ A::A(thisPtr2) } such that thisPtr1 == thisPtr2
 * In the following code thisPtr1 is "%class.B1* %this" and thisPtr2 is
 * "%class.A* %0".
 *
 *
 * define linkonce_odr dso_local void @B1::B1()(%class.B1* %this) unnamed_addr #6 comdat
 *   %this.addr = alloca %class.B1*, align 8
 *   store %class.B1* %this, %class.B1** %this.addr, align 8
 *   %this1 = load %class.B1*, %class.B1** %this.addr, align 8
 *   %0 = bitcast %class.B1* %this1 to %class.A*
 *   call void @A::A()(%class.A* %0)
 */
bool CppUtil::isSameThisPtrInConstructor(const Argument* thisPtr1, const Value* thisPtr2) {
    if (thisPtr1 == thisPtr2)
        return true;
    for (const Value* thisU : thisPtr1->users()) {
        if (const StoreInst* store = dyn_cast<StoreInst>(thisU)) {
            for (const Value* storeU : store->getPointerOperand()->users()) {
                if (const LoadInst* load = dyn_cast<LoadInst>(storeU)) {
                    if (load->getNextNode() && isa<CastInst>(load->getNextNode()))
                        return cast<CastInst>(load->getNextNode()) == thisPtr2->stripPointerCasts();
                }
            }
        }
    }
    return false;
}


const ConstantStruct* CppUtil::getVtblStruct(const GlobalValue* vtbl) {
    const ConstantStruct* vtblStruct = dyn_cast<ConstantStruct>(vtbl->getOperand(0));
    assert(vtblStruct && "Initializer of a vtable not a struct?");
    if (vtblStruct->getNumOperands() == 2 && isa<ConstantStruct>(vtblStruct->getOperand(0)) &&
        vtblStruct->getOperand(1)->getType()->isArrayTy())
        return cast<ConstantStruct>(vtblStruct->getOperand(0));

    return vtblStruct;

}


bool CppUtil::isCPPThunkFunction(const Function* F) {
    DemangledName dname = demangle(F->getName().str());
    return dname.isThunkFunc;
}


Function* CppUtil::getThunkTarget(const Function* F) {
    Function* ret = nullptr;
    for (auto& bb: *F) {
        for (auto& inst: bb) {
            if (const CallBase* callbase = dyn_cast<CallBase>(&inst))
                ret = callbase->getCalledFunction();
        }
    }

    return ret;
}


/*
 * get the ptr "vtable" for a given virtual callsite:
 * %vtable = load ...
 * %vfn = getelementptr %vtable, idx
 * %x = load %vfn
 * call %x (...)
 */
const Value* CppUtil::getVCallVtblPtr(const CallBase* cs) {
    const LoadInst* loadInst = dyn_cast<LoadInst>(cs->getCalledOperand());
    assert(loadInst != nullptr);
    const Value* vfuncptr = loadInst->getPointerOperand();
    const GetElementPtrInst* gepInst = dyn_cast<GetElementPtrInst>(vfuncptr);
    assert(gepInst != nullptr);
    const Value* vtbl = gepInst->getPointerOperand();
    return vtbl;
}