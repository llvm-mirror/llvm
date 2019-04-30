The EVM target
---


-------
## A to-do list for recent EVM target implementation targets
* [ ] Support 256bit data types in EVM LLVM backend. There are some works to be done in the common code. For front-end to mid-end (LLVM IR), the support has been implemented in the master branch of `evm_llvm`. For backend support (machine level): Use `GPR256` type register, and `CImmediate` for arbitrary precision integer support in backend. `MCOperand` needs to be changed to support 256-bit values, probably will add a new type, and use the same approach as in `MCOperand`.
* [ ] Support EVM-specific object file emitting.
* [ ] Unit test cases. We might want to reuse those created by Solidity team and Vyper team.
* [ ] integration with CI services.




