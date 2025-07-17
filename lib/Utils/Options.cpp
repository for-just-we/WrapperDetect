//
// Created by prophe on 25-7-17.
//

#include "Utils/Basic/Options.h"

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
        "debug-mode",
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