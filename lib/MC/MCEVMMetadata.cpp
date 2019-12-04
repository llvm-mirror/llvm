
#include "llvm/MC/MCGenEVMInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/FileSystem.h"
 
using namespace llvm;

static cl::opt<std::string>
EVMMetadataFile("evm_md_file", cl::desc("EVM metadata filename."));
   
void MCGenEVMInfo::Emit() {
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
    for (std::pair<MCSymbol*, uint32_t> symbol_pair : this->symbol_locations) {
        FDOut->os() << "[" << symbol_pair.first << ", " << symbol_pair.second << "]\n";
    }

    FDOut->keep();
}
