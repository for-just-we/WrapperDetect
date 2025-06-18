//
// Created by prophe cheng on 2025/5/23.
//

// LLM-enhanced wrapper analysis

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IRReader/IRReader.h"

#include <iostream>
#include <fstream>
#include <chrono>

// indirect-call analyzer
#include "Passes/CallGraph/FLTAPass.h"
#include "Passes/CallGraph/MLTAPass.h"
#include "Passes/CallGraph/MLTADFPass.h"
#include "Passes/CallGraph/KELPPass.h"

// alloc wrapper analysis
#include "Passes/AllocWrapperDetect/LLM/IntraAWDPass.h"

#include "Utils/Basic/Tarjan.h"
#include "Utils/Basic/Config.h"
#include "Utils/Basic/SourceCodeInfo.h"

#include "LLMQuery/LLMAnalyzer.h"

using namespace llvm;
using namespace std;
using namespace chrono;

// Command line parameters.
cl::list<string> InputFilenames(
        cl::Positional,
        cl::OneOrMore,
        cl::desc("<input bitcode files>"));

// 默认采用FLTA
cl::opt<int> ICallAnalysisType(
        "icall-analysis-type",
        cl::desc("select which call analysis to use: 1 --> FLTA, 2 --> MLTA, 3 --> Data Flow Enhanced MLTA, 4 --> Kelp"),
        cl::NotHidden, cl::init(4));

// wrapper analysis type
cl::opt<int> WrapperAnalysisType(
        "wrapper-analysis-type",
        cl::desc("select which wrapper analysis to use: 1 --> LAWDPass"),
        cl::NotHidden, cl::init(1)
);

// source code file
cl::opt<string> SouceCodeInfoFile(
        "source-info-file",
        cl::desc("file storing source code information, in json format"),
        cl::NotHidden, cl::init("")
        );

// max_type_layer
cl::opt<int> MaxTypeLayer(
        "max-type-layer",
        cl::desc("Multi-layer type analysis for refining indirect-call targets"),
        cl::NotHidden, cl::init(10));

cl::opt<bool> DebugMode(
        "debug",
        cl::desc("debug mode"),
        cl::init(false)
);

// indirect-call结果保存路径
cl::opt<string> IcallOutputFilePath(
        "icall-output-file",
        cl::desc("Indirect-Call Analysis Output file path, better to use absolute path"),
        cl::init(""));

// wrapper结果保存路径
cl::opt<string> WrapperOutputFilePath(
        "wrapper-output-file",
        cl::desc("Wrapper Analysis Output file path"),
        cl::init(""));

// prompt template加载路径
cl::opt<string> PromptTemplateFile(
        "prompt-template-file",
        cl::desc("The resource file which contains prompt template"),
        cl::init("../resources/templates.json"));

// 温度
cl::opt<float> Temperature(
        "temperature",
        cl::desc("temperature passed to LLM"),
        cl::init(-1)
        );

cl::opt<string> Address(
        "addr",
        cl::desc("address of large language model"),
        cl::init("localhost:8989")
        );

cl::opt<unsigned> RetryTime(
        "retry",
        cl::desc("retry time limit for query LLM"),
        cl::init(3)
        );

cl::opt<unsigned> VoteTime(
        "vote",
        cl::desc("vote time"),
        cl::init(5)
        );

cl::opt<string> LogDir(
        "log-dir",
        cl::desc("log path of LLM analysis results"),
        cl::init("cout")
        );

cl::opt<string> WrapperInfoFile(
        "wrapper-info",
        cl::desc("log wrapper information"),
        cl::init("")
        );

GlobalContext GlobalCtx;


// 打印结果
void PrintResults(GlobalContext* GCtx) {
    int TotalTargets = 0;
    // 计算间接调用总共调用的target function数量
    for (auto IC : GCtx->IndirectCallInsts)
        TotalTargets += GCtx->Callees[IC].size();

    int totalsize = 0;
    OP << "\n@@ Total number of final callees: " << totalsize << ".\n";

    OP << "############## Result Statistics ##############\n";
    // cout<<"# Ave. Number of indirect-call targets: \t" << setprecision(5) << AveIndirectTargets<<"\n";
    OP << "# Number of virtual calls: \t\t\t" << GCtx->NumVirtualCalls << "\n";
    OP << "# Number of indirect calls: \t\t\t" << GCtx->IndirectCallInsts.size() << "\n";
    OP << "# Number of indirect calls with targets: \t" << GCtx->NumValidIndirectCalls << "\n";
    OP << "# Number of indirect-call targets: \t\t" << GCtx->NumIndirectCallTargets << "\n";
    OP << "# Number of address-taken functions: \t\t" << GCtx->AddressTakenFuncs.size() << "\n";
    OP << "# Number of multi-layer calls: \t\t\t" << GCtx->NumSecondLayerTypeCalls << "\n";
    OP << "# Number of multi-layer targets: \t\t" << GCtx->NumSecondLayerTargets << "\n";
    OP << "# Number of one-layer calls: \t\t\t" << GCtx->NumFirstLayerTypeCalls << "\n";
    OP << "# Number of one-layer targets: \t\t\t" << GCtx->NumFirstLayerTargets << "\n";
    OP << "# Number of simple indirect calls: \t\t\t" << GCtx->NumSimpleIndCalls << "\n";
    OP << "# Number of confined functions: \t\t\t" << GCtx->NumConfinedFuncs << "\n";

    OP << "# Number of customized alloc Function: \t\t\t" << GCtx->AllocWrapperKeys.size() << "\n";

    // 根据OutputFilePath决定输出方式
    if (!IcallOutputFilePath.empty()) {
        ostream& output = (IcallOutputFilePath == "cout") ? cout : *(new ofstream(IcallOutputFilePath));

        for (auto &curEle: GCtx->Callees) {
            if (curEle.first->isIndirectCall()) {
                totalsize += curEle.second.size();
                FuncSet funcs = curEle.second;

                auto* Scope = cast<DIScope>(curEle.first->getDebugLoc().getScope());
                string callsiteFile = Scope->getFilename().str();
                int line = curEle.first->getDebugLoc().getLine();
                int col = curEle.first->getDebugLoc().getCol();
                string content = callsiteFile + ":" + itostr(line) + ":" + itostr(col) + "|";
                for (Function* func: funcs)
                    content += (func->getName().str() + ",");
                content = content.substr(0, content.size() - 1);
                content += "\n";
                output << content;
            }
        }

        if (IcallOutputFilePath != "cout") {
            static_cast<ofstream&>(output).close();
            delete &output;
        }
    }

    if (!WrapperOutputFilePath.empty()) {
        ostream& output = (WrapperOutputFilePath == "cout") ? cout : *(new ofstream(WrapperOutputFilePath));

        for (const string& wrapperKey: GCtx->AllocWrapperKeys)
            output << wrapperKey << "\n";

        if (WrapperOutputFilePath != "cout") {
            static_cast<ofstream &>(output).close();
            delete &output;
        }
    }
}


int main(int argc, char** argv) {
    // Print a stack trace if we signal out.
    sys::PrintStackTraceOnErrorSignal(argv[0]);
    PrettyStackTraceProgram X(argc, argv);

    llvm_shutdown_obj Y;  // Call llvm_shutdown() on exit.

    cl::ParseCommandLineOptions(argc, argv, "global analysis\n");
    SMDiagnostic Err;

    for (unsigned i = 0; i < InputFilenames.size(); ++i) {
        auto LLVMCtx = new LLVMContext();
        unique_ptr<Module> M = parseIRFile(InputFilenames[i], Err, *LLVMCtx);

        if (M == nullptr) {
            OP << argv[0] << ": error loading file '" << InputFilenames[i] << "'\n";
            continue;
        }

        Module* Module = M.release();
        auto MName = StringRef(strdup(InputFilenames[i].data()));
        GlobalCtx.Modules.push_back(make_pair(Module, MName));
        GlobalCtx.ModuleMaps[Module] = InputFilenames[i];
    }

    if (SouceCodeInfoFile.empty()) {
        OP << "please input valid source code information file\n";
        return 0;
    }

    // 1. 打开 JSON 文件
    std::ifstream file(PromptTemplateFile);
    if (!file.is_open()) {
        OP << "Error: Could not open file: " << PromptTemplateFile << "\n";
        return 1;
    }

    // 2. 解析 JSON 数据
    json jsonData;
    try {
        file >> jsonData; // 读取 JSON 文件到 jsonData
    } catch (const json::parse_error& e) {
        OP << "JSON parse error: " << e.what() << "\n";
        return 1;
    }

    unordered_map<string, FunctionInfo> sourceInfos = parseSourceFileInfo(SouceCodeInfoFile);
    OP << "parse source info done\n";

    auto llmAnalyzer = new LLMAnalyzer(Address, Temperature, RetryTime, VoteTime);

    debug_mode = DebugMode;
    max_type_layer = MaxTypeLayer;

    auto start = high_resolution_clock::now();
    CallGraphPass* CGPass;
    // 进行indirect-call分析
    if (ICallAnalysisType == 1)
        CGPass = new FLTAPass(&GlobalCtx);
    else if (ICallAnalysisType == 2)
        CGPass = new MLTAPass(&GlobalCtx);
    else if (ICallAnalysisType == 3)
        CGPass = new MLTADFPass(&GlobalCtx);
    else if (ICallAnalysisType == 4)
        CGPass = new KELPPass(&GlobalCtx);
    else {
        OP << "unimplemnted analysis type, break\n";
        return 0;
    }
    CGPass->run(GlobalCtx.Modules);
    delete CGPass;
    auto end = high_resolution_clock::now();
    seconds duration = duration_cast<seconds>(end - start);
    OP << "indirect call analysis spent: " << duration.count() << " seconds\n";

    start = high_resolution_clock::now();
    HAWDPass* WDPass;
    if (WrapperAnalysisType == 1)
        WDPass = new IntraAWDPass(&GlobalCtx, sourceInfos, jsonData["summarizing"], llmAnalyzer,
                                  jsonData["intra_sys"], jsonData["intra_user"], LogDir);
    else {
        cout << "unimplemnted wrapper analysis type, break\n";
        return 0;
    }

    WDPass->run(GlobalCtx.Modules);
    if (!WrapperInfoFile.empty())
        dumpAllocationWrapperInfo(WDPass->function2AllocCalls, &GlobalCtx, WrapperInfoFile);

    delete WDPass;
    end = high_resolution_clock::now();
    duration = duration_cast<seconds>(end - start);
    OP << "alloc wrapper analysis spent: " << duration.count() << " seconds\n";

    OP << "LLM Spend: " << llmAnalyzer->totalLLMTime << ", input tokens: " <<
        llmAnalyzer->totalInputTokenNum << ", output tokens: " << llmAnalyzer->totalOutputTokenNum <<
        ", query num: " << llmAnalyzer->totalQueryNum << "\n";

    cout << "| " << duration.count() << " | " << llmAnalyzer->totalLLMTime << " | " << (duration.count() - llmAnalyzer->totalLLMTime) <<
       " | " << llmAnalyzer->totalQueryNum << " | "
       << std::fixed << std::setprecision(1) << static_cast<double>(llmAnalyzer->totalLLMTime) / llmAnalyzer->totalQueryNum <<
       " | " << static_cast<double>(llmAnalyzer->totalInputTokenNum) / 1000.0 <<
       " | " << static_cast<double>(llmAnalyzer->totalOutputTokenNum) / 1000.0 << "\n";

    // 打印分析结果
    PrintResults(&GlobalCtx);
    return 0;
}