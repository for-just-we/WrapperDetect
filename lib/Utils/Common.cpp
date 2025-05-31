//
// Created by prophe cheng on 2025/4/10.
//

#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Operator.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/Support/Path.h"
#include "llvm/ADT/SmallString.h"

#include <regex>
#include <utility>
#include <sstream>
#include <iostream>
#include <fstream>

#include "Utils/Tool/Common.h"


map<string, set<StringRef>> CommonUtil::elementsStructNameMap;

string getInstructionText(Value* inst) {
    if (auto F = dyn_cast<Function>(inst))
        return F->getName().str();
    string instructionText;
    raw_string_ostream stream(instructionText);
    inst->print(stream);
    stream.flush();
    return instructionText;
}

string getInstructionText(Type* type) {
    string instructionText;
    raw_string_ostream stream(instructionText);
    type->print(stream);
    stream.flush();
    return instructionText;
}

// ToDo：考虑匿名结构体
string CommonUtil::getValidStructName(string structName) {
    // 查找最后一个点的位置
    size_t lastDotPos = structName.find_last_of('.');

    // 如果 structName 中包含 "anno"，直接返回 structName
    if (structName.find("anon") != string::npos)
        return structName;

    // 如果没有点，或者点后面不是数字，则直接返回原始字符串
    if (lastDotPos == string::npos || !isdigit(structName[lastDotPos + 1]))
        return structName;

    // 返回去除数字后缀的字符串
    return structName.substr(0, lastDotPos);
}

string CommonUtil::getValidStructName(StructType *STy) {
    string struct_name = STy->getName().str();
    string valid_struct_name = getValidStructName(struct_name);
    return valid_struct_name;
}


// 获取函数F的第ArgNo个参数对象
Argument* CommonUtil::getParamByArgNo(Function* F, int8_t ArgNo) {
    if (ArgNo >= F->arg_size())
        return nullptr;

    int8_t idx = 0;
    Function::arg_iterator ai = F->arg_begin();
    while (idx != ArgNo) {
        ++ai;
        ++idx;
    }
    return ai;
}

// 从所有模块加载结构体信息，初始化使用
void CommonUtil::LoadElementsStructNameMap(vector<pair<Module*, StringRef>> &Modules) {
    // 遍历所有的模块
    for (auto M : Modules) {
        // 遍历所有非匿名结构体
        for (auto STy : M.first->getIdentifiedStructTypes()) {
            assert(STy->hasName());
            if (STy->isOpaque()) // 必须有定义，不能只是声明
                continue;

            string strSTy = structTyStr(STy); // 基于每个成员变量类型构建该类型的key
            elementsStructNameMap[strSTy].insert(STy->getName());
        }
    }
}

void cleanString(string &str) {
    // process string
    // remove c++ class type added by compiler
    size_t pos = str.find("(%class.");
    if (pos != string::npos) {
        //regex pattern1("\\(\\%class\\.[_A-Za-z0-9]+\\*,?");
        regex pattern("^[_A-Za-z0-9]+\\*,?");
        smatch match;
        string str_sub = str.substr(pos + 8);
        if (regex_search(str_sub, match, pattern)) {
            str.replace(pos + 1, 7 + match[0].length(), "");
        }
    }
    string::iterator end_pos = remove(str.begin(), str.end(), ' ');
    str.erase(end_pos, str.end());
}

// 计算函数F的has值
size_t CommonUtil::funcHash(Function* F, bool withName) {
    hash<string> str_hash;
    string output;

    string sig;
    raw_string_ostream rso(sig);
    // FunctionType包含返回值类型，参数类型，是否支持可变参数 3个信息
    FunctionType *FTy = F->getFunctionType();
    FTy->print(rso);
    // output为FunctionType的tostring
    output = rso.str();

    if (withName)
        output += F->getName();
    // process string
    cleanString(output);

    return str_hash(output);
}


// 获取callsite对应的signature
size_t CommonUtil::callHash(CallInst *CI) {
    auto CB = dyn_cast<CallBase>(CI);

    hash<string> str_hash;
    string sig;
    raw_string_ostream rso(sig);
    // 根据callsite对应的FunctionType计算hash
    FunctionType *FTy = CB->getFunctionType();
    FTy->print(rso);
    string strip_str = rso.str();
    //string strip_str = funcTypeString(FTy);
    cleanString(strip_str);

    return str_hash(strip_str);
}


string CommonUtil::structTyStr(StructType *STy) {
    string ty_str;
    string sig;
    for (auto Ty : STy->elements()) {
        ty_str += to_string(Ty->getTypeID());
    }
    return ty_str;
}

// 计算类型hash,
// 相比原版mlta，我们对structTypeHash做一些调整，参考https://blog.csdn.net/fcsfcsfcs/article/details/119062032
size_t CommonUtil::typeHash(Type *Ty) {
    hash<string> str_hash;
    string sig;
    string ty_str;

    // 如果是结构体类型
    if (auto STy = dyn_cast<StructType>(Ty)) {
        // TODO: Use more but reliable information
        // FIXME: A few cases may not even have a name
        if (STy->hasName()) {
            ty_str = getValidStructName(STy);
            ty_str += ("," + itostr(STy->getNumElements()));
        }
        else {
            string sstr = structTyStr(STy);
            if (elementsStructNameMap.find(sstr) != elementsStructNameMap.end())
                ty_str = elementsStructNameMap[sstr].begin()->str();
        }
    }
    else {
        raw_string_ostream rso(sig);
        Ty->print(rso);
        ty_str = rso.str();
        string::iterator end_pos = remove(ty_str.begin(), ty_str.end(), ' ');
        ty_str.erase(end_pos, ty_str.end());
    }
    return str_hash(ty_str);
}

size_t CommonUtil::hashIdxHash(size_t Hs, int Idx) {
    hash<string> str_hash;
    return Hs + str_hash(to_string(Idx));
}

size_t CommonUtil::typeIdxHash(Type *Ty, int Idx) {
    return hashIdxHash(typeHash(Ty), Idx);
}

int64_t CommonUtil::getGEPOffset(const Value *V, const DataLayout *DL) {
    const GEPOperator *GEP = dyn_cast<GEPOperator>(V);

    int64_t offset = 0;
    const Value *baseValue = GEP->getPointerOperand()->stripPointerCasts();
    if (const ConstantExpr *cexp = dyn_cast<ConstantExpr>(baseValue))
        if (cexp->getOpcode() == Instruction::GetElementPtr)
        {
            // FIXME: this looks incorrect
            offset += getGEPOffset(cexp, DL);
        }
    Type *ptrTy = GEP->getSourceElementType();

    SmallVector<Value *, 4> indexOps(GEP->op_begin() + 1, GEP->op_end());
    // Make sure all indices are constants
    for (unsigned i = 0, e = indexOps.size(); i != e; ++i)
    {
        if (!isa<ConstantInt>(indexOps[i]))
            indexOps[i] = ConstantInt::get(Type::getInt32Ty(ptrTy->getContext()), 0);
    }
    offset += DL->getIndexedOffsetInType(ptrTy, indexOps);
    return offset;
}

bool isAllDigits(const string& s) {
    for (char c : s) {
        if (!isdigit(c))
            return false;
    }
    return !s.empty(); // 空字符串不算数字
}

string removeFuncNumberSuffix(const string& funcName) {
    size_t dotPos = funcName.find_last_of('.');

    // 如果没有 '.'，或者 '.' 是最后一个字符，直接返回原字符串
    if (dotPos == string::npos || dotPos == funcName.length() - 1)
        return funcName;

    // 提取 '.' 后面的部分
    string suffix = funcName.substr(dotPos + 1);

    // 如果后缀全是数字，则删除 '.' 及其后面的部分
    if (isAllDigits(suffix))
        return funcName.substr(0, dotPos);

    // 否则返回原字符串
    return funcName;
}

string getNormalizedPath(const DISubprogram *DIS) {
    StringRef file = DIS->getFilename();
    StringRef dir = DIS->getDirectory();

    SmallString<512> fullPath;
    if (sys::path::is_absolute(file)) {
        fullPath = file;
    } else {
        fullPath = dir;
        sys::path::append(fullPath, file);
    }

    sys::path::remove_dots(fullPath, /*remove_dot_dot=*/true);
    return fullPath.str().str();
}

void dumpAllocationWrapperInfo(const map<Function*, const set<CallInst*>>& function2AllocCalls, GlobalContext* Ctx) {
    for (auto iter: function2AllocCalls) {
        string funcName = removeFuncNumberSuffix(iter.first->getName().str());
        unsigned line = iter.first->getSubprogram()->getLine();
        string fileName = iter.first->getSubprogram()->getFilename().str();
        string funcKey("{ \"ln\": ");
        funcKey.append(itostr(line));
        funcKey.append(R"(, "file": ")") ;
        funcKey.append(fileName);
        funcKey.append("\" }+");
        funcKey.append(funcName);
        for (CallInst* CI: iter.second) {
            // { "ln": 72, "cl": 9, "fl": "nasmlib/alloc.c" }+calloc
            string callLoc = "{ \"ln\": " + itostr(CI->getDebugLoc()->getLine()) +
                             ", \"cl\": " + itostr(CI->getDebugLoc()->getColumn()) +
                             R"(, "fl": ")" + CI->getDebugLoc()->getFilename().str() + "\" }+";
            for (Function* _Callee: Ctx->Callees[CI]) {
                string callKey = callLoc + removeFuncNumberSuffix(_Callee->getName().str());
                OP << funcKey << "|" << callKey << "\n";
            }
        }
    }
}


string strip(const string& s) {
    string result = s;

    // 去除前导空白
    result.erase(result.begin(), find_if(result.begin(), result.end(), [](unsigned char c) {
        return !std::isspace(c);
    }));

    // 去除尾随空白
    result.erase(std::find_if(result.rbegin(), result.rend(), [](unsigned char c) {
        return !std::isspace(c);
    }).base(), result.end());

    return result;
}

string replaceAll(string str, const string& from, const string& to) {
    size_t start_pos = 0;
    while ((start_pos = str.find(from, start_pos)) != string::npos) {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length();
    }
    return str;
}

bool CmpFirst(const string& str, const string target) {
    // 找到第一个 '\n' 的位置
    size_t newline_pos = str.find('\n');
    // 提取第一个部分（可能是整个字符串，如果 '\n' 不存在）
    string first_part = (newline_pos == std::string::npos) ? str: str.substr(0, newline_pos);
    // 转换为小写（C++ 风格）
    string lower_first_part = first_part;
    transform(lower_first_part.begin(), lower_first_part.end(), lower_first_part.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    // 比较是否等于 "yes"
    return lower_first_part == target;
}


void log(const string& file, const vector<string>& curLogs) {
    auto writeLogs = [&](auto &out) {
        for (size_t i = 0; i < curLogs.size(); ++i) {
            out << curLogs[i];
            if (i != curLogs.size() - 1)
                out << "\n";
        }
    };

    if (file == "cout")
        writeLogs(cout);
    else {
        ofstream outFile(file, ios::app);
        if (outFile)
            writeLogs(outFile);
        else
            cerr << "Error: Unable to open file " << file << " for writing." << endl;
    }
}


Function* CommonUtil::getBaseFunction(Value *V) {
    if (Function *F = dyn_cast<Function>(V))
        if (!F->isIntrinsic())
            return F;
    Value *CV = V;
    while (BitCastOperator *BCO = dyn_cast<BitCastOperator>(CV)) {
        Value* O = BCO->getOperand(0);
        if (Function *F = dyn_cast<Function>(O))
            if (!F->isIntrinsic())
                return F;
        CV = O;
    }
    return NULL;
}