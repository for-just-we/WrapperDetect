Allocation Wrapper Detector

The implementation of indirect-call analysis is the same as in [IndirectCallAnalyzer](https://github.com/for-just-we/IndirectCallAnalyzer), further detail can be found on that repo.

The input to this project is single LLVM IR bitcode file. We use option `-g -Xclang -no-opaque-pointers -Xclang -disable-O0-optnone` for compilation and then use `mem2reg` to optimize.

| project | program | simple detection | heuristic detection | Intra-LLM |
| ---- | ---- | ---- | ---- | ---- |
| bash-5.2 | bash | 462 | 3 | 65 |
| curl-8.14.1 | curl | 79 | 15 | 15 |
| git-2.47.0 | git | 391 | 4 | 54 |
| htop-3.3.0 | htop | 65 | 7 | 10 |
| nanomq-0.22.10 | nanomq | 155 | 21 | 56 |
| nanomq-0.22.10 | nanomq_cli | 95 | 14 | 44 |
| nasm-2.16.03 | nasm | 67 | 20 | 20 |
| nginx-1.26.2 | nginx | 156 | 0 | Nan |
| openssl-3.4.0 | openssl | 2005 | 1 | 138 |
| perl-5.4.0 | perl | 208 | 3 | 11 |
| postgresql | postgresql | 33 | 9 | Nan |
| php-8.3.13 | php | 728 | 6 | Nan |
| php-8.3.13 | php-cgi | 657 | 5 | Nan |
| php-8.3.13 | php-dbg | 658 | 5 | Nan |
| ruby-3.3.6 | ruby | 1282 | 26 | 27 |
| teeworlds-0.7.5 | teeworlds | 6 | 4 | 4 |
| teeworlds-0.7.5 | teeworlds_srv | 5 | 4 | 4 |
| tmux-3.5 | tmux | 250 | 0 | 96 |
| vim-9.1.0857 | vim | 308 | 1 | 62 |
| wine-9.22 | widl | 98 | 19 | 20 |
| wine-9.22 | winebuild | 30 | 8 | 11 |
| wine-9.22 | winedump | 26 | 11 | 14 |
| wine-9.22 | winegcc | 20 | 6 | 11 |
| wine-9.22 | wineserver | 125 | 14 | 14 |
| wine-9.22 | wmc | 25 | 10 | 13 |
| wine-9.22 | wrc | 105 | 34 | 36 |
| lighttpd1.4 | lighttpd | 77 | 22 | Nan |


side effect:

- store inst: we deem storing pointer type data as side-effect cause it brings hard-determined alias relationship.

- load inst: most load operations in scope of a function could be deemed safe, but if it could flow to a return value, it could cause side-effect.