//
// Created by prophe on 25-7-17.
//

#ifndef OPTIONS_H
#define OPTIONS_H

#include <llvm/Support/CommandLine.h>
#include <string>

using namespace std;
using namespace llvm;

extern cl::list<string> InputFilenames;
extern cl::opt<int> ICallAnalysisType;
extern cl::opt<int> WrapperAnalysisType;
extern cl::opt<int> MaxTypeLayer;
extern cl::opt<bool> DebugMode;
extern cl::opt<string> IcallOutputFilePath;
extern cl::opt<string> WrapperOutputFilePath;

#endif //OPTIONS_H
