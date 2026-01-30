//
// Created on 2025/5/31.
//
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/PrettyStackTrace.h>
#include <llvm/Support/Signals.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/ADT/StringExtras.h>
#include <llvm/IR/DebugInfo.h>
#include <llvm/IRReader/IRReader.h>

#include <iostream>
#include <fstream>
#include <sys/stat.h>

// indirect-call analyzer
#include "Passes/CallGraph/FLTAPass.h"
#include "Passes/CallGraph/MLTAPass.h"
#include "Passes/CallGraph/MLTADFPass.h"
#include "Passes/CallGraph/KELPPass.h"

// alloc wrapper detection
#include "Passes/AllocWrapperDetect/Debug/DebugPass.h"

// Command line parameters.
static cl::list<string> InputFilenames(
        cl::Positional,
        cl::OneOrMore,
        cl::desc("<input bitcode files>"));

// 默认采用FLTA
static cl::opt<int> ICallAnalysisType(
        "icall-analysis-type",
        cl::desc("select which call analysis to use: 1 --> FLTA, 2 --> MLTA, 3 --> Data Flow Enhanced MLTA, 4 --> Kelp, 5 --> Cpp Callgraph Analysis"),
        cl::NotHidden, cl::init(4));

// wrapper analysis type
static cl::opt<int> WrapperAnalysisType(
        "wrapper-analysis-type",
        cl::desc("select which wrapper analysis to use: 1 --> AWDPass, 2 --> BUAWDPass, 3 --> HAWDPass"),
        cl::NotHidden, cl::init(1)
);

// max_type_layer
static cl::opt<int> MaxTypeLayer(
        "max-type-layer",
        cl::desc("Multi-layer type analysis for refining indirect-call targets"),
        cl::NotHidden, cl::init(10));

static cl::opt<bool> DebugMode(
        "debug-mode",
        cl::desc("debug mode"),
        cl::init(false)
);

// indirect-call结果保存路径
static cl::opt<string> IcallOutputFilePath(
        "icall-output-file",
        cl::desc("Indirect-Call Analysis Output file path, better to use absolute path"),
        cl::init(""));

// wrapper结果保存路径
static cl::opt<string> WrapperOutputFilePath(
        "wrapper-output-file",
        cl::desc("Wrapper Analysis Output file path"),
        cl::init(""));

cl::opt<string> PreAnalyzedPath(
        "pre-analyzed-path",
        cl::desc("pre analyzed wrapper path"),
        cl::init(""));

GlobalContext GlobalCtx;

int main(int argc, char** argv) {
    // Print a stack trace if we signal out.
    sys::PrintStackTraceOnErrorSignal(argv[0]);
    PrettyStackTraceProgram X(argc, argv);

    llvm_shutdown_obj Y;  // Call llvm_shutdown() on exit.

    cl::ParseCommandLineOptions(argc, argv, "debug analysis\n");
    SMDiagnostic Err;

    for (unsigned i = 0; i < InputFilenames.size(); ++i) {
        auto LLVMCtx = new LLVMContext();
        unique_ptr<Module> M = parseIRFile(InputFilenames[i], Err, *LLVMCtx);

        if (M == nullptr) {
            OP << argv[0] << ": error loading file '" << InputFilenames[i] << "'\n";
            continue;
        }

        Module *Module = M.release();
        auto MName = StringRef(strdup(InputFilenames[i].data()));
        GlobalCtx.Modules.push_back(make_pair(Module, MName));
        GlobalCtx.ModuleMaps[Module] = InputFilenames[i];
    }

    debug_mode = DebugMode;
    max_type_layer = MaxTypeLayer;
    CallGraphPass *CGPass;
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

    struct stat statbuf{};
    if (stat(PreAnalyzedPath.c_str(), &statbuf))
        exit(-1);

    ifstream file(PreAnalyzedPath);
    string line;

    if (!file.is_open()) {
        OP << "无法打开文件: " << PreAnalyzedPath << "\n";
        exit(-1);
    }

    set<const Function*> preAnalyzed;
    while (getline(file, line)) {
        size_t pos1 = line.find('<');
        size_t pos2 = line.find('<', pos1 + 1);

        if (pos1 != string::npos && pos2 != string::npos) {
            string funcName = line.substr(0, pos1);
            string fileName = line.substr(pos1 + 1, pos2 - pos1 - 1);
            unsigned lineNum = stoi(line.substr(pos2 + 1));

            for (Module::const_iterator F = GlobalCtx.Modules[0].first->begin(),
                    E = GlobalCtx.Modules[0].first->end(); F != E; ++F) {
                const Function* fun = &*F;
                if (removeFuncNumberSuffix(fun->getName().str()) == funcName &&
                    fileName == fun->getSubprogram()->getFilename().str() &&
                    lineNum == fun->getSubprogram()->getLine()) {
                    preAnalyzed.insert(fun);
                }
            }
        }
    }
    file.close();

    OP << "pre-analyzed size: " << preAnalyzed.size() << "\n";
    for (const Function* f: preAnalyzed) {
        OP << "pre-analyzed function: " << removeFuncNumberSuffix(f->getName().str()) << "\n";
    }

    AWDPass* WDPass = new DebugPass(&GlobalCtx, preAnalyzed);
    WDPass->run(GlobalCtx.Modules);
    delete WDPass;
    return 0;
}