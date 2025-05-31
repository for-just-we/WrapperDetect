Allocation Wrapper Detector

The implementation of indirect-call analysis is the same as in [IndirectCallAnalyzer](https://github.com/for-just-we/IndirectCallAnalyzer), further detail can be found on that repo.

The input to this project is single LLVM IR bitcode file. We use option `-g -Xclang -no-opaque-pointers -Xclang -disable-O0-optnone` for compilation and then use `mem2reg` to optimize.

| project | program | simple detection | heuristic detection | Intra-LLM |
| ---- | ---- | ---- | ---- | ---- |

| bash-5.2 | bash | 449 | 3 | 65 |
| git-2.47.0 | git | 384 | 3 | 54 |
| htop-3.3.0 | htop | 64 | 7 | 10 |
| nanomq-0.22.10 | nanomq | 155 | 21 | 56 |
| nanomq-0.22.10 | nanomq_cli | 95 | 14 | 44 |
| nanomq-0.22.10 | nngcat | 49 | 4 | 34 |
| nasm-2.16.03 | nasm | 67 | 20 | 20 |
| nasm-2.16.03 | ndisasm | 8 | 7 | - |
| openssl-3.4.0 | openssl | 1866 | 1 | 137 |
| perl-5.4.0 | perl | 204 | 3 | 11 |
| php-8.3.13 | php | 700 | 6 | Nan |
| php-8.3.13 | php-cgi | 657 | 5 | Nan |
| php-8.3.13 | php-dbg | 658 | 5 | Nan |
| ruby-3.3.6 | ruby | 1281 | 26 | 27 |
| teeworlds-0.7.5 | teeworlds | 6 | 4 | 4 |
| teeworlds-0.7.5 | teeworlds_srv | 5 | 4 | 4 |
| tmux-3.5 | tmux | 250 | 0 | 96 |
| vim-9.1.0857 | vim | 308 | 1 | 63 |
| wine-9.22 | widl | 93 | 16 | 20 |
| wine-9.22 | winebuild | 30 | 8 | 11 |
| wine-9.22 | winedump | 26 | 11 | 14 |
| wine-9.22 | winegcc | 20 | 6 | 11 |
| wine-9.22 | wineserver | 119 | 14 | 14 |
| wine-9.22 | wmc | 25 | 10 | 13 |
| wine-9.22 | wrc | 105 | 34 | 42 |


side effect:

- store inst: we deem storing pointer type data as side-effect cause it brings hard-determined alias relationship.

- load inst: most load operations in scope of a function could be deemed safe, but if it could flow to a return value, it could cause side-effect.