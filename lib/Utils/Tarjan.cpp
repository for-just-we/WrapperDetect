//
// Created by prophe cheng on 2025/4/13.
//

#include "Utils/Basic/Tarjan.h"
#include "Utils/Tool/Common.h"


void Tarjan::dfs(Function *F) {
    dfn[F] = low[F] = ts++;
    depVisit.insert(F);
    Stack.push(F);
    inStack[F] = true;
    for (auto iter: depCG[F]) {
        Function *func = iter;
        if (!depVisit.count(func)) {
            dfs(func);
            if (low[func] < low[F])
                low[F] = low[func];
        } else {
            if (dfn[func] < low[F] && inStack[func])
                low[F] = dfn[func];
        }
    }

    Function* temp = NULL;
    sc.clear();
    if (dfn[F] == low[F]) {
        do {
            temp = Stack.top();
            sc.push_back(temp);
            Stack.pop();
            inStack[temp] = false;
        } while (temp != F);

        SCC.push_back(sc);
        int n = sc.size();
        if (n > biggest) biggest = n;
        if (sc.size() == 1 && depCG[sc.back()].count(sc.back())) {
            rec++;
        }
        sccSize += sc.size();
    }
}

void Tarjan::getSCC(vector<vector<Function*>> &ans) {
    int count = 0;
    for (auto iter : funcs) {
        Function *cur = iter;
        if (depVisit.count(cur))
            continue;
        dfs(cur);
        count++;
    }
    ans = SCC;
    OP << "partition size : " << count << ", sccSize = " << sccSize << ", rec size = " << rec << ", biggest = "
           << biggest << "\n";
}