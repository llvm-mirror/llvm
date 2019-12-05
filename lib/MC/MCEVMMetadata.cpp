
#include "llvm/MC/MCGenEVMInfo.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCAsmLayout.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/FileSystem.h"
 
using namespace llvm;

static cl::opt<std::string>
EVMMetadataFile("evm_md_file", cl::desc("EVM metadata filename."));
   
void MCGenEVMInfo::Emit(const MCAssembler& Asm, const MCAsmLayout &Layout) {
    if (EVMMetadataFile.empty()) {
        // TODO: change it to an appropriate name
        EVMMetadataFile = "EVMMeta.txt";
    }

    std::error_code EC;
    sys::fs::OpenFlags OpenFlags = sys::fs::OF_None;
    auto FDOut = std::make_unique<ToolOutputFile>(EVMMetadataFile, EC, OpenFlags);

    if (EC) {
        WithColor::error() << EC.message() << '\n';
        return;
    }

    // Now FDOut is ready, emit the labeles and its location to the file.
    // Iterate over this structure and emit tables.
    llvm::raw_fd_ostream &os = FDOut->os();
    os << "[\n";
    for (MCAssembler::const_symbol_iterator it = Asm.symbol_begin(),
                                            ie = Asm.symbol_end();
         it != ie; ++it) {
      os << "\t{ \"SymbolName\": \"";
      os << it->getName();
      os << "\", \"Offset\": \"" << it->getOffset() << "\" }\n";
    }
    os << "]\n";

    FDOut->keep();
}
