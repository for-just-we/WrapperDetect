//
// Created on 2025/4/13.
//

#ifndef WRAPPERDETECT_TARJAN_H
#define WRAPPERDETECT_TARJAN_H

#include <llvm/IR/Function.h>

#include <unordered_set>
#include <stack>
#include <vector>

using namespace std;
using namespace llvm;

// 分析强连通分量并按照逆拓扑序填入SCC中
// 假如有图: 1 -> 2, 2 -> 3, 3 -> 4, 4 -> 2, 4 -> 5
// 分析结束后有SCC为: { {5}, {2, 3, 4}, {1} }
class Tarjan {
private:
    unordered_map<Function*, set<Function*>> depCG;
    unordered_set<Function*> funcs;

    unordered_set<Function*> depVisit;
    stack<Function*> Stack;
    unordered_map<Function*, int> dfn;
    unordered_map<Function*, int> low;
    unordered_map<Function*, bool> inStack;
    vector<Function*> sc;
    vector<vector<Function*>> SCC;
    int ts = 1;
    int sccSize = 0;
    int rec = 0;
    int biggest = 0;

public:
    Tarjan(unordered_map<Function*, set<Function*>> _depCG) {
        depCG = _depCG;
        for (auto item : _depCG) {
            funcs.insert(item.first);
            for (auto callee: item.second)
                funcs.insert(callee);
        }
        depVisit.clear();
    };

    void getSCC(vector<vector<Function*>> &);

    void dfs(Function *F);
};

#endif //WRAPPERDETECT_TARJAN_H
