#include "EVMBaseInfo.h"
#include "llvm/ADT/ArrayRef.h"

namespace llvm {
namespace EVMSysReg {
#define GET_SysRegsList_IMPL
#include "EVMGenSystemOperands.inc"
} // namespace EVMSysReg
} // namespace llvm
