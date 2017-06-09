//===- LLVMOutputStyle.cpp ------------------------------------ *- C++ --*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "LLVMOutputStyle.h"

#include "CompactTypeDumpVisitor.h"
#include "StreamUtil.h"
#include "llvm-pdbdump.h"

#include "llvm/DebugInfo/CodeView/CVTypeVisitor.h"
#include "llvm/DebugInfo/CodeView/DebugChecksumsSubsection.h"
#include "llvm/DebugInfo/CodeView/DebugCrossExSubsection.h"
#include "llvm/DebugInfo/CodeView/DebugCrossImpSubsection.h"
#include "llvm/DebugInfo/CodeView/DebugFrameDataSubsection.h"
#include "llvm/DebugInfo/CodeView/DebugInlineeLinesSubsection.h"
#include "llvm/DebugInfo/CodeView/DebugLinesSubsection.h"
#include "llvm/DebugInfo/CodeView/DebugStringTableSubsection.h"
#include "llvm/DebugInfo/CodeView/DebugSubsectionVisitor.h"
#include "llvm/DebugInfo/CodeView/DebugSymbolsSubsection.h"
#include "llvm/DebugInfo/CodeView/DebugUnknownSubsection.h"
#include "llvm/DebugInfo/CodeView/EnumTables.h"
#include "llvm/DebugInfo/CodeView/LazyRandomTypeCollection.h"
#include "llvm/DebugInfo/CodeView/Line.h"
#include "llvm/DebugInfo/CodeView/SymbolDumper.h"
#include "llvm/DebugInfo/CodeView/TypeDatabaseVisitor.h"
#include "llvm/DebugInfo/CodeView/TypeDumpVisitor.h"
#include "llvm/DebugInfo/CodeView/TypeVisitorCallbackPipeline.h"
#include "llvm/DebugInfo/MSF/MappedBlockStream.h"
#include "llvm/DebugInfo/PDB/Native/DbiModuleDescriptor.h"
#include "llvm/DebugInfo/PDB/Native/DbiStream.h"
#include "llvm/DebugInfo/PDB/Native/EnumTables.h"
#include "llvm/DebugInfo/PDB/Native/GlobalsStream.h"
#include "llvm/DebugInfo/PDB/Native/ISectionContribVisitor.h"
#include "llvm/DebugInfo/PDB/Native/InfoStream.h"
#include "llvm/DebugInfo/PDB/Native/ModuleDebugStream.h"
#include "llvm/DebugInfo/PDB/Native/PDBFile.h"
#include "llvm/DebugInfo/PDB/Native/PublicsStream.h"
#include "llvm/DebugInfo/PDB/Native/RawError.h"
#include "llvm/DebugInfo/PDB/Native/TpiHashing.h"
#include "llvm/DebugInfo/PDB/Native/TpiStream.h"
#include "llvm/DebugInfo/PDB/PDBExtras.h"
#include "llvm/Object/COFF.h"
#include "llvm/Support/BinaryStreamReader.h"
#include "llvm/Support/FormatVariadic.h"

#include <unordered_map>
#include <cstring>
using namespace llvm;
using namespace llvm::codeview;
using namespace llvm::msf;
using namespace llvm::pdb;

namespace {
struct PageStats {
  explicit PageStats(const BitVector &FreePages)
      : Upm(FreePages), ActualUsedPages(FreePages.size()),
        MultiUsePages(FreePages.size()), UseAfterFreePages(FreePages.size()) {
    const_cast<BitVector &>(Upm).flip();
    // To calculate orphaned pages, we start with the set of pages that the
    // MSF thinks are used.  Each time we find one that actually *is* used,
    // we unset it.  Whichever bits remain set at the end are orphaned.
    OrphanedPages = Upm;
  }

  // The inverse of the MSF File's copy of the Fpm.  The basis for which we
  // determine the allocation status of each page.
  const BitVector Upm;

  // Pages which are marked as used in the FPM and are used at least once.
  BitVector ActualUsedPages;

  // Pages which are marked as used in the FPM but are used more than once.
  BitVector MultiUsePages;

  // Pages which are marked as used in the FPM but are not used at all.
  BitVector OrphanedPages;

  // Pages which are marked free in the FPM but are used.
  BitVector UseAfterFreePages;
};

class C13RawVisitor : public DebugSubsectionVisitor {
public:
  C13RawVisitor(ScopedPrinter &P, LazyRandomTypeCollection &TPI,
                LazyRandomTypeCollection &IPI)
      : P(P), TPI(TPI), IPI(IPI) {}

  Error visitUnknown(DebugUnknownSubsectionRef &Unknown) override {
    if (!opts::checkModuleSubsection(opts::ModuleSubsection::Unknown))
      return Error::success();
    DictScope DD(P, "Unknown");
    P.printHex("Kind", static_cast<uint32_t>(Unknown.kind()));
    ArrayRef<uint8_t> Data;
    BinaryStreamReader Reader(Unknown.getData());
    consumeError(Reader.readBytes(Data, Reader.bytesRemaining()));
    P.printBinaryBlock("Data", Data);
    return Error::success();
  }

  Error visitLines(DebugLinesSubsectionRef &Lines,
                   const DebugSubsectionState &State) override {
    if (!opts::checkModuleSubsection(opts::ModuleSubsection::Lines))
      return Error::success();

    DictScope DD(P, "Lines");

    P.printNumber("RelocSegment", Lines.header()->RelocSegment);
    P.printNumber("RelocOffset", Lines.header()->RelocOffset);
    P.printNumber("CodeSize", Lines.header()->CodeSize);
    P.printBoolean("HasColumns", Lines.hasColumnInfo());

    for (const auto &L : Lines) {
      DictScope DDDD(P, "FileEntry");

      if (auto EC = printFileName("FileName", L.NameIndex, State))
        return EC;

      for (const auto &N : L.LineNumbers) {
        DictScope DDD(P, "Line");
        LineInfo LI(N.Flags);
        P.printNumber("Offset", N.Offset);
        if (LI.isAlwaysStepInto())
          P.printString("StepInto", StringRef("Always"));
        else if (LI.isNeverStepInto())
          P.printString("StepInto", StringRef("Never"));
        else
          P.printNumber("LineNumberStart", LI.getStartLine());
        P.printNumber("EndDelta", LI.getLineDelta());
        P.printBoolean("IsStatement", LI.isStatement());
      }
      for (const auto &C : L.Columns) {
        DictScope DDD(P, "Column");
        P.printNumber("Start", C.StartColumn);
        P.printNumber("End", C.EndColumn);
      }
    }

    return Error::success();
  }

  Error visitFileChecksums(DebugChecksumsSubsectionRef &Checksums,
                           const DebugSubsectionState &State) override {
    if (!opts::checkModuleSubsection(opts::ModuleSubsection::FileChecksums))
      return Error::success();

    DictScope DD(P, "FileChecksums");
    for (const auto &CS : Checksums) {
      DictScope DDD(P, "Checksum");
      if (auto Result = getNameFromStringTable(CS.FileNameOffset, State))
        P.printString("FileName", *Result);
      else
        return Result.takeError();
      P.printEnum("Kind", uint8_t(CS.Kind), getFileChecksumNames());
      P.printBinaryBlock("Checksum", CS.Checksum);
    }
    return Error::success();
  }

  Error visitInlineeLines(DebugInlineeLinesSubsectionRef &Inlinees,
                          const DebugSubsectionState &State) override {
    if (!opts::checkModuleSubsection(opts::ModuleSubsection::InlineeLines))
      return Error::success();

    DictScope D(P, "InlineeLines");
    P.printBoolean("HasExtraFiles", Inlinees.hasExtraFiles());
    ListScope LS(P, "Lines");
    for (const auto &L : Inlinees) {
      DictScope DDD(P, "Inlinee");
      if (auto EC = printFileName("FileName", L.Header->FileID, State))
        return EC;

      if (auto EC = dumpTypeRecord("Function", L.Header->Inlinee))
        return EC;
      P.printNumber("SourceLine", L.Header->SourceLineNum);
      if (Inlinees.hasExtraFiles()) {
        ListScope DDDD(P, "ExtraFiles");
        for (const auto &EF : L.ExtraFiles) {
          if (auto EC = printFileName("File", EF, State))
            return EC;
        }
      }
    }
    return Error::success();
  }

  Error visitCrossModuleExports(DebugCrossModuleExportsSubsectionRef &CSE,
                                const DebugSubsectionState &State) override {
    if (!opts::checkModuleSubsection(opts::ModuleSubsection::CrossScopeExports))
      return Error::success();

    ListScope D(P, "CrossModuleExports");
    for (const auto &M : CSE) {
      DictScope D(P, "Export");
      P.printHex("Local", M.Local);
      P.printHex("Global", M.Global);
    }
    return Error::success();
  }

  Error visitCrossModuleImports(DebugCrossModuleImportsSubsectionRef &CSI,
                                const DebugSubsectionState &State) override {
    if (!opts::checkModuleSubsection(opts::ModuleSubsection::CrossScopeImports))
      return Error::success();

    ListScope L(P, "CrossModuleImports");
    for (const auto &M : CSI) {
      DictScope D(P, "ModuleImport");
      auto Name = getNameFromStringTable(M.Header->ModuleNameOffset, State);
      if (!Name)
        return Name.takeError();
      P.printString("Module", *Name);
      P.printHexList("Imports", M.Imports);
    }
    return Error::success();
  }

  Error visitFrameData(DebugFrameDataSubsectionRef &FD,
                       const DebugSubsectionState &State) override {
    if (!opts::checkModuleSubsection(opts::ModuleSubsection::FrameData))
      return Error::success();

    ListScope L(P, "FrameData");
    for (const auto &Frame : FD) {
      DictScope D(P, "Frame");
      auto Name = getNameFromStringTable(Frame.FrameFunc, State);
      if (!Name)
        return joinErrors(make_error<RawError>(raw_error_code::invalid_format,
                                               "Invalid Frame.FrameFunc index"),
                          Name.takeError());
      P.printNumber("Rva", Frame.RvaStart);
      P.printNumber("CodeSize", Frame.CodeSize);
      P.printNumber("LocalSize", Frame.LocalSize);
      P.printNumber("ParamsSize", Frame.ParamsSize);
      P.printNumber("MaxStackSize", Frame.MaxStackSize);
      P.printString("FrameFunc", *Name);
      P.printNumber("PrologSize", Frame.PrologSize);
      P.printNumber("SavedRegsSize", Frame.SavedRegsSize);
      P.printNumber("Flags", Frame.Flags);
    }
    return Error::success();
  }

  Error visitSymbols(DebugSymbolsSubsectionRef &Symbols,
                     const DebugSubsectionState &State) override {
    if (!opts::checkModuleSubsection(opts::ModuleSubsection::Symbols))
      return Error::success();
    ListScope L(P, "Symbols");

    // This section should not actually appear in a PDB file, it really only
    // appears in object files.  But we support it here for testing.  So we
    // specify the Object File container type.
    codeview::CVSymbolDumper SD(P, TPI, CodeViewContainer::ObjectFile, nullptr,
                                false);
    for (auto S : Symbols) {
      DictScope LL(P, "");
      if (auto EC = SD.dump(S)) {
        return make_error<RawError>(
            raw_error_code::corrupt_file,
            "DEBUG_S_SYMBOLS subsection contained corrupt symbol record");
      }
    }
    return Error::success();
  }

  Error visitStringTable(DebugStringTableSubsectionRef &Strings,
                         const DebugSubsectionState &State) override {
    if (!opts::checkModuleSubsection(opts::ModuleSubsection::StringTable))
      return Error::success();

    ListScope D(P, "String Table");
    BinaryStreamReader Reader(Strings.getBuffer());
    StringRef S;
    consumeError(Reader.readCString(S));
    while (Reader.bytesRemaining() > 0) {
      consumeError(Reader.readCString(S));
      if (S.empty() && Reader.bytesRemaining() < 4)
        break;
      P.printString(S);
    }
    return Error::success();
  }

private:
  Error dumpTypeRecord(StringRef Label, TypeIndex Index) {
    CompactTypeDumpVisitor CTDV(IPI, Index, &P);
    DictScope D(P, Label);
    if (IPI.contains(Index)) {
      CVType Type = IPI.getType(Index);
      if (auto EC = codeview::visitTypeRecord(Type, CTDV))
        return EC;
    } else {
      P.printString(
          llvm::formatv("Index: {0:x} (unknown function)", Index.getIndex())
              .str());
    }
    return Error::success();
  }
  Error printFileName(StringRef Label, uint32_t Offset,
                      const DebugSubsectionState &State) {
    if (auto Result = getNameFromChecksumsBuffer(Offset, State)) {
      P.printString(Label, *Result);
      return Error::success();
    } else
      return Result.takeError();
  }

  Expected<StringRef>
  getNameFromStringTable(uint32_t Offset, const DebugSubsectionState &State) {
    return State.strings().getString(Offset);
  }

  Expected<StringRef>
  getNameFromChecksumsBuffer(uint32_t Offset,
                             const DebugSubsectionState &State) {
    auto Array = State.checksums().getArray();
    auto ChecksumIter = Array.at(Offset);
    if (ChecksumIter == Array.end())
      return make_error<RawError>(raw_error_code::invalid_format);
    const auto &Entry = *ChecksumIter;
    return getNameFromStringTable(Entry.FileNameOffset, State);
  }

  ScopedPrinter &P;
  LazyRandomTypeCollection &TPI;
  LazyRandomTypeCollection &IPI;
};
}

static void recordKnownUsedPage(PageStats &Stats, uint32_t UsedIndex) {
  if (Stats.Upm.test(UsedIndex)) {
    if (Stats.ActualUsedPages.test(UsedIndex))
      Stats.MultiUsePages.set(UsedIndex);
    Stats.ActualUsedPages.set(UsedIndex);
    Stats.OrphanedPages.reset(UsedIndex);
  } else {
    // The MSF doesn't think this page is used, but it is.
    Stats.UseAfterFreePages.set(UsedIndex);
  }
}

static void printSectionOffset(llvm::raw_ostream &OS,
                               const SectionOffset &Off) {
  OS << Off.Off << ", " << Off.Isect;
}

LLVMOutputStyle::LLVMOutputStyle(PDBFile &File) : File(File), P(outs()) {}

Error LLVMOutputStyle::dump() {
  if (auto EC = dumpFileHeaders())
    return EC;

  if (auto EC = dumpStreamSummary())
    return EC;

  if (auto EC = dumpFreePageMap())
    return EC;

  if (auto EC = dumpStreamBlocks())
    return EC;

  if (auto EC = dumpBlockRanges())
    return EC;

  if (auto EC = dumpStreamBytes())
    return EC;

  if (auto EC = dumpStringTable())
    return EC;

  if (auto EC = dumpInfoStream())
    return EC;

  if (auto EC = dumpTpiStream(StreamTPI))
    return EC;

  if (auto EC = dumpTpiStream(StreamIPI))
    return EC;

  if (auto EC = dumpDbiStream())
    return EC;

  if (auto EC = dumpSectionContribs())
    return EC;

  if (auto EC = dumpSectionMap())
    return EC;

  if (auto EC = dumpGlobalsStream())
    return EC;

  if (auto EC = dumpPublicsStream())
    return EC;

  if (auto EC = dumpSectionHeaders())
    return EC;

  if (auto EC = dumpFpoStream())
    return EC;

  flush();

  return Error::success();
}

Error LLVMOutputStyle::dumpFileHeaders() {
  if (!opts::raw::DumpHeaders)
    return Error::success();

  DictScope D(P, "FileHeaders");
  P.printNumber("BlockSize", File.getBlockSize());
  P.printNumber("FreeBlockMap", File.getFreeBlockMapBlock());
  P.printNumber("NumBlocks", File.getBlockCount());
  P.printNumber("NumDirectoryBytes", File.getNumDirectoryBytes());
  P.printNumber("Unknown1", File.getUnknown1());
  P.printNumber("BlockMapAddr", File.getBlockMapIndex());
  P.printNumber("NumDirectoryBlocks", File.getNumDirectoryBlocks());

  // The directory is not contiguous.  Instead, the block map contains a
  // contiguous list of block numbers whose contents, when concatenated in
  // order, make up the directory.
  P.printList("DirectoryBlocks", File.getDirectoryBlockArray());
  P.printNumber("NumStreams", File.getNumStreams());
  return Error::success();
}

Error LLVMOutputStyle::dumpStreamSummary() {
  if (!opts::raw::DumpStreamSummary)
    return Error::success();

  if (StreamPurposes.empty())
    discoverStreamPurposes(File, StreamPurposes);

  uint32_t StreamCount = File.getNumStreams();

  ListScope L(P, "Streams");
  for (uint16_t StreamIdx = 0; StreamIdx < StreamCount; ++StreamIdx) {
    std::string Label("Stream ");
    Label += to_string(StreamIdx);

    std::string Value = "[" + StreamPurposes[StreamIdx] + "] (";
    Value += to_string(File.getStreamByteSize(StreamIdx));
    Value += " bytes)";

    P.printString(Label, Value);
  }

  P.flush();
  return Error::success();
}

Error LLVMOutputStyle::dumpFreePageMap() {
  if (!opts::raw::DumpPageStats)
    return Error::success();

  // Start with used pages instead of free pages because
  // the number of free pages is far larger than used pages.
  BitVector FPM = File.getMsfLayout().FreePageMap;

  PageStats PS(FPM);

  recordKnownUsedPage(PS, 0); // MSF Super Block

  uint32_t BlocksPerSection = msf::getFpmIntervalLength(File.getMsfLayout());
  uint32_t NumSections = msf::getNumFpmIntervals(File.getMsfLayout());
  for (uint32_t I = 0; I < NumSections; ++I) {
    uint32_t Fpm0 = 1 + BlocksPerSection * I;
    // 2 Fpm blocks spaced at `getBlockSize()` block intervals
    recordKnownUsedPage(PS, Fpm0);
    recordKnownUsedPage(PS, Fpm0 + 1);
  }

  recordKnownUsedPage(PS, File.getBlockMapIndex()); // Stream Table

  for (auto DB : File.getDirectoryBlockArray())
    recordKnownUsedPage(PS, DB);

  // Record pages used by streams. Note that pages for stream 0
  // are considered being unused because that's what MSVC tools do.
  // Stream 0 doesn't contain actual data, so it makes some sense,
  // though it's a bit confusing to us.
  for (auto &SE : File.getStreamMap().drop_front(1))
    for (auto &S : SE)
      recordKnownUsedPage(PS, S);

  dumpBitVector("Msf Free Pages", FPM);
  dumpBitVector("Orphaned Pages", PS.OrphanedPages);
  dumpBitVector("Multiply Used Pages", PS.MultiUsePages);
  dumpBitVector("Use After Free Pages", PS.UseAfterFreePages);
  return Error::success();
}

void LLVMOutputStyle::dumpBitVector(StringRef Name, const BitVector &V) {
  std::vector<uint32_t> Vec;
  for (uint32_t I = 0, E = V.size(); I != E; ++I)
    if (V[I])
      Vec.push_back(I);
  P.printList(Name, Vec);
}

Error LLVMOutputStyle::dumpGlobalsStream() {
  if (!opts::raw::DumpGlobals)
    return Error::success();
  if (!File.hasPDBGlobalsStream()) {
    P.printString("Globals Stream not present");
    return Error::success();
  }

  auto Globals = File.getPDBGlobalsStream();
  if (!Globals)
    return Globals.takeError();
  DictScope D(P, "Globals Stream");

  auto Dbi = File.getPDBDbiStream();
  if (!Dbi)
    return Dbi.takeError();

  P.printNumber("Stream number", Dbi->getGlobalSymbolStreamIndex());
  P.printNumber("Number of buckets", Globals->getNumBuckets());
  P.printList("Hash Buckets", Globals->getHashBuckets());

  return Error::success();
}

Error LLVMOutputStyle::dumpStreamBlocks() {
  if (!opts::raw::DumpStreamBlocks)
    return Error::success();

  ListScope L(P, "StreamBlocks");
  uint32_t StreamCount = File.getNumStreams();
  for (uint32_t StreamIdx = 0; StreamIdx < StreamCount; ++StreamIdx) {
    std::string Name("Stream ");
    Name += to_string(StreamIdx);
    auto StreamBlocks = File.getStreamBlockList(StreamIdx);
    P.printList(Name, StreamBlocks);
  }
  return Error::success();
}

Error LLVMOutputStyle::dumpBlockRanges() {
  if (!opts::raw::DumpBlockRange.hasValue())
    return Error::success();
  auto &R = *opts::raw::DumpBlockRange;
  uint32_t Max = R.Max.getValueOr(R.Min);

  if (Max < R.Min)
    return make_error<StringError>(
        "Invalid block range specified.  Max < Min",
        std::make_error_code(std::errc::bad_address));
  if (Max >= File.getBlockCount())
    return make_error<StringError>(
        "Invalid block range specified.  Requested block out of bounds",
        std::make_error_code(std::errc::bad_address));

  DictScope D(P, "Block Data");
  for (uint32_t I = R.Min; I <= Max; ++I) {
    auto ExpectedData = File.getBlockData(I, File.getBlockSize());
    if (!ExpectedData)
      return ExpectedData.takeError();
    std::string Label;
    llvm::raw_string_ostream S(Label);
    S << "Block " << I;
    S.flush();
    P.printBinaryBlock(Label, *ExpectedData);
  }

  return Error::success();
}

static Error parseStreamSpec(StringRef Str, uint32_t &SI, uint32_t &Offset,
                             uint32_t &Size) {
  if (Str.consumeInteger(0, SI))
    return make_error<RawError>(raw_error_code::invalid_format,
                                "Invalid Stream Specification");
  if (Str.consume_front(":")) {
    if (Str.consumeInteger(0, Offset))
      return make_error<RawError>(raw_error_code::invalid_format,
                                  "Invalid Stream Specification");
  }
  if (Str.consume_front("@")) {
    if (Str.consumeInteger(0, Size))
      return make_error<RawError>(raw_error_code::invalid_format,
                                  "Invalid Stream Specification");
  }
  if (!Str.empty())
    return make_error<RawError>(raw_error_code::invalid_format,
                                "Invalid Stream Specification");
  return Error::success();
}

Error LLVMOutputStyle::dumpStreamBytes() {
  if (opts::raw::DumpStreamData.empty())
    return Error::success();

  if (StreamPurposes.empty())
    discoverStreamPurposes(File, StreamPurposes);

  DictScope D(P, "Stream Data");
  for (auto &Str : opts::raw::DumpStreamData) {
    uint32_t SI = 0;
    uint32_t Begin = 0;
    uint32_t Size = 0;
    uint32_t End = 0;

    if (auto EC = parseStreamSpec(Str, SI, Begin, Size))
      return EC;

    if (SI >= File.getNumStreams())
      return make_error<RawError>(raw_error_code::no_stream);

    auto S = MappedBlockStream::createIndexedStream(
        File.getMsfLayout(), File.getMsfBuffer(), SI, File.getAllocator());
    if (!S)
      continue;
    DictScope DD(P, "Stream");
    if (Size == 0)
      End = S->getLength();
    else {
      End = Begin + Size;
      if (End >= S->getLength())
        return make_error<RawError>(raw_error_code::index_out_of_bounds,
                                    "Stream is not long enough!");
    }

    P.printNumber("Index", SI);
    P.printString("Type", StreamPurposes[SI]);
    P.printNumber("Size", S->getLength());
    auto Blocks = File.getMsfLayout().StreamMap[SI];
    P.printList("Blocks", Blocks);

    BinaryStreamReader R(*S);
    ArrayRef<uint8_t> StreamData;
    if (auto EC = R.readBytes(StreamData, S->getLength()))
      return EC;
    Size = End - Begin;
    StreamData = StreamData.slice(Begin, Size);
    P.printBinaryBlock("Data", StreamData, Begin);
  }
  return Error::success();
}

Error LLVMOutputStyle::dumpStringTable() {
  if (!opts::raw::DumpStringTable)
    return Error::success();

  auto IS = File.getStringTable();
  if (!IS)
    return IS.takeError();

  DictScope D(P, "String Table");
  for (uint32_t I : IS->name_ids()) {
    auto ES = IS->getStringForID(I);
    if (!ES)
      return ES.takeError();

    if (ES->empty())
      continue;
    llvm::SmallString<32> Str;
    Str.append("'");
    Str.append(*ES);
    Str.append("'");
    P.printString(Str);
  }
  return Error::success();
}

Error LLVMOutputStyle::dumpInfoStream() {
  if (!opts::raw::DumpHeaders)
    return Error::success();
  if (!File.hasPDBInfoStream()) {
    P.printString("PDB Stream not present");
    return Error::success();
  }
  auto IS = File.getPDBInfoStream();
  if (!IS)
    return IS.takeError();

  DictScope D(P, "PDB Stream");
  P.printNumber("Version", IS->getVersion());
  P.printHex("Signature", IS->getSignature());
  P.printNumber("Age", IS->getAge());
  P.printObject("Guid", IS->getGuid());
  P.printHex("Features", IS->getFeatures());
  {
    DictScope DD(P, "Named Streams");
    for (const auto &S : IS->getNamedStreams().entries())
      P.printObject(S.getKey(), S.getValue());
  }
  return Error::success();
}

namespace {
class RecordBytesVisitor : public TypeVisitorCallbacks {
public:
  explicit RecordBytesVisitor(ScopedPrinter &P) : P(P) {}

  Error visitTypeEnd(CVType &Record) override {
    P.printBinaryBlock("Bytes", Record.content());
    return Error::success();
  }

private:
  ScopedPrinter &P;
};
}

Error LLVMOutputStyle::dumpTpiStream(uint32_t StreamIdx) {
  assert(StreamIdx == StreamTPI || StreamIdx == StreamIPI);

  bool DumpRecordBytes = false;
  bool DumpRecords = false;
  bool DumpTpiHash = false;
  StringRef Label;
  StringRef VerLabel;
  if (StreamIdx == StreamTPI) {
    if (!File.hasPDBTpiStream()) {
      P.printString("Type Info Stream (TPI) not present");
      return Error::success();
    }
    DumpRecordBytes = opts::raw::DumpTpiRecordBytes;
    DumpRecords = opts::raw::DumpTpiRecords;
    DumpTpiHash = opts::raw::DumpTpiHash;
    Label = "Type Info Stream (TPI)";
    VerLabel = "TPI Version";
  } else if (StreamIdx == StreamIPI) {
    if (!File.hasPDBIpiStream()) {
      P.printString("Type Info Stream (IPI) not present");
      return Error::success();
    }
    DumpRecordBytes = opts::raw::DumpIpiRecordBytes;
    DumpRecords = opts::raw::DumpIpiRecords;
    Label = "Type Info Stream (IPI)";
    VerLabel = "IPI Version";
  }

  auto Tpi = (StreamIdx == StreamTPI) ? File.getPDBTpiStream()
                                      : File.getPDBIpiStream();
  if (!Tpi)
    return Tpi.takeError();

  auto ExpectedTypes = initializeTypeDatabase(StreamIdx);
  if (!ExpectedTypes)
    return ExpectedTypes.takeError();
  auto &Types = *ExpectedTypes;

  if (!DumpRecordBytes && !DumpRecords && !DumpTpiHash)
    return Error::success();

  std::unique_ptr<DictScope> StreamScope;
  std::unique_ptr<ListScope> RecordScope;

  StreamScope = llvm::make_unique<DictScope>(P, Label);
  P.printNumber(VerLabel, Tpi->getTpiVersion());
  P.printNumber("Record count", Tpi->getNumTypeRecords());

  std::vector<std::unique_ptr<TypeVisitorCallbacks>> Visitors;

  // If we're in dump mode, add a dumper with the appropriate detail level.
  if (DumpRecords) {
    std::unique_ptr<TypeVisitorCallbacks> Dumper;
    if (opts::raw::CompactRecords)
      Dumper = make_unique<CompactTypeDumpVisitor>(Types, &P);
    else {
      assert(TpiTypes);

      auto X = make_unique<TypeDumpVisitor>(*TpiTypes, &P, false);
      if (StreamIdx == StreamIPI)
        X->setIpiTypes(*IpiTypes);
      Dumper = std::move(X);
    }
    Visitors.push_back(std::move(Dumper));
  }
  if (DumpRecordBytes)
    Visitors.push_back(make_unique<RecordBytesVisitor>(P));

  // We always need to deserialize and add it to the type database.  This is
  // true if even if we're not dumping anything, because we could need the
  // type database for the purposes of dumping symbols.
  TypeVisitorCallbackPipeline Pipeline;
  for (const auto &V : Visitors)
    Pipeline.addCallbackToPipeline(*V);

  if (DumpRecords || DumpRecordBytes)
    RecordScope = llvm::make_unique<ListScope>(P, "Records");

  Optional<TypeIndex> I = Types.getFirst();
  while (I) {
    std::unique_ptr<DictScope> OneRecordScope;

    if ((DumpRecords || DumpRecordBytes) && !opts::raw::CompactRecords)
      OneRecordScope = llvm::make_unique<DictScope>(P, "");

    auto T = Types.getType(*I);
    if (auto EC = codeview::visitTypeRecord(T, *I, Pipeline))
      return EC;
    I = Types.getNext(*I);
  }

  if (DumpTpiHash) {
    DictScope DD(P, "Hash");
    P.printNumber("Number of Hash Buckets", Tpi->getNumHashBuckets());
    P.printNumber("Hash Key Size", Tpi->getHashKeySize());
    P.printList("Values", Tpi->getHashValues());

    ListScope LHA(P, "Adjusters");
    auto ExpectedST = File.getStringTable();
    if (!ExpectedST)
      return ExpectedST.takeError();
    const auto &ST = *ExpectedST;
    for (const auto &E : Tpi->getHashAdjusters()) {
      DictScope DHA(P);
      auto Name = ST.getStringForID(E.first);
      if (!Name)
        return Name.takeError();

      P.printString("Type", *Name);
      P.printHex("TI", E.second);
    }
  }

  ListScope L(P, "TypeIndexOffsets");
  for (const auto &IO : Tpi->getTypeIndexOffsets()) {
    P.printString(formatv("Index: {0:x}, Offset: {1:N}", IO.Type.getIndex(),
                          (uint32_t)IO.Offset)
                      .str());
  }

  P.flush();
  return Error::success();
}

Expected<codeview::LazyRandomTypeCollection &>
LLVMOutputStyle::initializeTypeDatabase(uint32_t SN) {
  auto &TypeCollection = (SN == StreamTPI) ? TpiTypes : IpiTypes;
  auto Tpi =
      (SN == StreamTPI) ? File.getPDBTpiStream() : File.getPDBIpiStream();
  if (!Tpi)
    return Tpi.takeError();

  if (!TypeCollection) {
    // Initialize the type collection, even if we're not going to dump it.  This
    // way if some other part of the dumper decides it wants to use some or all
    // of the records for whatever purposes, it can still access them lazily.
    auto &Types = Tpi->typeArray();
    uint32_t Count = Tpi->getNumTypeRecords();
    auto Offsets = Tpi->getTypeIndexOffsets();
    TypeCollection =
        llvm::make_unique<LazyRandomTypeCollection>(Types, Count, Offsets);
  }

  return *TypeCollection;
}

Error LLVMOutputStyle::dumpDbiStream() {
  bool DumpModules = opts::shared::DumpModules ||
                     opts::shared::DumpModuleSyms ||
                     opts::shared::DumpModuleFiles ||
                     !opts::shared::DumpModuleSubsections.empty();
  if (!opts::raw::DumpHeaders && !DumpModules)
    return Error::success();
  if (!File.hasPDBDbiStream()) {
    P.printString("DBI Stream not present");
    return Error::success();
  }

  auto DS = File.getPDBDbiStream();
  if (!DS)
    return DS.takeError();

  DictScope D(P, "DBI Stream");
  P.printNumber("Dbi Version", DS->getDbiVersion());
  P.printNumber("Age", DS->getAge());
  P.printBoolean("Incremental Linking", DS->isIncrementallyLinked());
  P.printBoolean("Has CTypes", DS->hasCTypes());
  P.printBoolean("Is Stripped", DS->isStripped());
  P.printObject("Machine Type", DS->getMachineType());
  P.printNumber("Symbol Record Stream Index", DS->getSymRecordStreamIndex());
  P.printNumber("Public Symbol Stream Index", DS->getPublicSymbolStreamIndex());
  P.printNumber("Global Symbol Stream Index", DS->getGlobalSymbolStreamIndex());

  uint16_t Major = DS->getBuildMajorVersion();
  uint16_t Minor = DS->getBuildMinorVersion();
  P.printVersion("Toolchain Version", Major, Minor);

  std::string DllName;
  raw_string_ostream DllStream(DllName);
  DllStream << "mspdb" << Major << Minor << ".dll version";
  DllStream.flush();
  P.printVersion(DllName, Major, Minor, DS->getPdbDllVersion());

  if (DumpModules) {
    ListScope L(P, "Modules");
    const DbiModuleList &Modules = DS->modules();
    for (uint32_t I = 0; I < Modules.getModuleCount(); ++I) {
      const DbiModuleDescriptor &Modi = Modules.getModuleDescriptor(I);
      DictScope DD(P);
      P.printString("Name", Modi.getModuleName().str());
      P.printNumber("Debug Stream Index", Modi.getModuleStreamIndex());
      P.printString("Object File Name", Modi.getObjFileName().str());
      P.printNumber("Num Files", Modi.getNumberOfFiles());
      P.printNumber("Source File Name Idx", Modi.getSourceFileNameIndex());
      P.printNumber("Pdb File Name Idx", Modi.getPdbFilePathNameIndex());
      P.printNumber("Line Info Byte Size", Modi.getC11LineInfoByteSize());
      P.printNumber("C13 Line Info Byte Size", Modi.getC13LineInfoByteSize());
      P.printNumber("Symbol Byte Size", Modi.getSymbolDebugInfoByteSize());
      P.printNumber("Type Server Index", Modi.getTypeServerIndex());
      P.printBoolean("Has EC Info", Modi.hasECInfo());
      if (opts::shared::DumpModuleFiles) {
        std::string FileListName = to_string(Modules.getSourceFileCount(I)) +
                                   " Contributing Source Files";
        ListScope LL(P, FileListName);
        for (auto File : Modules.source_files(I))
          P.printString(File);
      }
      bool HasModuleDI = (Modi.getModuleStreamIndex() < File.getNumStreams());
      bool ShouldDumpSymbols =
          (opts::shared::DumpModuleSyms || opts::raw::DumpSymRecordBytes);
      if (HasModuleDI &&
          (ShouldDumpSymbols || !opts::shared::DumpModuleSubsections.empty())) {
        auto ModStreamData = MappedBlockStream::createIndexedStream(
            File.getMsfLayout(), File.getMsfBuffer(),
            Modi.getModuleStreamIndex(), File.getAllocator());

        ModuleDebugStreamRef ModS(Modi, std::move(ModStreamData));
        if (auto EC = ModS.reload())
          return EC;

        auto ExpectedTpi = initializeTypeDatabase(StreamTPI);
        if (!ExpectedTpi)
          return ExpectedTpi.takeError();
        auto &Tpi = *ExpectedTpi;
        if (ShouldDumpSymbols) {

          ListScope SS(P, "Symbols");
          codeview::CVSymbolDumper SD(P, Tpi, CodeViewContainer::Pdb, nullptr,
                                      false);
          bool HadError = false;
          for (auto S : ModS.symbols(&HadError)) {
            DictScope LL(P, "");
            if (opts::shared::DumpModuleSyms) {
              if (auto EC = SD.dump(S)) {
                llvm::consumeError(std::move(EC));
                HadError = true;
                break;
              }
            }
            if (opts::raw::DumpSymRecordBytes)
              P.printBinaryBlock("Bytes", S.content());
          }
          if (HadError)
            return make_error<RawError>(
                raw_error_code::corrupt_file,
                "DBI stream contained corrupt symbol record");
        }
        if (!opts::shared::DumpModuleSubsections.empty()) {
          ListScope SS(P, "Subsections");
          auto ExpectedIpi = initializeTypeDatabase(StreamIPI);
          if (!ExpectedIpi)
            return ExpectedIpi.takeError();
          auto &Ipi = *ExpectedIpi;
          auto ExpectedStrings = File.getStringTable();
          if (!ExpectedStrings)
            return joinErrors(
                make_error<RawError>(raw_error_code::no_stream,
                                     "Could not get string table!"),
                ExpectedStrings.takeError());

          C13RawVisitor V(P, Tpi, Ipi);
          if (auto EC = codeview::visitDebugSubsections(
                  ModS.subsections(), V, ExpectedStrings->getStringTable()))
            return EC;
        }
      }
    }
  }
  return Error::success();
}

Error LLVMOutputStyle::dumpSectionContribs() {
  if (!opts::raw::DumpSectionContribs)
    return Error::success();
  if (!File.hasPDBDbiStream()) {
    P.printString("DBI Stream not present");
    return Error::success();
  }

  auto Dbi = File.getPDBDbiStream();
  if (!Dbi)
    return Dbi.takeError();

  ListScope L(P, "Section Contributions");
  class Visitor : public ISectionContribVisitor {
  public:
    Visitor(ScopedPrinter &P, DbiStream &DS) : P(P), DS(DS) {}
    void visit(const SectionContrib &SC) override {
      DictScope D(P, "Contribution");
      P.printNumber("ISect", SC.ISect);
      P.printNumber("Off", SC.Off);
      P.printNumber("Size", SC.Size);
      P.printFlags("Characteristics", SC.Characteristics,
                   codeview::getImageSectionCharacteristicNames(),
                   COFF::SectionCharacteristics(0x00F00000));
      {
        DictScope DD(P, "Module");
        P.printNumber("Index", SC.Imod);
        const DbiModuleList &Modules = DS.modules();
        if (Modules.getModuleCount() > SC.Imod) {
          P.printString("Name",
                        Modules.getModuleDescriptor(SC.Imod).getModuleName());
        }
      }
      P.printNumber("Data CRC", SC.DataCrc);
      P.printNumber("Reloc CRC", SC.RelocCrc);
      P.flush();
    }
    void visit(const SectionContrib2 &SC) override {
      visit(SC.Base);
      P.printNumber("ISect Coff", SC.ISectCoff);
      P.flush();
    }

  private:
    ScopedPrinter &P;
    DbiStream &DS;
  };
  Visitor V(P, *Dbi);
  Dbi->visitSectionContributions(V);
  return Error::success();
}

Error LLVMOutputStyle::dumpSectionMap() {
  if (!opts::raw::DumpSectionMap)
    return Error::success();
  if (!File.hasPDBDbiStream()) {
    P.printString("DBI Stream not present");
    return Error::success();
  }

  auto Dbi = File.getPDBDbiStream();
  if (!Dbi)
    return Dbi.takeError();

  ListScope L(P, "Section Map");
  for (auto &M : Dbi->getSectionMap()) {
    DictScope D(P, "Entry");
    P.printFlags("Flags", M.Flags, getOMFSegMapDescFlagNames());
    P.printNumber("Ovl", M.Ovl);
    P.printNumber("Group", M.Group);
    P.printNumber("Frame", M.Frame);
    P.printNumber("SecName", M.SecName);
    P.printNumber("ClassName", M.ClassName);
    P.printNumber("Offset", M.Offset);
    P.printNumber("SecByteLength", M.SecByteLength);
    P.flush();
  }
  return Error::success();
}

Error LLVMOutputStyle::dumpPublicsStream() {
  if (!opts::raw::DumpPublics)
    return Error::success();
  if (!File.hasPDBPublicsStream()) {
    P.printString("Publics Stream not present");
    return Error::success();
  }

  auto Publics = File.getPDBPublicsStream();
  if (!Publics)
    return Publics.takeError();
  DictScope D(P, "Publics Stream");

  auto Dbi = File.getPDBDbiStream();
  if (!Dbi)
    return Dbi.takeError();

  P.printNumber("Stream number", Dbi->getPublicSymbolStreamIndex());
  P.printNumber("SymHash", Publics->getSymHash());
  P.printNumber("AddrMap", Publics->getAddrMap());
  P.printNumber("Number of buckets", Publics->getNumBuckets());
  P.printList("Hash Buckets", Publics->getHashBuckets());
  P.printList("Address Map", Publics->getAddressMap());
  P.printList("Thunk Map", Publics->getThunkMap());
  P.printList("Section Offsets", Publics->getSectionOffsets(),
              printSectionOffset);
  ListScope L(P, "Symbols");
  auto ExpectedTypes = initializeTypeDatabase(StreamTPI);
  if (!ExpectedTypes)
    return ExpectedTypes.takeError();
  auto &Tpi = *ExpectedTypes;

  codeview::CVSymbolDumper SD(P, Tpi, CodeViewContainer::Pdb, nullptr, false);
  bool HadError = false;
  for (auto S : Publics->getSymbols(&HadError)) {
    DictScope DD(P, "");

    if (auto EC = SD.dump(S)) {
      HadError = true;
      break;
    }
    if (opts::raw::DumpSymRecordBytes)
      P.printBinaryBlock("Bytes", S.content());
  }
  if (HadError)
    return make_error<RawError>(
        raw_error_code::corrupt_file,
        "Public symbol stream contained corrupt record");

  return Error::success();
}

Error LLVMOutputStyle::dumpSectionHeaders() {
  if (!opts::raw::DumpSectionHeaders)
    return Error::success();
  if (!File.hasPDBDbiStream()) {
    P.printString("DBI Stream not present");
    return Error::success();
  }

  auto Dbi = File.getPDBDbiStream();
  if (!Dbi)
    return Dbi.takeError();

  ListScope D(P, "Section Headers");
  for (const object::coff_section &Section : Dbi->getSectionHeaders()) {
    DictScope DD(P, "");

    // If a name is 8 characters long, there is no NUL character at end.
#if __QNXNTO__
    StringRef Name(Section.Name, std::strnlen(Section.Name, sizeof(Section.Name)));
#else
    StringRef Name(Section.Name, ::strnlen(Section.Name, sizeof(Section.Name)));
#endif
    P.printString("Name", Name);
    P.printNumber("Virtual Size", Section.VirtualSize);
    P.printNumber("Virtual Address", Section.VirtualAddress);
    P.printNumber("Size of Raw Data", Section.SizeOfRawData);
    P.printNumber("File Pointer to Raw Data", Section.PointerToRawData);
    P.printNumber("File Pointer to Relocations", Section.PointerToRelocations);
    P.printNumber("File Pointer to Linenumbers", Section.PointerToLinenumbers);
    P.printNumber("Number of Relocations", Section.NumberOfRelocations);
    P.printNumber("Number of Linenumbers", Section.NumberOfLinenumbers);
    P.printFlags("Characteristics", Section.Characteristics,
                 getImageSectionCharacteristicNames());
  }
  return Error::success();
}

Error LLVMOutputStyle::dumpFpoStream() {
  if (!opts::raw::DumpFpo)
    return Error::success();
  if (!File.hasPDBDbiStream()) {
    P.printString("DBI Stream not present");
    return Error::success();
  }

  auto Dbi = File.getPDBDbiStream();
  if (!Dbi)
    return Dbi.takeError();

  ListScope D(P, "New FPO");
  for (const object::FpoData &Fpo : Dbi->getFpoRecords()) {
    DictScope DD(P, "");
    P.printNumber("Offset", Fpo.Offset);
    P.printNumber("Size", Fpo.Size);
    P.printNumber("Number of locals", Fpo.NumLocals);
    P.printNumber("Number of params", Fpo.NumParams);
    P.printNumber("Size of Prolog", Fpo.getPrologSize());
    P.printNumber("Number of Saved Registers", Fpo.getNumSavedRegs());
    P.printBoolean("Has SEH", Fpo.hasSEH());
    P.printBoolean("Use BP", Fpo.useBP());
    P.printNumber("Frame Pointer", Fpo.getFP());
  }
  return Error::success();
}

void LLVMOutputStyle::flush() { P.flush(); }
