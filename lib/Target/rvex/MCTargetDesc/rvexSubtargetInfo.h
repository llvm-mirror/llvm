#ifndef rvexSUBTARGETINFO_H
#define rvexSUBTARGETINFO_H

#include "llvm/ADT/StringRef.h"

namespace llvm {
  class MCSubtargetInfo;

  void InitrvexMCSubtargetInfo(MCSubtargetInfo *II, StringRef TT, StringRef CPU, StringRef FS);
  void rvexUpdateSubtargetInfoFromConfig();

  bool rvexIsGeneric();
} // End llvm namespace 

#endif

