Allocation Wrapper Detector

The implementation of indirect-call analysis is the same as in [IndirectCallAnalyzer](https://github.com/for-just-we/IndirectCallAnalyzer), further detail can be found on that repo.

The input to this project is single LLVM IR bitcode file. We use option `-g -Xclang -no-opaque-pointers -Xclang -disable-O0-optnone` for compilation and then use `mem2reg` to optimize.

| project | program | simple detection | heuristic detection | Intra-LLM |
| ---- | ---- | ---- | ---- | ---- |
| bash-5.2 | bash | 462 | 3 | 65 |
| curl-8.14.1 | curl | 79 | 15 | 15 |
| git-2.47.0 | git | 391 | 4 | 54 |
| htop-3.3.0 | htop | 65 | 7 | 10 |
| nasm-2.16.03 | nasm | 67 | 20 | 20 |
| openssl-3.4.0 | openssl | 2005 | 1 | 138 |
| perl-5.4.0 | perl | 208 | 3 | 11 |
| ruby-3.3.6 | ruby | 1282 | 26 | 27 |
| teeworlds-0.7.5 | teeworlds | 6 | 4 | 4 |
| teeworlds-0.7.5 | teeworlds_srv | 5 | 4 | 4 |
| tmux-3.5 | tmux | 250 | 0 | 96 |
| vim-9.1.0857 | vim | 308 | 1 | 62 |
| lighttpd1.4 | lighttpd | 77 | 22 | Nan |


side effect:

- store inst: we deem storing pointer type data as a side-effect cause it brings a hard-determined alias relationship.

- load inst: most load operations in the scope of a function could be deemed safe, but if it could flow to a return value, it could cause a side-effect.
