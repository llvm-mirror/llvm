
![EVM LLVM Image](https://user-images.githubusercontent.com/450283/63640209-85cb3c00-c66b-11e9-9610-0c339ae66ac7.png)

Working on smart contracts on EVM these days has a lot of limitations. The programming language, the performance, the cost, the analysis tools, are all limited due to the fact that the EVM platform is a very blockchain specific execution environment. It differs very much from our daily compilers. It is a stack machine; its resources are limited; it does not have a heap space; et cetera, et cetera. Because of those dicrepancies, the blockchain community had to rebuild smart contract infrastructures from scratch. 

But to mature a software infrastructure takes a long time and tremendous efforts. With the fast-acting of blockchain upgrades, smart contract facilities are growing relatively slower than the rest of blockchain facilities. We would like to change that.

With this project, we prove that industry-strong, LLVM compiler frameworks are still compatible for the EVM platform, even though LLVM is not created specifically for such kind of architectures. 


EVM LLVM is an EVM architecture backend for LLVM. With EVM LLVM you can generate EVM binary code with LLVM-based compilers. The project is a codegen backend, it does not assume a language frontend, so you should be able to plug in a new smart contract language frontend to generate EVM binary. However, 

 
At this moment, this project is based on LLVM 10. Some additional features have added to support EVM codegen.



[Technical Paper](https://github.com/etclabscore/evm_llvm/wiki/files/Generating_stack_machine_code_using_LLVM.pdf)

[Wiki](https://github.com/etclabscore/evm_llvm/wiki)
