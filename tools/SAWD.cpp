//
// Created by prophe cheng on 2025/4/9.
//
// Simple Alloc Wrapper Detector

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

// alloc wrapper detection
#include "Passes/AllocWrapperDetect/Heuristic/AWDPass.h"
#include "Passes/AllocWrapperDetect/Heuristic/BUAWDPass.h"
#include "Passes/AllocWrapperDetect/Heuristic/HAWDPass.h"
#include "Passes/AllocWrapperDetect/Heuristic/EHAWDPass.h"



#include "Utils/Basic/Tarjan.h"
#include "Utils/Basic/Config.h"

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
        cl::desc("select which wrapper analysis to use: 1 --> AWDPass, 2 --> BUAWDPass, 3 --> HAWDPass"),
        cl::NotHidden, cl::init(1)
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
    AWDPass* WDPass;
    if (WrapperAnalysisType == 1)
        WDPass = new AWDPass(&GlobalCtx);
    else if (WrapperAnalysisType == 2)
        WDPass = new BUAWDPass(&GlobalCtx);
    else if (WrapperAnalysisType == 3)
        WDPass = new HAWDPass(&GlobalCtx);
    else if (WrapperAnalysisType == 4)
        WDPass = new EHAWDPass(&GlobalCtx);
    else {
        cout << "unimplemnted wrapper analysis type, break\n";
        return 0;
    }
    WDPass->run(GlobalCtx.Modules);
    delete WDPass;
    end = high_resolution_clock::now();
    duration = duration_cast<seconds>(end - start);
    OP << "alloc wrapper analysis spent: " << duration.count() << " seconds\n";

    // 打印分析结果
    PrintResults(&GlobalCtx);
    return 0;
}