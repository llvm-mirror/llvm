//===- BitcodeReader.cpp - Internal BitcodeReader implementation ----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Triple.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Bitcode/BitstreamReader.h"
#include "llvm/Bitcode/LLVMBitCodes.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/AutoUpgrade.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/Comdat.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalIFunc.h"
#include "llvm/IR/GlobalIndirectSymbol.h"
#include "llvm/IR/GlobalObject.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/GVMaterializer.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ModuleSummaryIndex.h"
#include "llvm/IR/OperandTraits.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/TrackingMDRef.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Support/AtomicOrdering.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

using namespace llvm;

static cl::opt<bool> PrintSummaryGUIDs(
    "print-summary-global-ids", cl::init(false), cl::Hidden,
    cl::desc(
        "Print the global id for each value when reading the module summary"));

namespace {

enum {
  SWITCH_INST_MAGIC = 0x4B5 // May 2012 => 1205 => Hex
};

class BitcodeReaderValueList {
  std::vector<WeakVH> ValuePtrs;

  /// As we resolve forward-referenced constants, we add information about them
  /// to this vector.  This allows us to resolve them in bulk instead of
  /// resolving each reference at a time.  See the code in
  /// ResolveConstantForwardRefs for more information about this.
  ///
  /// The key of this vector is the placeholder constant, the value is the slot
  /// number that holds the resolved value.
  typedef std::vector<std::pair<Constant*, unsigned> > ResolveConstantsTy;
  ResolveConstantsTy ResolveConstants;
  LLVMContext &Context;

public:
  BitcodeReaderValueList(LLVMContext &C) : Context(C) {}
  ~BitcodeReaderValueList() {
    assert(ResolveConstants.empty() && "Constants not resolved?");
  }

  // vector compatibility methods
  unsigned size() const { return ValuePtrs.size(); }
  void resize(unsigned N) { ValuePtrs.resize(N); }
  void push_back(Value *V) { ValuePtrs.emplace_back(V); }

  void clear() {
    assert(ResolveConstants.empty() && "Constants not resolved?");
    ValuePtrs.clear();
  }

  Value *operator[](unsigned i) const {
    assert(i < ValuePtrs.size());
    return ValuePtrs[i];
  }

  Value *back() const { return ValuePtrs.back(); }
  void pop_back() { ValuePtrs.pop_back(); }
  bool empty() const { return ValuePtrs.empty(); }

  void shrinkTo(unsigned N) {
    assert(N <= size() && "Invalid shrinkTo request!");
    ValuePtrs.resize(N);
  }

  Constant *getConstantFwdRef(unsigned Idx, Type *Ty);
  Value *getValueFwdRef(unsigned Idx, Type *Ty);

  void assignValue(Value *V, unsigned Idx);

  /// Once all constants are read, this method bulk resolves any forward
  /// references.
  void resolveConstantForwardRefs();
};

class BitcodeReaderMetadataList {
  unsigned NumFwdRefs;
  bool AnyFwdRefs;
  unsigned MinFwdRef;
  unsigned MaxFwdRef;

  /// Array of metadata references.
  ///
  /// Don't use std::vector here.  Some versions of libc++ copy (instead of
  /// move) on resize, and TrackingMDRef is very expensive to copy.
  SmallVector<TrackingMDRef, 1> MetadataPtrs;

  /// Structures for resolving old type refs.
  struct {
    SmallDenseMap<MDString *, TempMDTuple, 1> Unknown;
    SmallDenseMap<MDString *, DICompositeType *, 1> Final;
    SmallDenseMap<MDString *, DICompositeType *, 1> FwdDecls;
    SmallVector<std::pair<TrackingMDRef, TempMDTuple>, 1> Arrays;
  } OldTypeRefs;

  LLVMContext &Context;

public:
  BitcodeReaderMetadataList(LLVMContext &C)
      : NumFwdRefs(0), AnyFwdRefs(false), Context(C) {}

  // vector compatibility methods
  unsigned size() const { return MetadataPtrs.size(); }
  void resize(unsigned N) { MetadataPtrs.resize(N); }
  void push_back(Metadata *MD) { MetadataPtrs.emplace_back(MD); }
  void clear() { MetadataPtrs.clear(); }
  Metadata *back() const { return MetadataPtrs.back(); }
  void pop_back() { MetadataPtrs.pop_back(); }
  bool empty() const { return MetadataPtrs.empty(); }

  Metadata *operator[](unsigned i) const {
    assert(i < MetadataPtrs.size());
    return MetadataPtrs[i];
  }

  Metadata *lookup(unsigned I) const {
    if (I < MetadataPtrs.size())
      return MetadataPtrs[I];
    return nullptr;
  }

  void shrinkTo(unsigned N) {
    assert(N <= size() && "Invalid shrinkTo request!");
    assert(!AnyFwdRefs && "Unexpected forward refs");
    MetadataPtrs.resize(N);
  }

  /// Return the given metadata, creating a replaceable forward reference if
  /// necessary.
  Metadata *getMetadataFwdRef(unsigned Idx);

  /// Return the the given metadata only if it is fully resolved.
  ///
  /// Gives the same result as \a lookup(), unless \a MDNode::isResolved()
  /// would give \c false.
  Metadata *getMetadataIfResolved(unsigned Idx);

  MDNode *getMDNodeFwdRefOrNull(unsigned Idx);
  void assignValue(Metadata *MD, unsigned Idx);
  void tryToResolveCycles();
  bool hasFwdRefs() const { return AnyFwdRefs; }

  /// Upgrade a type that had an MDString reference.
  void addTypeRef(MDString &UUID, DICompositeType &CT);

  /// Upgrade a type that had an MDString reference.
  Metadata *upgradeTypeRef(Metadata *MaybeUUID);

  /// Upgrade a type ref array that may have MDString references.
  Metadata *upgradeTypeRefArray(Metadata *MaybeTuple);

private:
  Metadata *resolveTypeRefArray(Metadata *MaybeTuple);
};

Error error(const Twine &Message) {
  return make_error<StringError>(
      Message, make_error_code(BitcodeError::CorruptedBitcode));
}

/// Helper to read the header common to all bitcode files.
bool hasValidBitcodeHeader(BitstreamCursor &Stream) {
  // Sniff for the signature.
  if (!Stream.canSkipToPos(4) ||
      Stream.Read(8) != 'B' ||
      Stream.Read(8) != 'C' ||
      Stream.Read(4) != 0x0 ||
      Stream.Read(4) != 0xC ||
      Stream.Read(4) != 0xE ||
      Stream.Read(4) != 0xD)
    return false;
  return true;
}

Expected<BitstreamCursor> initStream(MemoryBufferRef Buffer) {
  const unsigned char *BufPtr = (const unsigned char *)Buffer.getBufferStart();
  const unsigned char *BufEnd = BufPtr + Buffer.getBufferSize();

  if (Buffer.getBufferSize() & 3)
    return error("Invalid bitcode signature");

  // If we have a wrapper header, parse it and ignore the non-bc file contents.
  // The magic number is 0x0B17C0DE stored in little endian.
  if (isBitcodeWrapper(BufPtr, BufEnd))
    if (SkipBitcodeWrapperHeader(BufPtr, BufEnd, true))
      return error("Invalid bitcode wrapper header");

  BitstreamCursor Stream(ArrayRef<uint8_t>(BufPtr, BufEnd));
  if (!hasValidBitcodeHeader(Stream))
    return error("Invalid bitcode signature");

  return std::move(Stream);
}

/// Convert a string from a record into an std::string, return true on failure.
template <typename StrTy>
static bool convertToString(ArrayRef<uint64_t> Record, unsigned Idx,
                            StrTy &Result) {
  if (Idx > Record.size())
    return true;

  for (unsigned i = Idx, e = Record.size(); i != e; ++i)
    Result += (char)Record[i];
  return false;
}

/// Read the "IDENTIFICATION_BLOCK_ID" block, do some basic enforcement on the
/// "epoch" encoded in the bitcode, and return the producer name if any.
Expected<std::string> readIdentificationBlock(BitstreamCursor &Stream) {
  if (Stream.EnterSubBlock(bitc::IDENTIFICATION_BLOCK_ID))
    return error("Invalid record");

  // Read all the records.
  SmallVector<uint64_t, 64> Record;

  std::string ProducerIdentification;

  while (true) {
    BitstreamEntry Entry = Stream.advance();

    switch (Entry.Kind) {
    default:
    case BitstreamEntry::Error:
      return error("Malformed block");
    case BitstreamEntry::EndBlock:
      return ProducerIdentification;
    case BitstreamEntry::Record:
      // The interesting case.
      break;
    }

    // Read a record.
    Record.clear();
    unsigned BitCode = Stream.readRecord(Entry.ID, Record);
    switch (BitCode) {
    default: // Default behavior: reject
      return error("Invalid value");
    case bitc::IDENTIFICATION_CODE_STRING: // IDENTIFICATION: [strchr x N]
      convertToString(Record, 0, ProducerIdentification);
      break;
    case bitc::IDENTIFICATION_CODE_EPOCH: { // EPOCH: [epoch#]
      unsigned epoch = (unsigned)Record[0];
      if (epoch != bitc::BITCODE_CURRENT_EPOCH) {
        return error(
          Twine("Incompatible epoch: Bitcode '") + Twine(epoch) +
          "' vs current: '" + Twine(bitc::BITCODE_CURRENT_EPOCH) + "'");
      }
    }
    }
  }
}

Expected<std::string> readIdentificationCode(BitstreamCursor &Stream) {
  // We expect a number of well-defined blocks, though we don't necessarily
  // need to understand them all.
  while (true) {
    if (Stream.AtEndOfStream())
      return "";

    BitstreamEntry Entry = Stream.advance();
    switch (Entry.Kind) {
    case BitstreamEntry::EndBlock:
    case BitstreamEntry::Error:
      return error("Malformed block");

    case BitstreamEntry::SubBlock:
      if (Entry.ID == bitc::IDENTIFICATION_BLOCK_ID)
        return readIdentificationBlock(Stream);

      // Ignore other sub-blocks.
      if (Stream.SkipBlock())
        return error("Malformed block");
      continue;
    case BitstreamEntry::Record:
      Stream.skipRecord(Entry.ID);
      continue;
    }
  }
}

Expected<bool> hasObjCCategoryInModule(BitstreamCursor &Stream) {
  if (Stream.EnterSubBlock(bitc::MODULE_BLOCK_ID))
    return error("Invalid record");

  SmallVector<uint64_t, 64> Record;
  // Read all the records for this module.

  while (true) {
    BitstreamEntry Entry = Stream.advanceSkippingSubblocks();

    switch (Entry.Kind) {
    case BitstreamEntry::SubBlock: // Handled for us already.
    case BitstreamEntry::Error:
      return error("Malformed block");
    case BitstreamEntry::EndBlock:
      return false;
    case BitstreamEntry::Record:
      // The interesting case.
      break;
    }

    // Read a record.
    switch (Stream.readRecord(Entry.ID, Record)) {
    default:
      break; // Default behavior, ignore unknown content.
    case bitc::MODULE_CODE_SECTIONNAME: { // SECTIONNAME: [strchr x N]
      std::string S;
      if (convertToString(Record, 0, S))
        return error("Invalid record");
      // Check for the i386 and other (x86_64, ARM) conventions
      if (S.find("__DATA, __objc_catlist") != std::string::npos ||
          S.find("__OBJC,__category") != std::string::npos)
        return true;
      break;
    }
    }
    Record.clear();
  }
  llvm_unreachable("Exit infinite loop");
}

Expected<bool> hasObjCCategory(BitstreamCursor &Stream) {
  // We expect a number of well-defined blocks, though we don't necessarily
  // need to understand them all.
  while (true) {
    BitstreamEntry Entry = Stream.advance();

    switch (Entry.Kind) {
    case BitstreamEntry::Error:
      return error("Malformed block");
    case BitstreamEntry::EndBlock:
      return false;

    case BitstreamEntry::SubBlock:
      if (Entry.ID == bitc::MODULE_BLOCK_ID)
        return hasObjCCategoryInModule(Stream);

      // Ignore other sub-blocks.
      if (Stream.SkipBlock())
        return error("Malformed block");
      continue;

    case BitstreamEntry::Record:
      Stream.skipRecord(Entry.ID);
      continue;
    }
  }
}

Expected<std::string> readModuleTriple(BitstreamCursor &Stream) {
  if (Stream.EnterSubBlock(bitc::MODULE_BLOCK_ID))
    return error("Invalid record");

  SmallVector<uint64_t, 64> Record;

  std::string Triple;

  // Read all the records for this module.
  while (true) {
    BitstreamEntry Entry = Stream.advanceSkippingSubblocks();

    switch (Entry.Kind) {
    case BitstreamEntry::SubBlock: // Handled for us already.
    case BitstreamEntry::Error:
      return error("Malformed block");
    case BitstreamEntry::EndBlock:
      return Triple;
    case BitstreamEntry::Record:
      // The interesting case.
      break;
    }

    // Read a record.
    switch (Stream.readRecord(Entry.ID, Record)) {
    default: break;  // Default behavior, ignore unknown content.
    case bitc::MODULE_CODE_TRIPLE: {  // TRIPLE: [strchr x N]
      std::string S;
      if (convertToString(Record, 0, S))
        return error("Invalid record");
      Triple = S;
      break;
    }
    }
    Record.clear();
  }
  llvm_unreachable("Exit infinite loop");
}

Expected<std::string> readTriple(BitstreamCursor &Stream) {
  // We expect a number of well-defined blocks, though we don't necessarily
  // need to understand them all.
  while (true) {
    BitstreamEntry Entry = Stream.advance();

    switch (Entry.Kind) {
    case BitstreamEntry::Error:
      return error("Malformed block");
    case BitstreamEntry::EndBlock:
      return "";

    case BitstreamEntry::SubBlock:
      if (Entry.ID == bitc::MODULE_BLOCK_ID)
        return readModuleTriple(Stream);

      // Ignore other sub-blocks.
      if (Stream.SkipBlock())
        return error("Malformed block");
      continue;

    case BitstreamEntry::Record:
      Stream.skipRecord(Entry.ID);
      continue;
    }
  }
}

class BitcodeReaderBase {
protected:
  BitcodeReaderBase(BitstreamCursor Stream) : Stream(std::move(Stream)) {
    this->Stream.setBlockInfo(&BlockInfo);
  }

  BitstreamBlockInfo BlockInfo;
  BitstreamCursor Stream;

  bool readBlockInfo();

  // Contains an arbitrary and optional string identifying the bitcode producer
  std::string ProducerIdentification;

  Error error(const Twine &Message);
};

Error BitcodeReaderBase::error(const Twine &Message) {
  std::string FullMsg = Message.str();
  if (!ProducerIdentification.empty())
    FullMsg += " (Producer: '" + ProducerIdentification + "' Reader: 'LLVM " +
               LLVM_VERSION_STRING "')";
  return ::error(FullMsg);
}

class BitcodeReader : public BitcodeReaderBase, public GVMaterializer {
  LLVMContext &Context;
  Module *TheModule = nullptr;
  // Next offset to start scanning for lazy parsing of function bodies.
  uint64_t NextUnreadBit = 0;
  // Last function offset found in the VST.
  uint64_t LastFunctionBlockBit = 0;
  bool SeenValueSymbolTable = false;
  uint64_t VSTOffset = 0;

  std::vector<Type*> TypeList;
  BitcodeReaderValueList ValueList;
  BitcodeReaderMetadataList MetadataList;
  std::vector<Comdat *> ComdatList;
  SmallVector<Instruction *, 64> InstructionList;

  std::vector<std::pair<GlobalVariable*, unsigned> > GlobalInits;
  std::vector<std::pair<GlobalIndirectSymbol*, unsigned> > IndirectSymbolInits;
  std::vector<std::pair<Function*, unsigned> > FunctionPrefixes;
  std::vector<std::pair<Function*, unsigned> > FunctionPrologues;
  std::vector<std::pair<Function*, unsigned> > FunctionPersonalityFns;

  bool HasSeenOldLoopTags = false;

  /// The set of attributes by index.  Index zero in the file is for null, and
  /// is thus not represented here.  As such all indices are off by one.
  std::vector<AttributeSet> MAttributes;

  /// The set of attribute groups.
  std::map<unsigned, AttributeSet> MAttributeGroups;

  /// While parsing a function body, this is a list of the basic blocks for the
  /// function.
  std::vector<BasicBlock*> FunctionBBs;

  // When reading the module header, this list is populated with functions that
  // have bodies later in the file.
  std::vector<Function*> FunctionsWithBodies;

  // When intrinsic functions are encountered which require upgrading they are
  // stored here with their replacement function.
  typedef DenseMap<Function*, Function*> UpdatedIntrinsicMap;
  UpdatedIntrinsicMap UpgradedIntrinsics;
  // Intrinsics which were remangled because of types rename
  UpdatedIntrinsicMap RemangledIntrinsics;

  // Map the bitcode's custom MDKind ID to the Module's MDKind ID.
  DenseMap<unsigned, unsigned> MDKindMap;

  // Several operations happen after the module header has been read, but
  // before function bodies are processed. This keeps track of whether
  // we've done this yet.
  bool SeenFirstFunctionBody = false;

  /// When function bodies are initially scanned, this map contains info about
  /// where to find deferred function body in the stream.
  DenseMap<Function*, uint64_t> DeferredFunctionInfo;

  /// When Metadata block is initially scanned when parsing the module, we may
  /// choose to defer parsing of the metadata. This vector contains info about
  /// which Metadata blocks are deferred.
  std::vector<uint64_t> DeferredMetadataInfo;

  /// These are basic blocks forward-referenced by block addresses.  They are
  /// inserted lazily into functions when they're loaded.  The basic block ID is
  /// its index into the vector.
  DenseMap<Function *, std::vector<BasicBlock *>> BasicBlockFwdRefs;
  std::deque<Function *> BasicBlockFwdRefQueue;

  /// Indicates that we are using a new encoding for instruction operands where
  /// most operands in the current FUNCTION_BLOCK are encoded relative to the
  /// instruction number, for a more compact encoding.  Some instruction
  /// operands are not relative to the instruction ID: basic block numbers, and
  /// types. Once the old style function blocks have been phased out, we would
  /// not need this flag.
  bool UseRelativeIDs = false;

  /// True if all functions will be materialized, negating the need to process
  /// (e.g.) blockaddress forward references.
  bool WillMaterializeAllForwardRefs = false;

  /// True if any Metadata block has been materialized.
  bool IsMetadataMaterialized = false;

  bool StripDebugInfo = false;

  /// Functions that need to be matched with subprograms when upgrading old
  /// metadata.
  SmallDenseMap<Function *, DISubprogram *, 16> FunctionsWithSPs;

  std::vector<std::string> BundleTags;

public:
  BitcodeReader(BitstreamCursor Stream, StringRef ProducerIdentification,
                LLVMContext &Context);

  Error materializeForwardReferencedFunctions();

  Error materialize(GlobalValue *GV) override;
  Error materializeModule() override;
  std::vector<StructType *> getIdentifiedStructTypes() const override;

  /// \brief Main interface to parsing a bitcode buffer.
  /// \returns true if an error occurred.
  Error parseBitcodeInto(Module *M, bool ShouldLazyLoadMetadata = false);

  static uint64_t decodeSignRotatedValue(uint64_t V);

  /// Materialize any deferred Metadata block.
  Error materializeMetadata() override;

  void setStripDebugInfo() override;

private:
  std::vector<StructType *> IdentifiedStructTypes;
  StructType *createIdentifiedStructType(LLVMContext &Context, StringRef Name);
  StructType *createIdentifiedStructType(LLVMContext &Context);

  Type *getTypeByID(unsigned ID);

  Value *getFnValueByID(unsigned ID, Type *Ty) {
    if (Ty && Ty->isMetadataTy())
      return MetadataAsValue::get(Ty->getContext(), getFnMetadataByID(ID));
    return ValueList.getValueFwdRef(ID, Ty);
  }

  Metadata *getFnMetadataByID(unsigned ID) {
    return MetadataList.getMetadataFwdRef(ID);
  }

  BasicBlock *getBasicBlock(unsigned ID) const {
    if (ID >= FunctionBBs.size()) return nullptr; // Invalid ID
    return FunctionBBs[ID];
  }

  AttributeSet getAttributes(unsigned i) const {
    if (i-1 < MAttributes.size())
      return MAttributes[i-1];
    return AttributeSet();
  }

  /// Read a value/type pair out of the specified record from slot 'Slot'.
  /// Increment Slot past the number of slots used in the record. Return true on
  /// failure.
  bool getValueTypePair(SmallVectorImpl<uint64_t> &Record, unsigned &Slot,
                        unsigned InstNum, Value *&ResVal) {
    if (Slot == Record.size()) return true;
    unsigned ValNo = (unsigned)Record[Slot++];
    // Adjust the ValNo, if it was encoded relative to the InstNum.
    if (UseRelativeIDs)
      ValNo = InstNum - ValNo;
    if (ValNo < InstNum) {
      // If this is not a forward reference, just return the value we already
      // have.
      ResVal = getFnValueByID(ValNo, nullptr);
      return ResVal == nullptr;
    }
    if (Slot == Record.size())
      return true;

    unsigned TypeNo = (unsigned)Record[Slot++];
    ResVal = getFnValueByID(ValNo, getTypeByID(TypeNo));
    return ResVal == nullptr;
  }

  /// Read a value out of the specified record from slot 'Slot'. Increment Slot
  /// past the number of slots used by the value in the record. Return true if
  /// there is an error.
  bool popValue(SmallVectorImpl<uint64_t> &Record, unsigned &Slot,
                unsigned InstNum, Type *Ty, Value *&ResVal) {
    if (getValue(Record, Slot, InstNum, Ty, ResVal))
      return true;
    // All values currently take a single record slot.
    ++Slot;
    return false;
  }

  /// Like popValue, but does not increment the Slot number.
  bool getValue(SmallVectorImpl<uint64_t> &Record, unsigned Slot,
                unsigned InstNum, Type *Ty, Value *&ResVal) {
    ResVal = getValue(Record, Slot, InstNum, Ty);
    return ResVal == nullptr;
  }

  /// Version of getValue that returns ResVal directly, or 0 if there is an
  /// error.
  Value *getValue(SmallVectorImpl<uint64_t> &Record, unsigned Slot,
                  unsigned InstNum, Type *Ty) {
    if (Slot == Record.size()) return nullptr;
    unsigned ValNo = (unsigned)Record[Slot];
    // Adjust the ValNo, if it was encoded relative to the InstNum.
    if (UseRelativeIDs)
      ValNo = InstNum - ValNo;
    return getFnValueByID(ValNo, Ty);
  }

  /// Like getValue, but decodes signed VBRs.
  Value *getValueSigned(SmallVectorImpl<uint64_t> &Record, unsigned Slot,
                        unsigned InstNum, Type *Ty) {
    if (Slot == Record.size()) return nullptr;
    unsigned ValNo = (unsigned)decodeSignRotatedValue(Record[Slot]);
    // Adjust the ValNo, if it was encoded relative to the InstNum.
    if (UseRelativeIDs)
      ValNo = InstNum - ValNo;
    return getFnValueByID(ValNo, Ty);
  }

  /// Converts alignment exponent (i.e. power of two (or zero)) to the
  /// corresponding alignment to use. If alignment is too large, returns
  /// a corresponding error code.
  Error parseAlignmentValue(uint64_t Exponent, unsigned &Alignment);
  Error parseAttrKind(uint64_t Code, Attribute::AttrKind *Kind);
  Error parseModule(uint64_t ResumeBit, bool ShouldLazyLoadMetadata = false);
  Error parseAttributeBlock();
  Error parseAttributeGroupBlock();
  Error parseTypeTable();
  Error parseTypeTableBody();
  Error parseOperandBundleTags();

  Expected<Value *> recordValue(SmallVectorImpl<uint64_t> &Record,
                                unsigned NameIndex, Triple &TT);
  Error parseValueSymbolTable(uint64_t Offset = 0);
  Error parseConstants();
  Error rememberAndSkipFunctionBodies();
  Error rememberAndSkipFunctionBody();
  /// Save the positions of the Metadata blocks and skip parsing the blocks.
  Error rememberAndSkipMetadata();
  Error typeCheckLoadStoreInst(Type *ValType, Type *PtrType);
  Error parseFunctionBody(Function *F);
  Error globalCleanup();
  Error resolveGlobalAndIndirectSymbolInits();
  Error parseMetadata(bool ModuleLevel = false);
  Error parseMetadataStrings(ArrayRef<uint64_t> Record, StringRef Blob,
                             unsigned &NextMetadataNo);
  Error parseMetadataKinds();
  Error parseMetadataKindRecord(SmallVectorImpl<uint64_t> &Record);
  Error parseGlobalObjectAttachment(GlobalObject &GO,
                                    ArrayRef<uint64_t> Record);
  Error parseMetadataAttachment(Function &F);
  Error parseUseLists();
  Error findFunctionInStream(
      Function *F,
      DenseMap<Function *, uint64_t>::iterator DeferredFunctionInfoIterator);
};

/// Class to manage reading and parsing function summary index bitcode
/// files/sections.
class ModuleSummaryIndexBitcodeReader : public BitcodeReaderBase {
  /// The module index built during parsing.
  ModuleSummaryIndex &TheIndex;

  /// Indicates whether we have encountered a global value summary section
  /// yet during parsing.
  bool SeenGlobalValSummary = false;

  /// Indicates whether we have already parsed the VST, used for error checking.
  bool SeenValueSymbolTable = false;

  /// Set to the offset of the VST recorded in the MODULE_CODE_VSTOFFSET record.
  /// Used to enable on-demand parsing of the VST.
  uint64_t VSTOffset = 0;

  // Map to save ValueId to GUID association that was recorded in the
  // ValueSymbolTable. It is used after the VST is parsed to convert
  // call graph edges read from the function summary from referencing
  // callees by their ValueId to using the GUID instead, which is how
  // they are recorded in the summary index being built.
  // We save a second GUID which is the same as the first one, but ignoring the
  // linkage, i.e. for value other than local linkage they are identical.
  DenseMap<unsigned, std::pair<GlobalValue::GUID, GlobalValue::GUID>>
      ValueIdToCallGraphGUIDMap;

  /// Map populated during module path string table parsing, from the
  /// module ID to a string reference owned by the index's module
  /// path string table, used to correlate with combined index
  /// summary records.
  DenseMap<uint64_t, StringRef> ModuleIdMap;

  /// Original source file name recorded in a bitcode record.
  std::string SourceFileName;

public:
  ModuleSummaryIndexBitcodeReader(
      BitstreamCursor Stream, ModuleSummaryIndex &TheIndex);

  Error parseModule(StringRef ModulePath);

private:
  Error parseValueSymbolTable(
      uint64_t Offset,
      DenseMap<unsigned, GlobalValue::LinkageTypes> &ValueIdToLinkageMap);
  Error parseEntireSummary(StringRef ModulePath);
  Error parseModuleStringTable();
  std::pair<GlobalValue::GUID, GlobalValue::GUID>

  getGUIDFromValueId(unsigned ValueId);
  std::pair<GlobalValue::GUID, CalleeInfo::HotnessType>
  readCallGraphEdge(const SmallVector<uint64_t, 64> &Record, unsigned int &I,
                    bool IsOldProfileFormat, bool HasProfile);
};

} // end anonymous namespace

std::error_code llvm::errorToErrorCodeAndEmitErrors(LLVMContext &Ctx,
                                                    Error Err) {
  if (Err) {
    std::error_code EC;
    handleAllErrors(std::move(Err), [&](ErrorInfoBase &EIB) {
      EC = EIB.convertToErrorCode();
      Ctx.emitError(EIB.message());
    });
    return EC;
  }
  return std::error_code();
}

BitcodeReader::BitcodeReader(BitstreamCursor Stream,
                             StringRef ProducerIdentification,
                             LLVMContext &Context)
    : BitcodeReaderBase(std::move(Stream)), Context(Context),
      ValueList(Context), MetadataList(Context) {
  this->ProducerIdentification = ProducerIdentification;
}

Error BitcodeReader::materializeForwardReferencedFunctions() {
  if (WillMaterializeAllForwardRefs)
    return Error::success();

  // Prevent recursion.
  WillMaterializeAllForwardRefs = true;

  while (!BasicBlockFwdRefQueue.empty()) {
    Function *F = BasicBlockFwdRefQueue.front();
    BasicBlockFwdRefQueue.pop_front();
    assert(F && "Expected valid function");
    if (!BasicBlockFwdRefs.count(F))
      // Already materialized.
      continue;

    // Check for a function that isn't materializable to prevent an infinite
    // loop.  When parsing a blockaddress stored in a global variable, there
    // isn't a trivial way to check if a function will have a body without a
    // linear search through FunctionsWithBodies, so just check it here.
    if (!F->isMaterializable())
      return error("Never resolved function from blockaddress");

    // Try to materialize F.
    if (Error Err = materialize(F))
      return Err;
  }
  assert(BasicBlockFwdRefs.empty() && "Function missing from queue");

  // Reset state.
  WillMaterializeAllForwardRefs = false;
  return Error::success();
}

//===----------------------------------------------------------------------===//
//  Helper functions to implement forward reference resolution, etc.
//===----------------------------------------------------------------------===//

static bool hasImplicitComdat(size_t Val) {
  switch (Val) {
  default:
    return false;
  case 1:  // Old WeakAnyLinkage
  case 4:  // Old LinkOnceAnyLinkage
  case 10: // Old WeakODRLinkage
  case 11: // Old LinkOnceODRLinkage
    return true;
  }
}

static GlobalValue::LinkageTypes getDecodedLinkage(unsigned Val) {
  switch (Val) {
  default: // Map unknown/new linkages to external
  case 0:
    return GlobalValue::ExternalLinkage;
  case 2:
    return GlobalValue::AppendingLinkage;
  case 3:
    return GlobalValue::InternalLinkage;
  case 5:
    return GlobalValue::ExternalLinkage; // Obsolete DLLImportLinkage
  case 6:
    return GlobalValue::ExternalLinkage; // Obsolete DLLExportLinkage
  case 7:
    return GlobalValue::ExternalWeakLinkage;
  case 8:
    return GlobalValue::CommonLinkage;
  case 9:
    return GlobalValue::PrivateLinkage;
  case 12:
    return GlobalValue::AvailableExternallyLinkage;
  case 13:
    return GlobalValue::PrivateLinkage; // Obsolete LinkerPrivateLinkage
  case 14:
    return GlobalValue::PrivateLinkage; // Obsolete LinkerPrivateWeakLinkage
  case 15:
    return GlobalValue::ExternalLinkage; // Obsolete LinkOnceODRAutoHideLinkage
  case 1: // Old value with implicit comdat.
  case 16:
    return GlobalValue::WeakAnyLinkage;
  case 10: // Old value with implicit comdat.
  case 17:
    return GlobalValue::WeakODRLinkage;
  case 4: // Old value with implicit comdat.
  case 18:
    return GlobalValue::LinkOnceAnyLinkage;
  case 11: // Old value with implicit comdat.
  case 19:
    return GlobalValue::LinkOnceODRLinkage;
  }
}

/// Decode the flags for GlobalValue in the summary.
static GlobalValueSummary::GVFlags getDecodedGVSummaryFlags(uint64_t RawFlags,
                                                            uint64_t Version) {
  // Summary were not emitted before LLVM 3.9, we don't need to upgrade Linkage
  // like getDecodedLinkage() above. Any future change to the linkage enum and
  // to getDecodedLinkage() will need to be taken into account here as above.
  auto Linkage = GlobalValue::LinkageTypes(RawFlags & 0xF); // 4 bits
  RawFlags = RawFlags >> 4;
  bool NoRename = RawFlags & 0x1;
  bool IsNotViableToInline = RawFlags & 0x2;
  bool HasInlineAsmMaybeReferencingInternal = RawFlags & 0x4;
  return GlobalValueSummary::GVFlags(Linkage, NoRename,
                                     HasInlineAsmMaybeReferencingInternal,
                                     IsNotViableToInline);
}

static GlobalValue::VisibilityTypes getDecodedVisibility(unsigned Val) {
  switch (Val) {
  default: // Map unknown visibilities to default.
  case 0: return GlobalValue::DefaultVisibility;
  case 1: return GlobalValue::HiddenVisibility;
  case 2: return GlobalValue::ProtectedVisibility;
  }
}

static GlobalValue::DLLStorageClassTypes
getDecodedDLLStorageClass(unsigned Val) {
  switch (Val) {
  default: // Map unknown values to default.
  case 0: return GlobalValue::DefaultStorageClass;
  case 1: return GlobalValue::DLLImportStorageClass;
  case 2: return GlobalValue::DLLExportStorageClass;
  }
}

static GlobalVariable::ThreadLocalMode getDecodedThreadLocalMode(unsigned Val) {
  switch (Val) {
    case 0: return GlobalVariable::NotThreadLocal;
    default: // Map unknown non-zero value to general dynamic.
    case 1: return GlobalVariable::GeneralDynamicTLSModel;
    case 2: return GlobalVariable::LocalDynamicTLSModel;
    case 3: return GlobalVariable::InitialExecTLSModel;
    case 4: return GlobalVariable::LocalExecTLSModel;
  }
}

static GlobalVariable::UnnamedAddr getDecodedUnnamedAddrType(unsigned Val) {
  switch (Val) {
    default: // Map unknown to UnnamedAddr::None.
    case 0: return GlobalVariable::UnnamedAddr::None;
    case 1: return GlobalVariable::UnnamedAddr::Global;
    case 2: return GlobalVariable::UnnamedAddr::Local;
  }
}

static int getDecodedCastOpcode(unsigned Val) {
  switch (Val) {
  default: return -1;
  case bitc::CAST_TRUNC   : return Instruction::Trunc;
  case bitc::CAST_ZEXT    : return Instruction::ZExt;
  case bitc::CAST_SEXT    : return Instruction::SExt;
  case bitc::CAST_FPTOUI  : return Instruction::FPToUI;
  case bitc::CAST_FPTOSI  : return Instruction::FPToSI;
  case bitc::CAST_UITOFP  : return Instruction::UIToFP;
  case bitc::CAST_SITOFP  : return Instruction::SIToFP;
  case bitc::CAST_FPTRUNC : return Instruction::FPTrunc;
  case bitc::CAST_FPEXT   : return Instruction::FPExt;
  case bitc::CAST_PTRTOINT: return Instruction::PtrToInt;
  case bitc::CAST_INTTOPTR: return Instruction::IntToPtr;
  case bitc::CAST_BITCAST : return Instruction::BitCast;
  case bitc::CAST_ADDRSPACECAST: return Instruction::AddrSpaceCast;
  }
}

static int getDecodedBinaryOpcode(unsigned Val, Type *Ty) {
  bool IsFP = Ty->isFPOrFPVectorTy();
  // BinOps are only valid for int/fp or vector of int/fp types
  if (!IsFP && !Ty->isIntOrIntVectorTy())
    return -1;

  switch (Val) {
  default:
    return -1;
  case bitc::BINOP_ADD:
    return IsFP ? Instruction::FAdd : Instruction::Add;
  case bitc::BINOP_SUB:
    return IsFP ? Instruction::FSub : Instruction::Sub;
  case bitc::BINOP_MUL:
    return IsFP ? Instruction::FMul : Instruction::Mul;
  case bitc::BINOP_UDIV:
    return IsFP ? -1 : Instruction::UDiv;
  case bitc::BINOP_SDIV:
    return IsFP ? Instruction::FDiv : Instruction::SDiv;
  case bitc::BINOP_UREM:
    return IsFP ? -1 : Instruction::URem;
  case bitc::BINOP_SREM:
    return IsFP ? Instruction::FRem : Instruction::SRem;
  case bitc::BINOP_SHL:
    return IsFP ? -1 : Instruction::Shl;
  case bitc::BINOP_LSHR:
    return IsFP ? -1 : Instruction::LShr;
  case bitc::BINOP_ASHR:
    return IsFP ? -1 : Instruction::AShr;
  case bitc::BINOP_AND:
    return IsFP ? -1 : Instruction::And;
  case bitc::BINOP_OR:
    return IsFP ? -1 : Instruction::Or;
  case bitc::BINOP_XOR:
    return IsFP ? -1 : Instruction::Xor;
  }
}

static AtomicRMWInst::BinOp getDecodedRMWOperation(unsigned Val) {
  switch (Val) {
  default: return AtomicRMWInst::BAD_BINOP;
  case bitc::RMW_XCHG: return AtomicRMWInst::Xchg;
  case bitc::RMW_ADD: return AtomicRMWInst::Add;
  case bitc::RMW_SUB: return AtomicRMWInst::Sub;
  case bitc::RMW_AND: return AtomicRMWInst::And;
  case bitc::RMW_NAND: return AtomicRMWInst::Nand;
  case bitc::RMW_OR: return AtomicRMWInst::Or;
  case bitc::RMW_XOR: return AtomicRMWInst::Xor;
  case bitc::RMW_MAX: return AtomicRMWInst::Max;
  case bitc::RMW_MIN: return AtomicRMWInst::Min;
  case bitc::RMW_UMAX: return AtomicRMWInst::UMax;
  case bitc::RMW_UMIN: return AtomicRMWInst::UMin;
  }
}

static AtomicOrdering getDecodedOrdering(unsigned Val) {
  switch (Val) {
  case bitc::ORDERING_NOTATOMIC: return AtomicOrdering::NotAtomic;
  case bitc::ORDERING_UNORDERED: return AtomicOrdering::Unordered;
  case bitc::ORDERING_MONOTONIC: return AtomicOrdering::Monotonic;
  case bitc::ORDERING_ACQUIRE: return AtomicOrdering::Acquire;
  case bitc::ORDERING_RELEASE: return AtomicOrdering::Release;
  case bitc::ORDERING_ACQREL: return AtomicOrdering::AcquireRelease;
  default: // Map unknown orderings to sequentially-consistent.
  case bitc::ORDERING_SEQCST: return AtomicOrdering::SequentiallyConsistent;
  }
}

static SynchronizationScope getDecodedSynchScope(unsigned Val) {
  if (Val >= bitc::SYNCHSCOPE_FIRSTTARGETSPECIFIC) {
    assert(Val == uint8_t(Val) && "expected 8-bit integer (too large)");
    return SynchronizationScope(SynchronizationScopeFirstTargetSpecific +
        (Val - bitc::SYNCHSCOPE_FIRSTTARGETSPECIFIC));
  }

  switch (Val) {
  default: llvm_unreachable("Invalid syncscope");
  case bitc::SYNCHSCOPE_SINGLETHREAD: return SingleThread;
  case bitc::SYNCHSCOPE_CROSSTHREAD: return CrossThread;
  }
}

static Comdat::SelectionKind getDecodedComdatSelectionKind(unsigned Val) {
  switch (Val) {
  default: // Map unknown selection kinds to any.
  case bitc::COMDAT_SELECTION_KIND_ANY:
    return Comdat::Any;
  case bitc::COMDAT_SELECTION_KIND_EXACT_MATCH:
    return Comdat::ExactMatch;
  case bitc::COMDAT_SELECTION_KIND_LARGEST:
    return Comdat::Largest;
  case bitc::COMDAT_SELECTION_KIND_NO_DUPLICATES:
    return Comdat::NoDuplicates;
  case bitc::COMDAT_SELECTION_KIND_SAME_SIZE:
    return Comdat::SameSize;
  }
}

static FastMathFlags getDecodedFastMathFlags(unsigned Val) {
  FastMathFlags FMF;
  if (0 != (Val & FastMathFlags::UnsafeAlgebra))
    FMF.setUnsafeAlgebra();
  if (0 != (Val & FastMathFlags::NoNaNs))
    FMF.setNoNaNs();
  if (0 != (Val & FastMathFlags::NoInfs))
    FMF.setNoInfs();
  if (0 != (Val & FastMathFlags::NoSignedZeros))
    FMF.setNoSignedZeros();
  if (0 != (Val & FastMathFlags::AllowReciprocal))
    FMF.setAllowReciprocal();
  return FMF;
}

static void upgradeDLLImportExportLinkage(GlobalValue *GV, unsigned Val) {
  switch (Val) {
  case 5: GV->setDLLStorageClass(GlobalValue::DLLImportStorageClass); break;
  case 6: GV->setDLLStorageClass(GlobalValue::DLLExportStorageClass); break;
  }
}

namespace llvm {
namespace {

/// \brief A class for maintaining the slot number definition
/// as a placeholder for the actual definition for forward constants defs.
class ConstantPlaceHolder : public ConstantExpr {
  void operator=(const ConstantPlaceHolder &) = delete;

public:
  // allocate space for exactly one operand
  void *operator new(size_t s) { return User::operator new(s, 1); }
  explicit ConstantPlaceHolder(Type *Ty, LLVMContext &Context)
      : ConstantExpr(Ty, Instruction::UserOp1, &Op<0>(), 1) {
    Op<0>() = UndefValue::get(Type::getInt32Ty(Context));
  }

  /// \brief Methods to support type inquiry through isa, cast, and dyn_cast.
  static bool classof(const Value *V) {
    return isa<ConstantExpr>(V) &&
           cast<ConstantExpr>(V)->getOpcode() == Instruction::UserOp1;
  }

  /// Provide fast operand accessors
  DECLARE_TRANSPARENT_OPERAND_ACCESSORS(Value);
};

} // end anonymous namespace

// FIXME: can we inherit this from ConstantExpr?
template <>
struct OperandTraits<ConstantPlaceHolder> :
  public FixedNumOperandTraits<ConstantPlaceHolder, 1> {
};
DEFINE_TRANSPARENT_OPERAND_ACCESSORS(ConstantPlaceHolder, Value)

} // end namespace llvm

void BitcodeReaderValueList::assignValue(Value *V, unsigned Idx) {
  if (Idx == size()) {
    push_back(V);
    return;
  }

  if (Idx >= size())
    resize(Idx+1);

  WeakVH &OldV = ValuePtrs[Idx];
  if (!OldV) {
    OldV = V;
    return;
  }

  // Handle constants and non-constants (e.g. instrs) differently for
  // efficiency.
  if (Constant *PHC = dyn_cast<Constant>(&*OldV)) {
    ResolveConstants.push_back(std::make_pair(PHC, Idx));
    OldV = V;
  } else {
    // If there was a forward reference to this value, replace it.
    Value *PrevVal = OldV;
    OldV->replaceAllUsesWith(V);
    delete PrevVal;
  }
}

Constant *BitcodeReaderValueList::getConstantFwdRef(unsigned Idx,
                                                    Type *Ty) {
  if (Idx >= size())
    resize(Idx + 1);

  if (Value *V = ValuePtrs[Idx]) {
    if (Ty != V->getType())
      report_fatal_error("Type mismatch in constant table!");
    return cast<Constant>(V);
  }

  // Create and return a placeholder, which will later be RAUW'd.
  Constant *C = new ConstantPlaceHolder(Ty, Context);
  ValuePtrs[Idx] = C;
  return C;
}

Value *BitcodeReaderValueList::getValueFwdRef(unsigned Idx, Type *Ty) {
  // Bail out for a clearly invalid value. This would make us call resize(0)
  if (Idx == std::numeric_limits<unsigned>::max())
    return nullptr;

  if (Idx >= size())
    resize(Idx + 1);

  if (Value *V = ValuePtrs[Idx]) {
    // If the types don't match, it's invalid.
    if (Ty && Ty != V->getType())
      return nullptr;
    return V;
  }

  // No type specified, must be invalid reference.
  if (!Ty) return nullptr;

  // Create and return a placeholder, which will later be RAUW'd.
  Value *V = new Argument(Ty);
  ValuePtrs[Idx] = V;
  return V;
}

/// Once all constants are read, this method bulk resolves any forward
/// references.  The idea behind this is that we sometimes get constants (such
/// as large arrays) which reference *many* forward ref constants.  Replacing
/// each of these causes a lot of thrashing when building/reuniquing the
/// constant.  Instead of doing this, we look at all the uses and rewrite all
/// the place holders at once for any constant that uses a placeholder.
void BitcodeReaderValueList::resolveConstantForwardRefs() {
  // Sort the values by-pointer so that they are efficient to look up with a
  // binary search.
  std::sort(ResolveConstants.begin(), ResolveConstants.end());

  SmallVector<Constant*, 64> NewOps;

  while (!ResolveConstants.empty()) {
    Value *RealVal = operator[](ResolveConstants.back().second);
    Constant *Placeholder = ResolveConstants.back().first;
    ResolveConstants.pop_back();

    // Loop over all users of the placeholder, updating them to reference the
    // new value.  If they reference more than one placeholder, update them all
    // at once.
    while (!Placeholder->use_empty()) {
      auto UI = Placeholder->user_begin();
      User *U = *UI;

      // If the using object isn't uniqued, just update the operands.  This
      // handles instructions and initializers for global variables.
      if (!isa<Constant>(U) || isa<GlobalValue>(U)) {
        UI.getUse().set(RealVal);
        continue;
      }

      // Otherwise, we have a constant that uses the placeholder.  Replace that
      // constant with a new constant that has *all* placeholder uses updated.
      Constant *UserC = cast<Constant>(U);
      for (User::op_iterator I = UserC->op_begin(), E = UserC->op_end();
           I != E; ++I) {
        Value *NewOp;
        if (!isa<ConstantPlaceHolder>(*I)) {
          // Not a placeholder reference.
          NewOp = *I;
        } else if (*I == Placeholder) {
          // Common case is that it just references this one placeholder.
          NewOp = RealVal;
        } else {
          // Otherwise, look up the placeholder in ResolveConstants.
          ResolveConstantsTy::iterator It =
            std::lower_bound(ResolveConstants.begin(), ResolveConstants.end(),
                             std::pair<Constant*, unsigned>(cast<Constant>(*I),
                                                            0));
          assert(It != ResolveConstants.end() && It->first == *I);
          NewOp = operator[](It->second);
        }

        NewOps.push_back(cast<Constant>(NewOp));
      }

      // Make the new constant.
      Constant *NewC;
      if (ConstantArray *UserCA = dyn_cast<ConstantArray>(UserC)) {
        NewC = ConstantArray::get(UserCA->getType(), NewOps);
      } else if (ConstantStruct *UserCS = dyn_cast<ConstantStruct>(UserC)) {
        NewC = ConstantStruct::get(UserCS->getType(), NewOps);
      } else if (isa<ConstantVector>(UserC)) {
        NewC = ConstantVector::get(NewOps);
      } else {
        assert(isa<ConstantExpr>(UserC) && "Must be a ConstantExpr.");
        NewC = cast<ConstantExpr>(UserC)->getWithOperands(NewOps);
      }

      UserC->replaceAllUsesWith(NewC);
      UserC->destroyConstant();
      NewOps.clear();
    }

    // Update all ValueHandles, they should be the only users at this point.
    Placeholder->replaceAllUsesWith(RealVal);
    delete Placeholder;
  }
}

void BitcodeReaderMetadataList::assignValue(Metadata *MD, unsigned Idx) {
  if (Idx == size()) {
    push_back(MD);
    return;
  }

  if (Idx >= size())
    resize(Idx+1);

  TrackingMDRef &OldMD = MetadataPtrs[Idx];
  if (!OldMD) {
    OldMD.reset(MD);
    return;
  }

  // If there was a forward reference to this value, replace it.
  TempMDTuple PrevMD(cast<MDTuple>(OldMD.get()));
  PrevMD->replaceAllUsesWith(MD);
  --NumFwdRefs;
}

Metadata *BitcodeReaderMetadataList::getMetadataFwdRef(unsigned Idx) {
  if (Idx >= size())
    resize(Idx + 1);

  if (Metadata *MD = MetadataPtrs[Idx])
    return MD;

  // Track forward refs to be resolved later.
  if (AnyFwdRefs) {
    MinFwdRef = std::min(MinFwdRef, Idx);
    MaxFwdRef = std::max(MaxFwdRef, Idx);
  } else {
    AnyFwdRefs = true;
    MinFwdRef = MaxFwdRef = Idx;
  }
  ++NumFwdRefs;

  // Create and return a placeholder, which will later be RAUW'd.
  Metadata *MD = MDNode::getTemporary(Context, None).release();
  MetadataPtrs[Idx].reset(MD);
  return MD;
}

Metadata *BitcodeReaderMetadataList::getMetadataIfResolved(unsigned Idx) {
  Metadata *MD = lookup(Idx);
  if (auto *N = dyn_cast_or_null<MDNode>(MD))
    if (!N->isResolved())
      return nullptr;
  return MD;
}

MDNode *BitcodeReaderMetadataList::getMDNodeFwdRefOrNull(unsigned Idx) {
  return dyn_cast_or_null<MDNode>(getMetadataFwdRef(Idx));
}

void BitcodeReaderMetadataList::tryToResolveCycles() {
  if (NumFwdRefs)
    // Still forward references... can't resolve cycles.
    return;

  bool DidReplaceTypeRefs = false;

  // Give up on finding a full definition for any forward decls that remain.
  for (const auto &Ref : OldTypeRefs.FwdDecls)
    OldTypeRefs.Final.insert(Ref);
  OldTypeRefs.FwdDecls.clear();

  // Upgrade from old type ref arrays.  In strange cases, this could add to
  // OldTypeRefs.Unknown.
  for (const auto &Array : OldTypeRefs.Arrays) {
    DidReplaceTypeRefs = true;
    Array.second->replaceAllUsesWith(resolveTypeRefArray(Array.first.get()));
  }
  OldTypeRefs.Arrays.clear();

  // Replace old string-based type refs with the resolved node, if possible.
  // If we haven't seen the node, leave it to the verifier to complain about
  // the invalid string reference.
  for (const auto &Ref : OldTypeRefs.Unknown) {
    DidReplaceTypeRefs = true;
    if (DICompositeType *CT = OldTypeRefs.Final.lookup(Ref.first))
      Ref.second->replaceAllUsesWith(CT);
    else
      Ref.second->replaceAllUsesWith(Ref.first);
  }
  OldTypeRefs.Unknown.clear();

  // Make sure all the upgraded types are resolved.
  if (DidReplaceTypeRefs) {
    AnyFwdRefs = true;
    MinFwdRef = 0;
    MaxFwdRef = MetadataPtrs.size() - 1;
  }

  if (!AnyFwdRefs)
    // Nothing to do.
    return;

  // Resolve any cycles.
  for (unsigned I = MinFwdRef, E = MaxFwdRef + 1; I != E; ++I) {
    auto &MD = MetadataPtrs[I];
    auto *N = dyn_cast_or_null<MDNode>(MD);
    if (!N)
      continue;

    assert(!N->isTemporary() && "Unexpected forward reference");
    N->resolveCycles();
  }

  // Make sure we return early again until there's another forward ref.
  AnyFwdRefs = false;
}

void BitcodeReaderMetadataList::addTypeRef(MDString &UUID,
                                           DICompositeType &CT) {
  assert(CT.getRawIdentifier() == &UUID && "Mismatched UUID");
  if (CT.isForwardDecl())
    OldTypeRefs.FwdDecls.insert(std::make_pair(&UUID, &CT));
  else
    OldTypeRefs.Final.insert(std::make_pair(&UUID, &CT));
}

Metadata *BitcodeReaderMetadataList::upgradeTypeRef(Metadata *MaybeUUID) {
  auto *UUID = dyn_cast_or_null<MDString>(MaybeUUID);
  if (LLVM_LIKELY(!UUID))
    return MaybeUUID;

  if (auto *CT = OldTypeRefs.Final.lookup(UUID))
    return CT;

  auto &Ref = OldTypeRefs.Unknown[UUID];
  if (!Ref)
    Ref = MDNode::getTemporary(Context, None);
  return Ref.get();
}

Metadata *BitcodeReaderMetadataList::upgradeTypeRefArray(Metadata *MaybeTuple) {
  auto *Tuple = dyn_cast_or_null<MDTuple>(MaybeTuple);
  if (!Tuple || Tuple->isDistinct())
    return MaybeTuple;

  // Look through the array immediately if possible.
  if (!Tuple->isTemporary())
    return resolveTypeRefArray(Tuple);

  // Create and return a placeholder to use for now.  Eventually
  // resolveTypeRefArrays() will be resolve this forward reference.
  OldTypeRefs.Arrays.emplace_back(
      std::piecewise_construct, std::forward_as_tuple(Tuple),
      std::forward_as_tuple(MDTuple::getTemporary(Context, None)));
  return OldTypeRefs.Arrays.back().second.get();
}

Metadata *BitcodeReaderMetadataList::resolveTypeRefArray(Metadata *MaybeTuple) {
  auto *Tuple = dyn_cast_or_null<MDTuple>(MaybeTuple);
  if (!Tuple || Tuple->isDistinct())
    return MaybeTuple;

  // Look through the DITypeRefArray, upgrading each DITypeRef.
  SmallVector<Metadata *, 32> Ops;
  Ops.reserve(Tuple->getNumOperands());
  for (Metadata *MD : Tuple->operands())
    Ops.push_back(upgradeTypeRef(MD));

  return MDTuple::get(Context, Ops);
}

Type *BitcodeReader::getTypeByID(unsigned ID) {
  // The type table size is always specified correctly.
  if (ID >= TypeList.size())
    return nullptr;

  if (Type *Ty = TypeList[ID])
    return Ty;

  // If we have a forward reference, the only possible case is when it is to a
  // named struct.  Just create a placeholder for now.
  return TypeList[ID] = createIdentifiedStructType(Context);
}

StructType *BitcodeReader::createIdentifiedStructType(LLVMContext &Context,
                                                      StringRef Name) {
  auto *Ret = StructType::create(Context, Name);
  IdentifiedStructTypes.push_back(Ret);
  return Ret;
}

StructType *BitcodeReader::createIdentifiedStructType(LLVMContext &Context) {
  auto *Ret = StructType::create(Context);
  IdentifiedStructTypes.push_back(Ret);
  return Ret;
}

//===----------------------------------------------------------------------===//
//  Functions for parsing blocks from the bitcode file
//===----------------------------------------------------------------------===//

static uint64_t getRawAttributeMask(Attribute::AttrKind Val) {
  switch (Val) {
  case Attribute::EndAttrKinds:
    llvm_unreachable("Synthetic enumerators which should never get here");

  case Attribute::None:            return 0;
  case Attribute::ZExt:            return 1 << 0;
  case Attribute::SExt:            return 1 << 1;
  case Attribute::NoReturn:        return 1 << 2;
  case Attribute::InReg:           return 1 << 3;
  case Attribute::StructRet:       return 1 << 4;
  case Attribute::NoUnwind:        return 1 << 5;
  case Attribute::NoAlias:         return 1 << 6;
  case Attribute::ByVal:           return 1 << 7;
  case Attribute::Nest:            return 1 << 8;
  case Attribute::ReadNone:        return 1 << 9;
  case Attribute::ReadOnly:        return 1 << 10;
  case Attribute::NoInline:        return 1 << 11;
  case Attribute::AlwaysInline:    return 1 << 12;
  case Attribute::OptimizeForSize: return 1 << 13;
  case Attribute::StackProtect:    return 1 << 14;
  case Attribute::StackProtectReq: return 1 << 15;
  case Attribute::Alignment:       return 31 << 16;
  case Attribute::NoCapture:       return 1 << 21;
  case Attribute::NoRedZone:       return 1 << 22;
  case Attribute::NoImplicitFloat: return 1 << 23;
  case Attribute::Naked:           return 1 << 24;
  case Attribute::InlineHint:      return 1 << 25;
  case Attribute::StackAlignment:  return 7 << 26;
  case Attribute::ReturnsTwice:    return 1 << 29;
  case Attribute::UWTable:         return 1 << 30;
  case Attribute::NonLazyBind:     return 1U << 31;
  case Attribute::SanitizeAddress: return 1ULL << 32;
  case Attribute::MinSize:         return 1ULL << 33;
  case Attribute::NoDuplicate:     return 1ULL << 34;
  case Attribute::StackProtectStrong: return 1ULL << 35;
  case Attribute::SanitizeThread:  return 1ULL << 36;
  case Attribute::SanitizeMemory:  return 1ULL << 37;
  case Attribute::NoBuiltin:       return 1ULL << 38;
  case Attribute::Returned:        return 1ULL << 39;
  case Attribute::Cold:            return 1ULL << 40;
  case Attribute::Builtin:         return 1ULL << 41;
  case Attribute::OptimizeNone:    return 1ULL << 42;
  case Attribute::InAlloca:        return 1ULL << 43;
  case Attribute::NonNull:         return 1ULL << 44;
  case Attribute::JumpTable:       return 1ULL << 45;
  case Attribute::Convergent:      return 1ULL << 46;
  case Attribute::SafeStack:       return 1ULL << 47;
  case Attribute::NoRecurse:       return 1ULL << 48;
  case Attribute::InaccessibleMemOnly:         return 1ULL << 49;
  case Attribute::InaccessibleMemOrArgMemOnly: return 1ULL << 50;
  case Attribute::SwiftSelf:       return 1ULL << 51;
  case Attribute::SwiftError:      return 1ULL << 52;
  case Attribute::WriteOnly:       return 1ULL << 53;
  case Attribute::Dereferenceable:
    llvm_unreachable("dereferenceable attribute not supported in raw format");
    break;
  case Attribute::DereferenceableOrNull:
    llvm_unreachable("dereferenceable_or_null attribute not supported in raw "
                     "format");
    break;
  case Attribute::ArgMemOnly:
    llvm_unreachable("argmemonly attribute not supported in raw format");
    break;
  case Attribute::AllocSize:
    llvm_unreachable("allocsize not supported in raw format");
    break;
  }
  llvm_unreachable("Unsupported attribute type");
}

static void addRawAttributeValue(AttrBuilder &B, uint64_t Val) {
  if (!Val) return;

  for (Attribute::AttrKind I = Attribute::None; I != Attribute::EndAttrKinds;
       I = Attribute::AttrKind(I + 1)) {
    if (I == Attribute::Dereferenceable ||
        I == Attribute::DereferenceableOrNull ||
        I == Attribute::ArgMemOnly ||
        I == Attribute::AllocSize)
      continue;
    if (uint64_t A = (Val & getRawAttributeMask(I))) {
      if (I == Attribute::Alignment)
        B.addAlignmentAttr(1ULL << ((A >> 16) - 1));
      else if (I == Attribute::StackAlignment)
        B.addStackAlignmentAttr(1ULL << ((A >> 26)-1));
      else
        B.addAttribute(I);
    }
  }
}

/// \brief This fills an AttrBuilder object with the LLVM attributes that have
/// been decoded from the given integer. This function must stay in sync with
/// 'encodeLLVMAttributesForBitcode'.
static void decodeLLVMAttributesForBitcode(AttrBuilder &B,
                                           uint64_t EncodedAttrs) {
  // FIXME: Remove in 4.0.

  // The alignment is stored as a 16-bit raw value from bits 31--16.  We shift
  // the bits above 31 down by 11 bits.
  unsigned Alignment = (EncodedAttrs & (0xffffULL << 16)) >> 16;
  assert((!Alignment || isPowerOf2_32(Alignment)) &&
         "Alignment must be a power of two.");

  if (Alignment)
    B.addAlignmentAttr(Alignment);
  addRawAttributeValue(B, ((EncodedAttrs & (0xfffffULL << 32)) >> 11) |
                          (EncodedAttrs & 0xffff));
}

Error BitcodeReader::parseAttributeBlock() {
  if (Stream.EnterSubBlock(bitc::PARAMATTR_BLOCK_ID))
    return error("Invalid record");

  if (!MAttributes.empty())
    return error("Invalid multiple blocks");

  SmallVector<uint64_t, 64> Record;

  SmallVector<AttributeSet, 8> Attrs;

  // Read all the records.
  while (true) {
    BitstreamEntry Entry = Stream.advanceSkippingSubblocks();

    switch (Entry.Kind) {
    case BitstreamEntry::SubBlock: // Handled for us already.
    case BitstreamEntry::Error:
      return error("Malformed block");
    case BitstreamEntry::EndBlock:
      return Error::success();
    case BitstreamEntry::Record:
      // The interesting case.
      break;
    }

    // Read a record.
    Record.clear();
    switch (Stream.readRecord(Entry.ID, Record)) {
    default:  // Default behavior: ignore.
      break;
    case bitc::PARAMATTR_CODE_ENTRY_OLD: { // ENTRY: [paramidx0, attr0, ...]
      // FIXME: Remove in 4.0.
      if (Record.size() & 1)
        return error("Invalid record");

      for (unsigned i = 0, e = Record.size(); i != e; i += 2) {
        AttrBuilder B;
        decodeLLVMAttributesForBitcode(B, Record[i+1]);
        Attrs.push_back(AttributeSet::get(Context, Record[i], B));
      }

      MAttributes.push_back(AttributeSet::get(Context, Attrs));
      Attrs.clear();
      break;
    }
    case bitc::PARAMATTR_CODE_ENTRY: { // ENTRY: [attrgrp0, attrgrp1, ...]
      for (unsigned i = 0, e = Record.size(); i != e; ++i)
        Attrs.push_back(MAttributeGroups[Record[i]]);

      MAttributes.push_back(AttributeSet::get(Context, Attrs));
      Attrs.clear();
      break;
    }
    }
  }
}

// Returns Attribute::None on unrecognized codes.
static Attribute::AttrKind getAttrFromCode(uint64_t Code) {
  switch (Code) {
  default:
    return Attribute::None;
  case bitc::ATTR_KIND_ALIGNMENT:
    return Attribute::Alignment;
  case bitc::ATTR_KIND_ALWAYS_INLINE:
    return Attribute::AlwaysInline;
  case bitc::ATTR_KIND_ARGMEMONLY:
    return Attribute::ArgMemOnly;
  case bitc::ATTR_KIND_BUILTIN:
    return Attribute::Builtin;
  case bitc::ATTR_KIND_BY_VAL:
    return Attribute::ByVal;
  case bitc::ATTR_KIND_IN_ALLOCA:
    return Attribute::InAlloca;
  case bitc::ATTR_KIND_COLD:
    return Attribute::Cold;
  case bitc::ATTR_KIND_CONVERGENT:
    return Attribute::Convergent;
  case bitc::ATTR_KIND_INACCESSIBLEMEM_ONLY:
    return Attribute::InaccessibleMemOnly;
  case bitc::ATTR_KIND_INACCESSIBLEMEM_OR_ARGMEMONLY:
    return Attribute::InaccessibleMemOrArgMemOnly;
  case bitc::ATTR_KIND_INLINE_HINT:
    return Attribute::InlineHint;
  case bitc::ATTR_KIND_IN_REG:
    return Attribute::InReg;
  case bitc::ATTR_KIND_JUMP_TABLE:
    return Attribute::JumpTable;
  case bitc::ATTR_KIND_MIN_SIZE:
    return Attribute::MinSize;
  case bitc::ATTR_KIND_NAKED:
    return Attribute::Naked;
  case bitc::ATTR_KIND_NEST:
    return Attribute::Nest;
  case bitc::ATTR_KIND_NO_ALIAS:
    return Attribute::NoAlias;
  case bitc::ATTR_KIND_NO_BUILTIN:
    return Attribute::NoBuiltin;
  case bitc::ATTR_KIND_NO_CAPTURE:
    return Attribute::NoCapture;
  case bitc::ATTR_KIND_NO_DUPLICATE:
    return Attribute::NoDuplicate;
  case bitc::ATTR_KIND_NO_IMPLICIT_FLOAT:
    return Attribute::NoImplicitFloat;
  case bitc::ATTR_KIND_NO_INLINE:
    return Attribute::NoInline;
  case bitc::ATTR_KIND_NO_RECURSE:
    return Attribute::NoRecurse;
  case bitc::ATTR_KIND_NON_LAZY_BIND:
    return Attribute::NonLazyBind;
  case bitc::ATTR_KIND_NON_NULL:
    return Attribute::NonNull;
  case bitc::ATTR_KIND_DEREFERENCEABLE:
    return Attribute::Dereferenceable;
  case bitc::ATTR_KIND_DEREFERENCEABLE_OR_NULL:
    return Attribute::DereferenceableOrNull;
  case bitc::ATTR_KIND_ALLOC_SIZE:
    return Attribute::AllocSize;
  case bitc::ATTR_KIND_NO_RED_ZONE:
    return Attribute::NoRedZone;
  case bitc::ATTR_KIND_NO_RETURN:
    return Attribute::NoReturn;
  case bitc::ATTR_KIND_NO_UNWIND:
    return Attribute::NoUnwind;
  case bitc::ATTR_KIND_OPTIMIZE_FOR_SIZE:
    return Attribute::OptimizeForSize;
  case bitc::ATTR_KIND_OPTIMIZE_NONE:
    return Attribute::OptimizeNone;
  case bitc::ATTR_KIND_READ_NONE:
    return Attribute::ReadNone;
  case bitc::ATTR_KIND_READ_ONLY:
    return Attribute::ReadOnly;
  case bitc::ATTR_KIND_RETURNED:
    return Attribute::Returned;
  case bitc::ATTR_KIND_RETURNS_TWICE:
    return Attribute::ReturnsTwice;
  case bitc::ATTR_KIND_S_EXT:
    return Attribute::SExt;
  case bitc::ATTR_KIND_STACK_ALIGNMENT:
    return Attribute::StackAlignment;
  case bitc::ATTR_KIND_STACK_PROTECT:
    return Attribute::StackProtect;
  case bitc::ATTR_KIND_STACK_PROTECT_REQ:
    return Attribute::StackProtectReq;
  case bitc::ATTR_KIND_STACK_PROTECT_STRONG:
    return Attribute::StackProtectStrong;
  case bitc::ATTR_KIND_SAFESTACK:
    return Attribute::SafeStack;
  case bitc::ATTR_KIND_STRUCT_RET:
    return Attribute::StructRet;
  case bitc::ATTR_KIND_SANITIZE_ADDRESS:
    return Attribute::SanitizeAddress;
  case bitc::ATTR_KIND_SANITIZE_THREAD:
    return Attribute::SanitizeThread;
  case bitc::ATTR_KIND_SANITIZE_MEMORY:
    return Attribute::SanitizeMemory;
  case bitc::ATTR_KIND_SWIFT_ERROR:
    return Attribute::SwiftError;
  case bitc::ATTR_KIND_SWIFT_SELF:
    return Attribute::SwiftSelf;
  case bitc::ATTR_KIND_UW_TABLE:
    return Attribute::UWTable;
  case bitc::ATTR_KIND_WRITEONLY:
    return Attribute::WriteOnly;
  case bitc::ATTR_KIND_Z_EXT:
    return Attribute::ZExt;
  }
}

Error BitcodeReader::parseAlignmentValue(uint64_t Exponent,
                                         unsigned &Alignment) {
  // Note: Alignment in bitcode files is incremented by 1, so that zero
  // can be used for default alignment.
  if (Exponent > Value::MaxAlignmentExponent + 1)
    return error("Invalid alignment value");
  Alignment = (1 << static_cast<unsigned>(Exponent)) >> 1;
  return Error::success();
}

Error BitcodeReader::parseAttrKind(uint64_t Code, Attribute::AttrKind *Kind) {
  *Kind = getAttrFromCode(Code);
  if (*Kind == Attribute::None)
    return error("Unknown attribute kind (" + Twine(Code) + ")");
  return Error::success();
}

Error BitcodeReader::parseAttributeGroupBlock() {
  if (Stream.EnterSubBlock(bitc::PARAMATTR_GROUP_BLOCK_ID))
    return error("Invalid record");

  if (!MAttributeGroups.empty())
    return error("Invalid multiple blocks");

  SmallVector<uint64_t, 64> Record;

  // Read all the records.
  while (true) {
    BitstreamEntry Entry = Stream.advanceSkippingSubblocks();

    switch (Entry.Kind) {
    case BitstreamEntry::SubBlock: // Handled for us already.
    case BitstreamEntry::Error:
      return error("Malformed block");
    case BitstreamEntry::EndBlock:
      return Error::success();
    case BitstreamEntry::Record:
      // The interesting case.
      break;
    }

    // Read a record.
    Record.clear();
    switch (Stream.readRecord(Entry.ID, Record)) {
    default:  // Default behavior: ignore.
      break;
    case bitc::PARAMATTR_GRP_CODE_ENTRY: { // ENTRY: [grpid, idx, a0, a1, ...]
      if (Record.size() < 3)
        return error("Invalid record");

      uint64_t GrpID = Record[0];
      uint64_t Idx = Record[1]; // Index of the object this attribute refers to.

      AttrBuilder B;
      for (unsigned i = 2, e = Record.size(); i != e; ++i) {
        if (Record[i] == 0) {        // Enum attribute
          Attribute::AttrKind Kind;
          if (Error Err = parseAttrKind(Record[++i], &Kind))
            return Err;

          B.addAttribute(Kind);
        } else if (Record[i] == 1) { // Integer attribute
          Attribute::AttrKind Kind;
          if (Error Err = parseAttrKind(Record[++i], &Kind))
            return Err;
          if (Kind == Attribute::Alignment)
            B.addAlignmentAttr(Record[++i]);
          else if (Kind == Attribute::StackAlignment)
            B.addStackAlignmentAttr(Record[++i]);
          else if (Kind == Attribute::Dereferenceable)
            B.addDereferenceableAttr(Record[++i]);
          else if (Kind == Attribute::DereferenceableOrNull)
            B.addDereferenceableOrNullAttr(Record[++i]);
          else if (Kind == Attribute::AllocSize)
            B.addAllocSizeAttrFromRawRepr(Record[++i]);
        } else {                     // String attribute
          assert((Record[i] == 3 || Record[i] == 4) &&
                 "Invalid attribute group entry");
          bool HasValue = (Record[i++] == 4);
          SmallString<64> KindStr;
          SmallString<64> ValStr;

          while (Record[i] != 0 && i != e)
            KindStr += Record[i++];
          assert(Record[i] == 0 && "Kind string not null terminated");

          if (HasValue) {
            // Has a value associated with it.
            ++i; // Skip the '0' that terminates the "kind" string.
            while (Record[i] != 0 && i != e)
              ValStr += Record[i++];
            assert(Record[i] == 0 && "Value string not null terminated");
          }

          B.addAttribute(KindStr.str(), ValStr.str());
        }
      }

      MAttributeGroups[GrpID] = AttributeSet::get(Context, Idx, B);
      break;
    }
    }
  }
}

Error BitcodeReader::parseTypeTable() {
  if (Stream.EnterSubBlock(bitc::TYPE_BLOCK_ID_NEW))
    return error("Invalid record");

  return parseTypeTableBody();
}

Error BitcodeReader::parseTypeTableBody() {
  if (!TypeList.empty())
    return error("Invalid multiple blocks");

  SmallVector<uint64_t, 64> Record;
  unsigned NumRecords = 0;

  SmallString<64> TypeName;

  // Read all the records for this type table.
  while (true) {
    BitstreamEntry Entry = Stream.advanceSkippingSubblocks();

    switch (Entry.Kind) {
    case BitstreamEntry::SubBlock: // Handled for us already.
    case BitstreamEntry::Error:
      return error("Malformed block");
    case BitstreamEntry::EndBlock:
      if (NumRecords != TypeList.size())
        return error("Malformed block");
      return Error::success();
    case BitstreamEntry::Record:
      // The interesting case.
      break;
    }

    // Read a record.
    Record.clear();
    Type *ResultTy = nullptr;
    switch (Stream.readRecord(Entry.ID, Record)) {
    default:
      return error("Invalid value");
    case bitc::TYPE_CODE_NUMENTRY: // TYPE_CODE_NUMENTRY: [numentries]
      // TYPE_CODE_NUMENTRY contains a count of the number of types in the
      // type list.  This allows us to reserve space.
      if (Record.size() < 1)
        return error("Invalid record");
      TypeList.resize(Record[0]);
      continue;
    case bitc::TYPE_CODE_VOID:      // VOID
      ResultTy = Type::getVoidTy(Context);
      break;
    case bitc::TYPE_CODE_HALF:     // HALF
      ResultTy = Type::getHalfTy(Context);
      break;
    case bitc::TYPE_CODE_FLOAT:     // FLOAT
      ResultTy = Type::getFloatTy(Context);
      break;
    case bitc::TYPE_CODE_DOUBLE:    // DOUBLE
      ResultTy = Type::getDoubleTy(Context);
      break;
    case bitc::TYPE_CODE_X86_FP80:  // X86_FP80
      ResultTy = Type::getX86_FP80Ty(Context);
      break;
    case bitc::TYPE_CODE_FP128:     // FP128
      ResultTy = Type::getFP128Ty(Context);
      break;
    case bitc::TYPE_CODE_PPC_FP128: // PPC_FP128
      ResultTy = Type::getPPC_FP128Ty(Context);
      break;
    case bitc::TYPE_CODE_LABEL:     // LABEL
      ResultTy = Type::getLabelTy(Context);
      break;
    case bitc::TYPE_CODE_METADATA:  // METADATA
      ResultTy = Type::getMetadataTy(Context);
      break;
    case bitc::TYPE_CODE_X86_MMX:   // X86_MMX
      ResultTy = Type::getX86_MMXTy(Context);
      break;
    case bitc::TYPE_CODE_TOKEN:     // TOKEN
      ResultTy = Type::getTokenTy(Context);
      break;
    case bitc::TYPE_CODE_INTEGER: { // INTEGER: [width]
      if (Record.size() < 1)
        return error("Invalid record");

      uint64_t NumBits = Record[0];
      if (NumBits < IntegerType::MIN_INT_BITS ||
          NumBits > IntegerType::MAX_INT_BITS)
        return error("Bitwidth for integer type out of range");
      ResultTy = IntegerType::get(Context, NumBits);
      break;
    }
    case bitc::TYPE_CODE_POINTER: { // POINTER: [pointee type] or
                                    //          [pointee type, address space]
      if (Record.size() < 1)
        return error("Invalid record");
      unsigned AddressSpace = 0;
      if (Record.size() == 2)
        AddressSpace = Record[1];
      ResultTy = getTypeByID(Record[0]);
      if (!ResultTy ||
          !PointerType::isValidElementType(ResultTy))
        return error("Invalid type");
      ResultTy = PointerType::get(ResultTy, AddressSpace);
      break;
    }
    case bitc::TYPE_CODE_FUNCTION_OLD: {
      // FIXME: attrid is dead, remove it in LLVM 4.0
      // FUNCTION: [vararg, attrid, retty, paramty x N]
      if (Record.size() < 3)
        return error("Invalid record");
      SmallVector<Type*, 8> ArgTys;
      for (unsigned i = 3, e = Record.size(); i != e; ++i) {
        if (Type *T = getTypeByID(Record[i]))
          ArgTys.push_back(T);
        else
          break;
      }

      ResultTy = getTypeByID(Record[2]);
      if (!ResultTy || ArgTys.size() < Record.size()-3)
        return error("Invalid type");

      ResultTy = FunctionType::get(ResultTy, ArgTys, Record[0]);
      break;
    }
    case bitc::TYPE_CODE_FUNCTION: {
      // FUNCTION: [vararg, retty, paramty x N]
      if (Record.size() < 2)
        return error("Invalid record");
      SmallVector<Type*, 8> ArgTys;
      for (unsigned i = 2, e = Record.size(); i != e; ++i) {
        if (Type *T = getTypeByID(Record[i])) {
          if (!FunctionType::isValidArgumentType(T))
            return error("Invalid function argument type");
          ArgTys.push_back(T);
        }
        else
          break;
      }

      ResultTy = getTypeByID(Record[1]);
      if (!ResultTy || ArgTys.size() < Record.size()-2)
        return error("Invalid type");

      ResultTy = FunctionType::get(ResultTy, ArgTys, Record[0]);
      break;
    }
    case bitc::TYPE_CODE_STRUCT_ANON: {  // STRUCT: [ispacked, eltty x N]
      if (Record.size() < 1)
        return error("Invalid record");
      SmallVector<Type*, 8> EltTys;
      for (unsigned i = 1, e = Record.size(); i != e; ++i) {
        if (Type *T = getTypeByID(Record[i]))
          EltTys.push_back(T);
        else
          break;
      }
      if (EltTys.size() != Record.size()-1)
        return error("Invalid type");
      ResultTy = StructType::get(Context, EltTys, Record[0]);
      break;
    }
    case bitc::TYPE_CODE_STRUCT_NAME:   // STRUCT_NAME: [strchr x N]
      if (convertToString(Record, 0, TypeName))
        return error("Invalid record");
      continue;

    case bitc::TYPE_CODE_STRUCT_NAMED: { // STRUCT: [ispacked, eltty x N]
      if (Record.size() < 1)
        return error("Invalid record");

      if (NumRecords >= TypeList.size())
        return error("Invalid TYPE table");

      // Check to see if this was forward referenced, if so fill in the temp.
      StructType *Res = cast_or_null<StructType>(TypeList[NumRecords]);
      if (Res) {
        Res->setName(TypeName);
        TypeList[NumRecords] = nullptr;
      } else  // Otherwise, create a new struct.
        Res = createIdentifiedStructType(Context, TypeName);
      TypeName.clear();

      SmallVector<Type*, 8> EltTys;
      for (unsigned i = 1, e = Record.size(); i != e; ++i) {
        if (Type *T = getTypeByID(Record[i]))
          EltTys.push_back(T);
        else
          break;
      }
      if (EltTys.size() != Record.size()-1)
        return error("Invalid record");
      Res->setBody(EltTys, Record[0]);
      ResultTy = Res;
      break;
    }
    case bitc::TYPE_CODE_OPAQUE: {       // OPAQUE: []
      if (Record.size() != 1)
        return error("Invalid record");

      if (NumRecords >= TypeList.size())
        return error("Invalid TYPE table");

      // Check to see if this was forward referenced, if so fill in the temp.
      StructType *Res = cast_or_null<StructType>(TypeList[NumRecords]);
      if (Res) {
        Res->setName(TypeName);
        TypeList[NumRecords] = nullptr;
      } else  // Otherwise, create a new struct with no body.
        Res = createIdentifiedStructType(Context, TypeName);
      TypeName.clear();
      ResultTy = Res;
      break;
    }
    case bitc::TYPE_CODE_ARRAY:     // ARRAY: [numelts, eltty]
      if (Record.size() < 2)
        return error("Invalid record");
      ResultTy = getTypeByID(Record[1]);
      if (!ResultTy || !ArrayType::isValidElementType(ResultTy))
        return error("Invalid type");
      ResultTy = ArrayType::get(ResultTy, Record[0]);
      break;
    case bitc::TYPE_CODE_VECTOR:    // VECTOR: [numelts, eltty]
      if (Record.size() < 2)
        return error("Invalid record");
      if (Record[0] == 0)
        return error("Invalid vector length");
      ResultTy = getTypeByID(Record[1]);
      if (!ResultTy || !StructType::isValidElementType(ResultTy))
        return error("Invalid type");
      ResultTy = VectorType::get(ResultTy, Record[0]);
      break;
    }

    if (NumRecords >= TypeList.size())
      return error("Invalid TYPE table");
    if (TypeList[NumRecords])
      return error(
          "Invalid TYPE table: Only named structs can be forward referenced");
    assert(ResultTy && "Didn't read a type?");
    TypeList[NumRecords++] = ResultTy;
  }
}

Error BitcodeReader::parseOperandBundleTags() {
  if (Stream.EnterSubBlock(bitc::OPERAND_BUNDLE_TAGS_BLOCK_ID))
    return error("Invalid record");

  if (!BundleTags.empty())
    return error("Invalid multiple blocks");

  SmallVector<uint64_t, 64> Record;

  while (true) {
    BitstreamEntry Entry = Stream.advanceSkippingSubblocks();

    switch (Entry.Kind) {
    case BitstreamEntry::SubBlock: // Handled for us already.
    case BitstreamEntry::Error:
      return error("Malformed block");
    case BitstreamEntry::EndBlock:
      return Error::success();
    case BitstreamEntry::Record:
      // The interesting case.
      break;
    }

    // Tags are implicitly mapped to integers by their order.

    if (Stream.readRecord(Entry.ID, Record) != bitc::OPERAND_BUNDLE_TAG)
      return error("Invalid record");

    // OPERAND_BUNDLE_TAG: [strchr x N]
    BundleTags.emplace_back();
    if (convertToString(Record, 0, BundleTags.back()))
      return error("Invalid record");
    Record.clear();
  }
}

/// Associate a value with its name from the given index in the provided record.
Expected<Value *> BitcodeReader::recordValue(SmallVectorImpl<uint64_t> &Record,
                                             unsigned NameIndex, Triple &TT) {
  SmallString<128> ValueName;
  if (convertToString(Record, NameIndex, ValueName))
    return error("Invalid record");
  unsigned ValueID = Record[0];
  if (ValueID >= ValueList.size() || !ValueList[ValueID])
    return error("Invalid record");
  Value *V = ValueList[ValueID];

  StringRef NameStr(ValueName.data(), ValueName.size());
  if (NameStr.find_first_of(0) != StringRef::npos)
    return error("Invalid value name");
  V->setName(NameStr);
  auto *GO = dyn_cast<GlobalObject>(V);
  if (GO) {
    if (GO->getComdat() == reinterpret_cast<Comdat *>(1)) {
      if (TT.isOSBinFormatMachO())
        GO->setComdat(nullptr);
      else
        GO->setComdat(TheModule->getOrInsertComdat(V->getName()));
    }
  }
  return V;
}

/// Helper to note and return the current location, and jump to the given
/// offset.
static uint64_t jumpToValueSymbolTable(uint64_t Offset,
                                       BitstreamCursor &Stream) {
  // Save the current parsing location so we can jump back at the end
  // of the VST read.
  uint64_t CurrentBit = Stream.GetCurrentBitNo();
  Stream.JumpToBit(Offset * 32);
#ifndef NDEBUG
  // Do some checking if we are in debug mode.
  BitstreamEntry Entry = Stream.advance();
  assert(Entry.Kind == BitstreamEntry::SubBlock);
  assert(Entry.ID == bitc::VALUE_SYMTAB_BLOCK_ID);
#else
  // In NDEBUG mode ignore the output so we don't get an unused variable
  // warning.
  Stream.advance();
#endif
  return CurrentBit;
}

/// Parse the value symbol table at either the current parsing location or
/// at the given bit offset if provided.
Error BitcodeReader::parseValueSymbolTable(uint64_t Offset) {
  uint64_t CurrentBit;
  // Pass in the Offset to distinguish between calling for the module-level
  // VST (where we want to jump to the VST offset) and the function-level
  // VST (where we don't).
  if (Offset > 0)
    CurrentBit = jumpToValueSymbolTable(Offset, Stream);

  // Compute the delta between the bitcode indices in the VST (the word offset
  // to the word-aligned ENTER_SUBBLOCK for the function block, and that
  // expected by the lazy reader. The reader's EnterSubBlock expects to have
  // already read the ENTER_SUBBLOCK code (size getAbbrevIDWidth) and BlockID
  // (size BlockIDWidth). Note that we access the stream's AbbrevID width here
  // just before entering the VST subblock because: 1) the EnterSubBlock
  // changes the AbbrevID width; 2) the VST block is nested within the same
  // outer MODULE_BLOCK as the FUNCTION_BLOCKs and therefore have the same
  // AbbrevID width before calling EnterSubBlock; and 3) when we want to
  // jump to the FUNCTION_BLOCK using this offset later, we don't want
  // to rely on the stream's AbbrevID width being that of the MODULE_BLOCK.
  unsigned FuncBitcodeOffsetDelta =
      Stream.getAbbrevIDWidth() + bitc::BlockIDWidth;

  if (Stream.EnterSubBlock(bitc::VALUE_SYMTAB_BLOCK_ID))
    return error("Invalid record");

  SmallVector<uint64_t, 64> Record;

  Triple TT(TheModule->getTargetTriple());

  // Read all the records for this value table.
  SmallString<128> ValueName;

  while (true) {
    BitstreamEntry Entry = Stream.advanceSkippingSubblocks();

    switch (Entry.Kind) {
    case BitstreamEntry::SubBlock: // Handled for us already.
    case BitstreamEntry::Error:
      return error("Malformed block");
    case BitstreamEntry::EndBlock:
      if (Offset > 0)
        Stream.JumpToBit(CurrentBit);
      return Error::success();
    case BitstreamEntry::Record:
      // The interesting case.
      break;
    }

    // Read a record.
    Record.clear();
    switch (Stream.readRecord(Entry.ID, Record)) {
    default:  // Default behavior: unknown type.
      break;
    case bitc::VST_CODE_ENTRY: {  // VST_CODE_ENTRY: [valueid, namechar x N]
      Expected<Value *> ValOrErr = recordValue(Record, 1, TT);
      if (Error Err = ValOrErr.takeError())
        return Err;
      ValOrErr.get();
      break;
    }
    case bitc::VST_CODE_FNENTRY: {
      // VST_CODE_FNENTRY: [valueid, offset, namechar x N]
      Expected<Value *> ValOrErr = recordValue(Record, 2, TT);
      if (Error Err = ValOrErr.takeError())
        return Err;
      Value *V = ValOrErr.get();

      auto *GO = dyn_cast<GlobalObject>(V);
      if (!GO) {
        // If this is an alias, need to get the actual Function object
        // it aliases, in order to set up the DeferredFunctionInfo entry below.
        auto *GA = dyn_cast<GlobalAlias>(V);
        if (GA)
          GO = GA->getBaseObject();
        assert(GO);
      }

      // Note that we subtract 1 here because the offset is relative to one word
      // before the start of the identification or module block, which was
      // historically always the start of the regular bitcode header.
      uint64_t FuncWordOffset = Record[1] - 1;
      Function *F = dyn_cast<Function>(GO);
      assert(F);
      uint64_t FuncBitOffset = FuncWordOffset * 32;
      DeferredFunctionInfo[F] = FuncBitOffset + FuncBitcodeOffsetDelta;
      // Set the LastFunctionBlockBit to point to the last function block.
      // Later when parsing is resumed after function materialization,
      // we can simply skip that last function block.
      if (FuncBitOffset > LastFunctionBlockBit)
        LastFunctionBlockBit = FuncBitOffset;
      break;
    }
    case bitc::VST_CODE_BBENTRY: {
      if (convertToString(Record, 1, ValueName))
        return error("Invalid record");
      BasicBlock *BB = getBasicBlock(Record[0]);
      if (!BB)
        return error("Invalid record");

      BB->setName(StringRef(ValueName.data(), ValueName.size()));
      ValueName.clear();
      break;
    }
    }
  }
}

/// Parse a single METADATA_KIND record, inserting result in MDKindMap.
Error BitcodeReader::parseMetadataKindRecord(
    SmallVectorImpl<uint64_t> &Record) {
  if (Record.size() < 2)
    return error("Invalid record");

  unsigned Kind = Record[0];
  SmallString<8> Name(Record.begin() + 1, Record.end());

  unsigned NewKind = TheModule->getMDKindID(Name.str());
  if (!MDKindMap.insert(std::make_pair(Kind, NewKind)).second)
    return error("Conflicting METADATA_KIND records");
  return Error::success();
}

static int64_t unrotateSign(uint64_t U) { return U & 1 ? ~(U >> 1) : U >> 1; }

Error BitcodeReader::parseMetadataStrings(ArrayRef<uint64_t> Record,
                                          StringRef Blob,
                                          unsigned &NextMetadataNo) {
  // All the MDStrings in the block are emitted together in a single
  // record.  The strings are concatenated and stored in a blob along with
  // their sizes.
  if (Record.size() != 2)
    return error("Invalid record: metadata strings layout");

  unsigned NumStrings = Record[0];
  unsigned StringsOffset = Record[1];
  if (!NumStrings)
    return error("Invalid record: metadata strings with no strings");
  if (StringsOffset > Blob.size())
    return error("Invalid record: metadata strings corrupt offset");

  StringRef Lengths = Blob.slice(0, StringsOffset);
  SimpleBitstreamCursor R(Lengths);

  StringRef Strings = Blob.drop_front(StringsOffset);
  do {
    if (R.AtEndOfStream())
      return error("Invalid record: metadata strings bad length");

    unsigned Size = R.ReadVBR(6);
    if (Strings.size() < Size)
      return error("Invalid record: metadata strings truncated chars");

    MetadataList.assignValue(MDString::get(Context, Strings.slice(0, Size)),
                             NextMetadataNo++);
    Strings = Strings.drop_front(Size);
  } while (--NumStrings);

  return Error::success();
}

namespace {

class PlaceholderQueue {
  // Placeholders would thrash around when moved, so store in a std::deque
  // instead of some sort of vector.
  std::deque<DistinctMDOperandPlaceholder> PHs;

public:
  DistinctMDOperandPlaceholder &getPlaceholderOp(unsigned ID);
  void flush(BitcodeReaderMetadataList &MetadataList);
};

} // end anonymous namespace

DistinctMDOperandPlaceholder &PlaceholderQueue::getPlaceholderOp(unsigned ID) {
  PHs.emplace_back(ID);
  return PHs.back();
}

void PlaceholderQueue::flush(BitcodeReaderMetadataList &MetadataList) {
  while (!PHs.empty()) {
    PHs.front().replaceUseWith(
        MetadataList.getMetadataFwdRef(PHs.front().getID()));
    PHs.pop_front();
  }
}

/// Parse a METADATA_BLOCK. If ModuleLevel is true then we are parsing
/// module level metadata.
Error BitcodeReader::parseMetadata(bool ModuleLevel) {
  assert((ModuleLevel || DeferredMetadataInfo.empty()) &&
         "Must read all module-level metadata before function-level");

  IsMetadataMaterialized = true;
  unsigned NextMetadataNo = MetadataList.size();

  if (!ModuleLevel && MetadataList.hasFwdRefs())
    return error("Invalid metadata: fwd refs into function blocks");

  if (Stream.EnterSubBlock(bitc::METADATA_BLOCK_ID))
    return error("Invalid record");

  std::vector<std::pair<DICompileUnit *, Metadata *>> CUSubprograms;
  SmallVector<uint64_t, 64> Record;

  PlaceholderQueue Placeholders;
  bool IsDistinct;
  auto getMD = [&](unsigned ID) -> Metadata * {
    if (!IsDistinct)
      return MetadataList.getMetadataFwdRef(ID);
    if (auto *MD = MetadataList.getMetadataIfResolved(ID))
      return MD;
    return &Placeholders.getPlaceholderOp(ID);
  };
  auto getMDOrNull = [&](unsigned ID) -> Metadata * {
    if (ID)
      return getMD(ID - 1);
    return nullptr;
  };
  auto getMDOrNullWithoutPlaceholders = [&](unsigned ID) -> Metadata * {
    if (ID)
      return MetadataList.getMetadataFwdRef(ID - 1);
    return nullptr;
  };
  auto getMDString = [&](unsigned ID) -> MDString *{
    // This requires that the ID is not really a forward reference.  In
    // particular, the MDString must already have been resolved.
    return cast_or_null<MDString>(getMDOrNull(ID));
  };

  // Support for old type refs.
  auto getDITypeRefOrNull = [&](unsigned ID) {
    return MetadataList.upgradeTypeRef(getMDOrNull(ID));
  };

#define GET_OR_DISTINCT(CLASS, ARGS)                                           \
  (IsDistinct ? CLASS::getDistinct ARGS : CLASS::get ARGS)

  // Read all the records.
  while (true) {
    BitstreamEntry Entry = Stream.advanceSkippingSubblocks();

    switch (Entry.Kind) {
    case BitstreamEntry::SubBlock: // Handled for us already.
    case BitstreamEntry::Error:
      return error("Malformed block");
    case BitstreamEntry::EndBlock:
      // Upgrade old-style CU <-> SP pointers to point from SP to CU.
      for (auto CU_SP : CUSubprograms)
        if (auto *SPs = dyn_cast_or_null<MDTuple>(CU_SP.second))
          for (auto &Op : SPs->operands())
            if (auto *SP = dyn_cast_or_null<MDNode>(Op))
              SP->replaceOperandWith(7, CU_SP.first);

      MetadataList.tryToResolveCycles();
      Placeholders.flush(MetadataList);
      return Error::success();
    case BitstreamEntry::Record:
      // The interesting case.
      break;
    }

    // Read a record.
    Record.clear();
    StringRef Blob;
    unsigned Code = Stream.readRecord(Entry.ID, Record, &Blob);
    IsDistinct = false;
    switch (Code) {
    default:  // Default behavior: ignore.
      break;
    case bitc::METADATA_NAME: {
      // Read name of the named metadata.
      SmallString<8> Name(Record.begin(), Record.end());
      Record.clear();
      Code = Stream.ReadCode();

      unsigned NextBitCode = Stream.readRecord(Code, Record);
      if (NextBitCode != bitc::METADATA_NAMED_NODE)
        return error("METADATA_NAME not followed by METADATA_NAMED_NODE");

      // Read named metadata elements.
      unsigned Size = Record.size();
      NamedMDNode *NMD = TheModule->getOrInsertNamedMetadata(Name);
      for (unsigned i = 0; i != Size; ++i) {
        MDNode *MD = MetadataList.getMDNodeFwdRefOrNull(Record[i]);
        if (!MD)
          return error("Invalid record");
        NMD->addOperand(MD);
      }
      break;
    }
    case bitc::METADATA_OLD_FN_NODE: {
      // FIXME: Remove in 4.0.
      // This is a LocalAsMetadata record, the only type of function-local
      // metadata.
      if (Record.size() % 2 == 1)
        return error("Invalid record");

      // If this isn't a LocalAsMetadata record, we're dropping it.  This used
      // to be legal, but there's no upgrade path.
      auto dropRecord = [&] {
        MetadataList.assignValue(MDNode::get(Context, None), NextMetadataNo++);
      };
      if (Record.size() != 2) {
        dropRecord();
        break;
      }

      Type *Ty = getTypeByID(Record[0]);
      if (Ty->isMetadataTy() || Ty->isVoidTy()) {
        dropRecord();
        break;
      }

      MetadataList.assignValue(
          LocalAsMetadata::get(ValueList.getValueFwdRef(Record[1], Ty)),
          NextMetadataNo++);
      break;
    }
    case bitc::METADATA_OLD_NODE: {
      // FIXME: Remove in 4.0.
      if (Record.size() % 2 == 1)
        return error("Invalid record");

      unsigned Size = Record.size();
      SmallVector<Metadata *, 8> Elts;
      for (unsigned i = 0; i != Size; i += 2) {
        Type *Ty = getTypeByID(Record[i]);
        if (!Ty)
          return error("Invalid record");
        if (Ty->isMetadataTy())
          Elts.push_back(getMD(Record[i + 1]));
        else if (!Ty->isVoidTy()) {
          auto *MD =
              ValueAsMetadata::get(ValueList.getValueFwdRef(Record[i + 1], Ty));
          assert(isa<ConstantAsMetadata>(MD) &&
                 "Expected non-function-local metadata");
          Elts.push_back(MD);
        } else
          Elts.push_back(nullptr);
      }
      MetadataList.assignValue(MDNode::get(Context, Elts), NextMetadataNo++);
      break;
    }
    case bitc::METADATA_VALUE: {
      if (Record.size() != 2)
        return error("Invalid record");

      Type *Ty = getTypeByID(Record[0]);
      if (Ty->isMetadataTy() || Ty->isVoidTy())
        return error("Invalid record");

      MetadataList.assignValue(
          ValueAsMetadata::get(ValueList.getValueFwdRef(Record[1], Ty)),
          NextMetadataNo++);
      break;
    }
    case bitc::METADATA_DISTINCT_NODE:
      IsDistinct = true;
      LLVM_FALLTHROUGH;
    case bitc::METADATA_NODE: {
      SmallVector<Metadata *, 8> Elts;
      Elts.reserve(Record.size());
      for (unsigned ID : Record)
        Elts.push_back(getMDOrNull(ID));
      MetadataList.assignValue(IsDistinct ? MDNode::getDistinct(Context, Elts)
                                          : MDNode::get(Context, Elts),
                               NextMetadataNo++);
      break;
    }
    case bitc::METADATA_LOCATION: {
      if (Record.size() != 5)
        return error("Invalid record");

      IsDistinct = Record[0];
      unsigned Line = Record[1];
      unsigned Column = Record[2];
      Metadata *Scope = getMD(Record[3]);
      Metadata *InlinedAt = getMDOrNull(Record[4]);
      MetadataList.assignValue(
          GET_OR_DISTINCT(DILocation,
                          (Context, Line, Column, Scope, InlinedAt)),
          NextMetadataNo++);
      break;
    }
    case bitc::METADATA_GENERIC_DEBUG: {
      if (Record.size() < 4)
        return error("Invalid record");

      IsDistinct = Record[0];
      unsigned Tag = Record[1];
      unsigned Version = Record[2];

      if (Tag >= 1u << 16 || Version != 0)
        return error("Invalid record");

      auto *Header = getMDString(Record[3]);
      SmallVector<Metadata *, 8> DwarfOps;
      for (unsigned I = 4, E = Record.size(); I != E; ++I)
        DwarfOps.push_back(getMDOrNull(Record[I]));
      MetadataList.assignValue(
          GET_OR_DISTINCT(GenericDINode, (Context, Tag, Header, DwarfOps)),
          NextMetadataNo++);
      break;
    }
    case bitc::METADATA_SUBRANGE: {
      if (Record.size() != 3)
        return error("Invalid record");

      IsDistinct = Record[0];
      MetadataList.assignValue(
          GET_OR_DISTINCT(DISubrange,
                          (Context, Record[1], unrotateSign(Record[2]))),
          NextMetadataNo++);
      break;
    }
    case bitc::METADATA_ENUMERATOR: {
      if (Record.size() != 3)
        return error("Invalid record");

      IsDistinct = Record[0];
      MetadataList.assignValue(
          GET_OR_DISTINCT(DIEnumerator, (Context, unrotateSign(Record[1]),
                                         getMDString(Record[2]))),
          NextMetadataNo++);
      break;
    }
    case bitc::METADATA_BASIC_TYPE: {
      if (Record.size() != 6)
        return error("Invalid record");

      IsDistinct = Record[0];
      MetadataList.assignValue(
          GET_OR_DISTINCT(DIBasicType,
                          (Context, Record[1], getMDString(Record[2]),
                           Record[3], Record[4], Record[5])),
          NextMetadataNo++);
      break;
    }
    case bitc::METADATA_DERIVED_TYPE: {
      if (Record.size() != 12)
        return error("Invalid record");

      IsDistinct = Record[0];
      DINode::DIFlags Flags = static_cast<DINode::DIFlags>(Record[10]);
      MetadataList.assignValue(
          GET_OR_DISTINCT(DIDerivedType,
                          (Context, Record[1], getMDString(Record[2]),
                           getMDOrNull(Record[3]), Record[4],
                           getDITypeRefOrNull(Record[5]),
                           getDITypeRefOrNull(Record[6]), Record[7], Record[8],
                           Record[9], Flags, getDITypeRefOrNull(Record[11]))),
          NextMetadataNo++);
      break;
    }
    case bitc::METADATA_COMPOSITE_TYPE: {
      if (Record.size() != 16)
        return error("Invalid record");

      // If we have a UUID and this is not a forward declaration, lookup the
      // mapping.
      IsDistinct = Record[0] & 0x1;
      bool IsNotUsedInTypeRef = Record[0] >= 2;
      unsigned Tag = Record[1];
      MDString *Name = getMDString(Record[2]);
      Metadata *File = getMDOrNull(Record[3]);
      unsigned Line = Record[4];
      Metadata *Scope = getDITypeRefOrNull(Record[5]);
      Metadata *BaseType = getDITypeRefOrNull(Record[6]);
      uint64_t SizeInBits = Record[7];
      if (Record[8] > (uint64_t)std::numeric_limits<uint32_t>::max())
        return error("Alignment value is too large");
      uint32_t AlignInBits = Record[8];
      uint64_t OffsetInBits = Record[9];
      DINode::DIFlags Flags = static_cast<DINode::DIFlags>(Record[10]);
      Metadata *Elements = getMDOrNull(Record[11]);
      unsigned RuntimeLang = Record[12];
      Metadata *VTableHolder = getDITypeRefOrNull(Record[13]);
      Metadata *TemplateParams = getMDOrNull(Record[14]);
      auto *Identifier = getMDString(Record[15]);
      DICompositeType *CT = nullptr;
      if (Identifier)
        CT = DICompositeType::buildODRType(
            Context, *Identifier, Tag, Name, File, Line, Scope, BaseType,
            SizeInBits, AlignInBits, OffsetInBits, Flags, Elements, RuntimeLang,
            VTableHolder, TemplateParams);

      // Create a node if we didn't get a lazy ODR type.
      if (!CT)
        CT = GET_OR_DISTINCT(DICompositeType,
                             (Context, Tag, Name, File, Line, Scope, BaseType,
                              SizeInBits, AlignInBits, OffsetInBits, Flags,
                              Elements, RuntimeLang, VTableHolder,
                              TemplateParams, Identifier));
      if (!IsNotUsedInTypeRef && Identifier)
        MetadataList.addTypeRef(*Identifier, *cast<DICompositeType>(CT));

      MetadataList.assignValue(CT, NextMetadataNo++);
      break;
    }
    case bitc::METADATA_SUBROUTINE_TYPE: {
      if (Record.size() < 3 || Record.size() > 4)
        return error("Invalid record");
      bool IsOldTypeRefArray = Record[0] < 2;
      unsigned CC = (Record.size() > 3) ? Record[3] : 0;

      IsDistinct = Record[0] & 0x1;
      DINode::DIFlags Flags = static_cast<DINode::DIFlags>(Record[1]);
      Metadata *Types = getMDOrNull(Record[2]);
      if (LLVM_UNLIKELY(IsOldTypeRefArray))
        Types = MetadataList.upgradeTypeRefArray(Types);

      MetadataList.assignValue(
          GET_OR_DISTINCT(DISubroutineType, (Context, Flags, CC, Types)),
          NextMetadataNo++);
      break;
    }

    case bitc::METADATA_MODULE: {
      if (Record.size() != 6)
        return error("Invalid record");

      IsDistinct = Record[0];
      MetadataList.assignValue(
          GET_OR_DISTINCT(DIModule,
                          (Context, getMDOrNull(Record[1]),
                           getMDString(Record[2]), getMDString(Record[3]),
                           getMDString(Record[4]), getMDString(Record[5]))),
          NextMetadataNo++);
      break;
    }

    case bitc::METADATA_FILE: {
      if (Record.size() != 3)
        return error("Invalid record");

      IsDistinct = Record[0];
      MetadataList.assignValue(
          GET_OR_DISTINCT(DIFile, (Context, getMDString(Record[1]),
                                   getMDString(Record[2]))),
          NextMetadataNo++);
      break;
    }
    case bitc::METADATA_COMPILE_UNIT: {
      if (Record.size() < 14 || Record.size() > 17)
        return error("Invalid record");

      // Ignore Record[0], which indicates whether this compile unit is
      // distinct.  It's always distinct.
      IsDistinct = true;
      auto *CU = DICompileUnit::getDistinct(
          Context, Record[1], getMDOrNull(Record[2]), getMDString(Record[3]),
          Record[4], getMDString(Record[5]), Record[6], getMDString(Record[7]),
          Record[8], getMDOrNull(Record[9]), getMDOrNull(Record[10]),
          getMDOrNull(Record[12]), getMDOrNull(Record[13]),
          Record.size() <= 15 ? nullptr : getMDOrNull(Record[15]),
          Record.size() <= 14 ? 0 : Record[14],
          Record.size() <= 16 ? true : Record[16]);

      MetadataList.assignValue(CU, NextMetadataNo++);

      // Move the Upgrade the list of subprograms.
      if (Metadata *SPs = getMDOrNullWithoutPlaceholders(Record[11]))
        CUSubprograms.push_back({CU, SPs});
      break;
    }
    case bitc::METADATA_SUBPROGRAM: {
      if (Record.size() < 18 || Record.size() > 20)
        return error("Invalid record");

      IsDistinct =
          (Record[0] & 1) || Record[8]; // All definitions should be distinct.
      // Version 1 has a Function as Record[15].
      // Version 2 has removed Record[15].
      // Version 3 has the Unit as Record[15].
      // Version 4 added thisAdjustment.
      bool HasUnit = Record[0] >= 2;
      if (HasUnit && Record.size() < 19)
        return error("Invalid record");
      Metadata *CUorFn = getMDOrNull(Record[15]);
      unsigned Offset = Record.size() >= 19 ? 1 : 0;
      bool HasFn = Offset && !HasUnit;
      bool HasThisAdj = Record.size() >= 20;
      DISubprogram *SP = GET_OR_DISTINCT(
          DISubprogram, (Context,
                         getDITypeRefOrNull(Record[1]),  // scope
                         getMDString(Record[2]),         // name
                         getMDString(Record[3]),         // linkageName
                         getMDOrNull(Record[4]),         // file
                         Record[5],                      // line
                         getMDOrNull(Record[6]),         // type
                         Record[7],                      // isLocal
                         Record[8],                      // isDefinition
                         Record[9],                      // scopeLine
                         getDITypeRefOrNull(Record[10]), // containingType
                         Record[11],                     // virtuality
                         Record[12],                     // virtualIndex
                         HasThisAdj ? Record[19] : 0,    // thisAdjustment
                         static_cast<DINode::DIFlags>(Record[13] // flags
                                                      ),
                         Record[14],                       // isOptimized
                         HasUnit ? CUorFn : nullptr,       // unit
                         getMDOrNull(Record[15 + Offset]), // templateParams
                         getMDOrNull(Record[16 + Offset]), // declaration
                         getMDOrNull(Record[17 + Offset])  // variables
                         ));
      MetadataList.assignValue(SP, NextMetadataNo++);

      // Upgrade sp->function mapping to function->sp mapping.
      if (HasFn) {
        if (auto *CMD = dyn_cast_or_null<ConstantAsMetadata>(CUorFn))
          if (auto *F = dyn_cast<Function>(CMD->getValue())) {
            if (F->isMaterializable())
              // Defer until materialized; unmaterialized functions may not have
              // metadata.
              FunctionsWithSPs[F] = SP;
            else if (!F->empty())
              F->setSubprogram(SP);
          }
      }
      break;
    }
    case bitc::METADATA_LEXICAL_BLOCK: {
      if (Record.size() != 5)
        return error("Invalid record");

      IsDistinct = Record[0];
      MetadataList.assignValue(
          GET_OR_DISTINCT(DILexicalBlock,
                          (Context, getMDOrNull(Record[1]),
                           getMDOrNull(Record[2]), Record[3], Record[4])),
          NextMetadataNo++);
      break;
    }
    case bitc::METADATA_LEXICAL_BLOCK_FILE: {
      if (Record.size() != 4)
        return error("Invalid record");

      IsDistinct = Record[0];
      MetadataList.assignValue(
          GET_OR_DISTINCT(DILexicalBlockFile,
                          (Context, getMDOrNull(Record[1]),
                           getMDOrNull(Record[2]), Record[3])),
          NextMetadataNo++);
      break;
    }
    case bitc::METADATA_NAMESPACE: {
      if (Record.size() != 5)
        return error("Invalid record");

      IsDistinct = Record[0] & 1;
      bool ExportSymbols = Record[0] & 2;
      MetadataList.assignValue(
          GET_OR_DISTINCT(DINamespace,
                          (Context, getMDOrNull(Record[1]),
                           getMDOrNull(Record[2]), getMDString(Record[3]),
                           Record[4], ExportSymbols)),
          NextMetadataNo++);
      break;
    }
    case bitc::METADATA_MACRO: {
      if (Record.size() != 5)
        return error("Invalid record");

      IsDistinct = Record[0];
      MetadataList.assignValue(
          GET_OR_DISTINCT(DIMacro,
                          (Context, Record[1], Record[2],
                           getMDString(Record[3]), getMDString(Record[4]))),
          NextMetadataNo++);
      break;
    }
    case bitc::METADATA_MACRO_FILE: {
      if (Record.size() != 5)
        return error("Invalid record");

      IsDistinct = Record[0];
      MetadataList.assignValue(
          GET_OR_DISTINCT(DIMacroFile,
                          (Context, Record[1], Record[2],
                           getMDOrNull(Record[3]), getMDOrNull(Record[4]))),
          NextMetadataNo++);
      break;
    }
    case bitc::METADATA_TEMPLATE_TYPE: {
      if (Record.size() != 3)
        return error("Invalid record");

      IsDistinct = Record[0];
      MetadataList.assignValue(GET_OR_DISTINCT(DITemplateTypeParameter,
                                               (Context, getMDString(Record[1]),
                                                getDITypeRefOrNull(Record[2]))),
                               NextMetadataNo++);
      break;
    }
    case bitc::METADATA_TEMPLATE_VALUE: {
      if (Record.size() != 5)
        return error("Invalid record");

      IsDistinct = Record[0];
      MetadataList.assignValue(
          GET_OR_DISTINCT(DITemplateValueParameter,
                          (Context, Record[1], getMDString(Record[2]),
                           getDITypeRefOrNull(Record[3]),
                           getMDOrNull(Record[4]))),
          NextMetadataNo++);
      break;
    }
    case bitc::METADATA_GLOBAL_VAR: {
      if (Record.size() < 11 || Record.size() > 12)
        return error("Invalid record");

      IsDistinct = Record[0];

      // Upgrade old metadata, which stored a global variable reference or a
      // ConstantInt here.
      Metadata *Expr = getMDOrNull(Record[9]);
      uint32_t AlignInBits = 0;
      if (Record.size() > 11) {
        if (Record[11] > (uint64_t)std::numeric_limits<uint32_t>::max())
          return error("Alignment value is too large");
        AlignInBits = Record[11];
      }
      GlobalVariable *Attach = nullptr;
      if (auto *CMD = dyn_cast_or_null<ConstantAsMetadata>(Expr)) {
        if (auto *GV = dyn_cast<GlobalVariable>(CMD->getValue())) {
          Attach = GV;
          Expr = nullptr;
        } else if (auto *CI = dyn_cast<ConstantInt>(CMD->getValue())) {
          Expr = DIExpression::get(Context,
                                   {dwarf::DW_OP_constu, CI->getZExtValue(),
                                    dwarf::DW_OP_stack_value});
        } else {
          Expr = nullptr;
        }
      }

      DIGlobalVariable *DGV = GET_OR_DISTINCT(
          DIGlobalVariable,
          (Context, getMDOrNull(Record[1]), getMDString(Record[2]),
           getMDString(Record[3]), getMDOrNull(Record[4]), Record[5],
           getDITypeRefOrNull(Record[6]), Record[7], Record[8], Expr,
           getMDOrNull(Record[10]), AlignInBits));
      MetadataList.assignValue(DGV, NextMetadataNo++);

      if (Attach)
        Attach->addDebugInfo(DGV);

      break;
    }
    case bitc::METADATA_LOCAL_VAR: {
      // 10th field is for the obseleted 'inlinedAt:' field.
      if (Record.size() < 8 || Record.size() > 10)
        return error("Invalid record");

      IsDistinct = Record[0] & 1;
      bool HasAlignment = Record[0] & 2;
      // 2nd field used to be an artificial tag, either DW_TAG_auto_variable or
      // DW_TAG_arg_variable, if we have alignment flag encoded it means, that
      // this is newer version of record which doesn't have artifical tag.
      bool HasTag = !HasAlignment && Record.size() > 8;
      DINode::DIFlags Flags = static_cast<DINode::DIFlags>(Record[7 + HasTag]);
      uint32_t AlignInBits = 0;
      if (HasAlignment) {
        if (Record[8 + HasTag] >
            (uint64_t)std::numeric_limits<uint32_t>::max())
          return error("Alignment value is too large");
        AlignInBits = Record[8 + HasTag];
      }
      MetadataList.assignValue(
          GET_OR_DISTINCT(DILocalVariable,
                          (Context, getMDOrNull(Record[1 + HasTag]),
                           getMDString(Record[2 + HasTag]),
                           getMDOrNull(Record[3 + HasTag]), Record[4 + HasTag],
                           getDITypeRefOrNull(Record[5 + HasTag]),
                           Record[6 + HasTag], Flags, AlignInBits)),
          NextMetadataNo++);
      break;
    }
    case bitc::METADATA_EXPRESSION: {
      if (Record.size() < 1)
        return error("Invalid record");

      IsDistinct = Record[0];
      MetadataList.assignValue(
          GET_OR_DISTINCT(DIExpression,
                          (Context, makeArrayRef(Record).slice(1))),
          NextMetadataNo++);
      break;
    }
    case bitc::METADATA_OBJC_PROPERTY: {
      if (Record.size() != 8)
        return error("Invalid record");

      IsDistinct = Record[0];
      MetadataList.assignValue(
          GET_OR_DISTINCT(DIObjCProperty,
                          (Context, getMDString(Record[1]),
                           getMDOrNull(Record[2]), Record[3],
                           getMDString(Record[4]), getMDString(Record[5]),
                           Record[6], getDITypeRefOrNull(Record[7]))),
          NextMetadataNo++);
      break;
    }
    case bitc::METADATA_IMPORTED_ENTITY: {
      if (Record.size() != 6)
        return error("Invalid record");

      IsDistinct = Record[0];
      MetadataList.assignValue(
          GET_OR_DISTINCT(DIImportedEntity,
                          (Context, Record[1], getMDOrNull(Record[2]),
                           getDITypeRefOrNull(Record[3]), Record[4],
                           getMDString(Record[5]))),
          NextMetadataNo++);
      break;
    }
    case bitc::METADATA_STRING_OLD: {
      std::string String(Record.begin(), Record.end());

      // Test for upgrading !llvm.loop.
      HasSeenOldLoopTags |= mayBeOldLoopAttachmentTag(String);

      Metadata *MD = MDString::get(Context, String);
      MetadataList.assignValue(MD, NextMetadataNo++);
      break;
    }
    case bitc::METADATA_STRINGS:
      if (Error Err = parseMetadataStrings(Record, Blob, NextMetadataNo))
        return Err;
      break;
    case bitc::METADATA_GLOBAL_DECL_ATTACHMENT: {
      if (Record.size() % 2 == 0)
        return error("Invalid record");
      unsigned ValueID = Record[0];
      if (ValueID >= ValueList.size())
        return error("Invalid record");
      if (auto *GO = dyn_cast<GlobalObject>(ValueList[ValueID]))
        if (Error Err = parseGlobalObjectAttachment(
                *GO, ArrayRef<uint64_t>(Record).slice(1)))
          return Err;
      break;
    }
    case bitc::METADATA_KIND: {
      // Support older bitcode files that had METADATA_KIND records in a
      // block with METADATA_BLOCK_ID.
      if (Error Err = parseMetadataKindRecord(Record))
        return Err;
      break;
    }
    }
  }

#undef GET_OR_DISTINCT
}

/// Parse the metadata kinds out of the METADATA_KIND_BLOCK.
Error BitcodeReader::parseMetadataKinds() {
  if (Stream.EnterSubBlock(bitc::METADATA_KIND_BLOCK_ID))
    return error("Invalid record");

  SmallVector<uint64_t, 64> Record;

  // Read all the records.
  while (true) {
    BitstreamEntry Entry = Stream.advanceSkippingSubblocks();

    switch (Entry.Kind) {
    case BitstreamEntry::SubBlock: // Handled for us already.
    case BitstreamEntry::Error:
      return error("Malformed block");
    case BitstreamEntry::EndBlock:
      return Error::success();
    case BitstreamEntry::Record:
      // The interesting case.
      break;
    }

    // Read a record.
    Record.clear();
    unsigned Code = Stream.readRecord(Entry.ID, Record);
    switch (Code) {
    default: // Default behavior: ignore.
      break;
    case bitc::METADATA_KIND: {
      if (Error Err = parseMetadataKindRecord(Record))
        return Err;
      break;
    }
    }
  }
}

/// Decode a signed value stored with the sign bit in the LSB for dense VBR
/// encoding.
uint64_t BitcodeReader::decodeSignRotatedValue(uint64_t V) {
  if ((V & 1) == 0)
    return V >> 1;
  if (V != 1)
    return -(V >> 1);
  // There is no such thing as -0 with integers.  "-0" really means MININT.
  return 1ULL << 63;
}

/// Resolve all of the initializers for global values and aliases that we can.
Error BitcodeReader::resolveGlobalAndIndirectSymbolInits() {
  std::vector<std::pair<GlobalVariable*, unsigned> > GlobalInitWorklist;
  std::vector<std::pair<GlobalIndirectSymbol*, unsigned> >
      IndirectSymbolInitWorklist;
  std::vector<std::pair<Function*, unsigned> > FunctionPrefixWorklist;
  std::vector<std::pair<Function*, unsigned> > FunctionPrologueWorklist;
  std::vector<std::pair<Function*, unsigned> > FunctionPersonalityFnWorklist;

  GlobalInitWorklist.swap(GlobalInits);
  IndirectSymbolInitWorklist.swap(IndirectSymbolInits);
  FunctionPrefixWorklist.swap(FunctionPrefixes);
  FunctionPrologueWorklist.swap(FunctionPrologues);
  FunctionPersonalityFnWorklist.swap(FunctionPersonalityFns);

  while (!GlobalInitWorklist.empty()) {
    unsigned ValID = GlobalInitWorklist.back().second;
    if (ValID >= ValueList.size()) {
      // Not ready to resolve this yet, it requires something later in the file.
      GlobalInits.push_back(GlobalInitWorklist.back());
    } else {
      if (Constant *C = dyn_cast_or_null<Constant>(ValueList[ValID]))
        GlobalInitWorklist.back().first->setInitializer(C);
      else
        return error("Expected a constant");
    }
    GlobalInitWorklist.pop_back();
  }

  while (!IndirectSymbolInitWorklist.empty()) {
    unsigned ValID = IndirectSymbolInitWorklist.back().second;
    if (ValID >= ValueList.size()) {
      IndirectSymbolInits.push_back(IndirectSymbolInitWorklist.back());
    } else {
      Constant *C = dyn_cast_or_null<Constant>(ValueList[ValID]);
      if (!C)
        return error("Expected a constant");
      GlobalIndirectSymbol *GIS = IndirectSymbolInitWorklist.back().first;
      if (isa<GlobalAlias>(GIS) && C->getType() != GIS->getType())
        return error("Alias and aliasee types don't match");
      GIS->setIndirectSymbol(C);
    }
    IndirectSymbolInitWorklist.pop_back();
  }

  while (!FunctionPrefixWorklist.empty()) {
    unsigned ValID = FunctionPrefixWorklist.back().second;
    if (ValID >= ValueList.size()) {
      FunctionPrefixes.push_back(FunctionPrefixWorklist.back());
    } else {
      if (Constant *C = dyn_cast_or_null<Constant>(ValueList[ValID]))
        FunctionPrefixWorklist.back().first->setPrefixData(C);
      else
        return error("Expected a constant");
    }
    FunctionPrefixWorklist.pop_back();
  }

  while (!FunctionPrologueWorklist.empty()) {
    unsigned ValID = FunctionPrologueWorklist.back().second;
    if (ValID >= ValueList.size()) {
      FunctionPrologues.push_back(FunctionPrologueWorklist.back());
    } else {
      if (Constant *C = dyn_cast_or_null<Constant>(ValueList[ValID]))
        FunctionPrologueWorklist.back().first->setPrologueData(C);
      else
        return error("Expected a constant");
    }
    FunctionPrologueWorklist.pop_back();
  }

  while (!FunctionPersonalityFnWorklist.empty()) {
    unsigned ValID = FunctionPersonalityFnWorklist.back().second;
    if (ValID >= ValueList.size()) {
      FunctionPersonalityFns.push_back(FunctionPersonalityFnWorklist.back());
    } else {
      if (Constant *C = dyn_cast_or_null<Constant>(ValueList[ValID]))
        FunctionPersonalityFnWorklist.back().first->setPersonalityFn(C);
      else
        return error("Expected a constant");
    }
    FunctionPersonalityFnWorklist.pop_back();
  }

  return Error::success();
}

static APInt readWideAPInt(ArrayRef<uint64_t> Vals, unsigned TypeBits) {
  SmallVector<uint64_t, 8> Words(Vals.size());
  transform(Vals, Words.begin(),
                 BitcodeReader::decodeSignRotatedValue);

  return APInt(TypeBits, Words);
}

Error BitcodeReader::parseConstants() {
  if (Stream.EnterSubBlock(bitc::CONSTANTS_BLOCK_ID))
    return error("Invalid record");

  SmallVector<uint64_t, 64> Record;

  // Read all the records for this value table.
  Type *CurTy = Type::getInt32Ty(Context);
  unsigned NextCstNo = ValueList.size();

  while (true) {
    BitstreamEntry Entry = Stream.advanceSkippingSubblocks();

    switch (Entry.Kind) {
    case BitstreamEntry::SubBlock: // Handled for us already.
    case BitstreamEntry::Error:
      return error("Malformed block");
    case BitstreamEntry::EndBlock:
      if (NextCstNo != ValueList.size())
        return error("Invalid constant reference");

      // Once all the constants have been read, go through and resolve forward
      // references.
      ValueList.resolveConstantForwardRefs();
      return Error::success();
    case BitstreamEntry::Record:
      // The interesting case.
      break;
    }

    // Read a record.
    Record.clear();
    Type *VoidType = Type::getVoidTy(Context);
    Value *V = nullptr;
    unsigned BitCode = Stream.readRecord(Entry.ID, Record);
    switch (BitCode) {
    default:  // Default behavior: unknown constant
    case bitc::CST_CODE_UNDEF:     // UNDEF
      V = UndefValue::get(CurTy);
      break;
    case bitc::CST_CODE_SETTYPE:   // SETTYPE: [typeid]
      if (Record.empty())
        return error("Invalid record");
      if (Record[0] >= TypeList.size() || !TypeList[Record[0]])
        return error("Invalid record");
      if (TypeList[Record[0]] == VoidType)
        return error("Invalid constant type");
      CurTy = TypeList[Record[0]];
      continue;  // Skip the ValueList manipulation.
    case bitc::CST_CODE_NULL:      // NULL
      V = Constant::getNullValue(CurTy);
      break;
    case bitc::CST_CODE_INTEGER:   // INTEGER: [intval]
      if (!CurTy->isIntegerTy() || Record.empty())
        return error("Invalid record");
      V = ConstantInt::get(CurTy, decodeSignRotatedValue(Record[0]));
      break;
    case bitc::CST_CODE_WIDE_INTEGER: {// WIDE_INTEGER: [n x intval]
      if (!CurTy->isIntegerTy() || Record.empty())
        return error("Invalid record");

      APInt VInt =
          readWideAPInt(Record, cast<IntegerType>(CurTy)->getBitWidth());
      V = ConstantInt::get(Context, VInt);

      break;
    }
    case bitc::CST_CODE_FLOAT: {    // FLOAT: [fpval]
      if (Record.empty())
        return error("Invalid record");
      if (CurTy->isHalfTy())
        V = ConstantFP::get(Context, APFloat(APFloat::IEEEhalf,
                                             APInt(16, (uint16_t)Record[0])));
      else if (CurTy->isFloatTy())
        V = ConstantFP::get(Context, APFloat(APFloat::IEEEsingle,
                                             APInt(32, (uint32_t)Record[0])));
      else if (CurTy->isDoubleTy())
        V = ConstantFP::get(Context, APFloat(APFloat::IEEEdouble,
                                             APInt(64, Record[0])));
      else if (CurTy->isX86_FP80Ty()) {
        // Bits are not stored the same way as a normal i80 APInt, compensate.
        uint64_t Rearrange[2];
        Rearrange[0] = (Record[1] & 0xffffLL) | (Record[0] << 16);
        Rearrange[1] = Record[0] >> 48;
        V = ConstantFP::get(Context, APFloat(APFloat::x87DoubleExtended,
                                             APInt(80, Rearrange)));
      } else if (CurTy->isFP128Ty())
        V = ConstantFP::get(Context, APFloat(APFloat::IEEEquad,
                                             APInt(128, Record)));
      else if (CurTy->isPPC_FP128Ty())
        V = ConstantFP::get(Context, APFloat(APFloat::PPCDoubleDouble,
                                             APInt(128, Record)));
      else
        V = UndefValue::get(CurTy);
      break;
    }

    case bitc::CST_CODE_AGGREGATE: {// AGGREGATE: [n x value number]
      if (Record.empty())
        return error("Invalid record");

      unsigned Size = Record.size();
      SmallVector<Constant*, 16> Elts;

      if (StructType *STy = dyn_cast<StructType>(CurTy)) {
        for (unsigned i = 0; i != Size; ++i)
          Elts.push_back(ValueList.getConstantFwdRef(Record[i],
                                                     STy->getElementType(i)));
        V = ConstantStruct::get(STy, Elts);
      } else if (ArrayType *ATy = dyn_cast<ArrayType>(CurTy)) {
        Type *EltTy = ATy->getElementType();
        for (unsigned i = 0; i != Size; ++i)
          Elts.push_back(ValueList.getConstantFwdRef(Record[i], EltTy));
        V = ConstantArray::get(ATy, Elts);
      } else if (VectorType *VTy = dyn_cast<VectorType>(CurTy)) {
        Type *EltTy = VTy->getElementType();
        for (unsigned i = 0; i != Size; ++i)
          Elts.push_back(ValueList.getConstantFwdRef(Record[i], EltTy));
        V = ConstantVector::get(Elts);
      } else {
        V = UndefValue::get(CurTy);
      }
      break;
    }
    case bitc::CST_CODE_STRING:    // STRING: [values]
    case bitc::CST_CODE_CSTRING: { // CSTRING: [values]
      if (Record.empty())
        return error("Invalid record");

      SmallString<16> Elts(Record.begin(), Record.end());
      V = ConstantDataArray::getString(Context, Elts,
                                       BitCode == bitc::CST_CODE_CSTRING);
      break;
    }
    case bitc::CST_CODE_DATA: {// DATA: [n x value]
      if (Record.empty())
        return error("Invalid record");

      Type *EltTy = cast<SequentialType>(CurTy)->getElementType();
      if (EltTy->isIntegerTy(8)) {
        SmallVector<uint8_t, 16> Elts(Record.begin(), Record.end());
        if (isa<VectorType>(CurTy))
          V = ConstantDataVector::get(Context, Elts);
        else
          V = ConstantDataArray::get(Context, Elts);
      } else if (EltTy->isIntegerTy(16)) {
        SmallVector<uint16_t, 16> Elts(Record.begin(), Record.end());
        if (isa<VectorType>(CurTy))
          V = ConstantDataVector::get(Context, Elts);
        else
          V = ConstantDataArray::get(Context, Elts);
      } else if (EltTy->isIntegerTy(32)) {
        SmallVector<uint32_t, 16> Elts(Record.begin(), Record.end());
        if (isa<VectorType>(CurTy))
          V = ConstantDataVector::get(Context, Elts);
        else
          V = ConstantDataArray::get(Context, Elts);
      } else if (EltTy->isIntegerTy(64)) {
        SmallVector<uint64_t, 16> Elts(Record.begin(), Record.end());
        if (isa<VectorType>(CurTy))
          V = ConstantDataVector::get(Context, Elts);
        else
          V = ConstantDataArray::get(Context, Elts);
      } else if (EltTy->isHalfTy()) {
        SmallVector<uint16_t, 16> Elts(Record.begin(), Record.end());
        if (isa<VectorType>(CurTy))
          V = ConstantDataVector::getFP(Context, Elts);
        else
          V = ConstantDataArray::getFP(Context, Elts);
      } else if (EltTy->isFloatTy()) {
        SmallVector<uint32_t, 16> Elts(Record.begin(), Record.end());
        if (isa<VectorType>(CurTy))
          V = ConstantDataVector::getFP(Context, Elts);
        else
          V = ConstantDataArray::getFP(Context, Elts);
      } else if (EltTy->isDoubleTy()) {
        SmallVector<uint64_t, 16> Elts(Record.begin(), Record.end());
        if (isa<VectorType>(CurTy))
          V = ConstantDataVector::getFP(Context, Elts);
        else
          V = ConstantDataArray::getFP(Context, Elts);
      } else {
        return error("Invalid type for value");
      }
      break;
    }
    case bitc::CST_CODE_CE_BINOP: {  // CE_BINOP: [opcode, opval, opval]
      if (Record.size() < 3)
        return error("Invalid record");
      int Opc = getDecodedBinaryOpcode(Record[0], CurTy);
      if (Opc < 0) {
        V = UndefValue::get(CurTy);  // Unknown binop.
      } else {
        Constant *LHS = ValueList.getConstantFwdRef(Record[1], CurTy);
        Constant *RHS = ValueList.getConstantFwdRef(Record[2], CurTy);
        unsigned Flags = 0;
        if (Record.size() >= 4) {
          if (Opc == Instruction::Add ||
              Opc == Instruction::Sub ||
              Opc == Instruction::Mul ||
              Opc == Instruction::Shl) {
            if (Record[3] & (1 << bitc::OBO_NO_SIGNED_WRAP))
              Flags |= OverflowingBinaryOperator::NoSignedWrap;
            if (Record[3] & (1 << bitc::OBO_NO_UNSIGNED_WRAP))
              Flags |= OverflowingBinaryOperator::NoUnsignedWrap;
          } else if (Opc == Instruction::SDiv ||
                     Opc == Instruction::UDiv ||
                     Opc == Instruction::LShr ||
                     Opc == Instruction::AShr) {
            if (Record[3] & (1 << bitc::PEO_EXACT))
              Flags |= SDivOperator::IsExact;
          }
        }
        V = ConstantExpr::get(Opc, LHS, RHS, Flags);
      }
      break;
    }
    case bitc::CST_CODE_CE_CAST: {  // CE_CAST: [opcode, opty, opval]
      if (Record.size() < 3)
        return error("Invalid record");
      int Opc = getDecodedCastOpcode(Record[0]);
      if (Opc < 0) {
        V = UndefValue::get(CurTy);  // Unknown cast.
      } else {
        Type *OpTy = getTypeByID(Record[1]);
        if (!OpTy)
          return error("Invalid record");
        Constant *Op = ValueList.getConstantFwdRef(Record[2], OpTy);
        V = UpgradeBitCastExpr(Opc, Op, CurTy);
        if (!V) V = ConstantExpr::getCast(Opc, Op, CurTy);
      }
      break;
    }
    case bitc::CST_CODE_CE_INBOUNDS_GEP: // [ty, n x operands]
    case bitc::CST_CODE_CE_GEP: // [ty, n x operands]
    case bitc::CST_CODE_CE_GEP_WITH_INRANGE_INDEX: { // [ty, flags, n x
                                                     // operands]
      unsigned OpNum = 0;
      Type *PointeeType = nullptr;
      if (BitCode == bitc::CST_CODE_CE_GEP_WITH_INRANGE_INDEX ||
          Record.size() % 2)
        PointeeType = getTypeByID(Record[OpNum++]);

      bool InBounds = false;
      Optional<unsigned> InRangeIndex;
      if (BitCode == bitc::CST_CODE_CE_GEP_WITH_INRANGE_INDEX) {
        uint64_t Op = Record[OpNum++];
        InBounds = Op & 1;
        InRangeIndex = Op >> 1;
      } else if (BitCode == bitc::CST_CODE_CE_INBOUNDS_GEP)
        InBounds = true;

      SmallVector<Constant*, 16> Elts;
      while (OpNum != Record.size()) {
        Type *ElTy = getTypeByID(Record[OpNum++]);
        if (!ElTy)
          return error("Invalid record");
        Elts.push_back(ValueList.getConstantFwdRef(Record[OpNum++], ElTy));
      }

      if (PointeeType &&
          PointeeType !=
              cast<PointerType>(Elts[0]->getType()->getScalarType())
                  ->getElementType())
        return error("Explicit gep operator type does not match pointee type "
                     "of pointer operand");

      if (Elts.size() < 1)
        return error("Invalid gep with no operands");

      ArrayRef<Constant *> Indices(Elts.begin() + 1, Elts.end());
      V = ConstantExpr::getGetElementPtr(PointeeType, Elts[0], Indices,
                                         InBounds, InRangeIndex);
      break;
    }
    case bitc::CST_CODE_CE_SELECT: {  // CE_SELECT: [opval#, opval#, opval#]
      if (Record.size() < 3)
        return error("Invalid record");

      Type *SelectorTy = Type::getInt1Ty(Context);

      // The selector might be an i1 or an <n x i1>
      // Get the type from the ValueList before getting a forward ref.
      if (VectorType *VTy = dyn_cast<VectorType>(CurTy))
        if (Value *V = ValueList[Record[0]])
          if (SelectorTy != V->getType())
            SelectorTy = VectorType::get(SelectorTy, VTy->getNumElements());

      V = ConstantExpr::getSelect(ValueList.getConstantFwdRef(Record[0],
                                                              SelectorTy),
                                  ValueList.getConstantFwdRef(Record[1],CurTy),
                                  ValueList.getConstantFwdRef(Record[2],CurTy));
      break;
    }
    case bitc::CST_CODE_CE_EXTRACTELT
        : { // CE_EXTRACTELT: [opty, opval, opty, opval]
      if (Record.size() < 3)
        return error("Invalid record");
      VectorType *OpTy =
        dyn_cast_or_null<VectorType>(getTypeByID(Record[0]));
      if (!OpTy)
        return error("Invalid record");
      Constant *Op0 = ValueList.getConstantFwdRef(Record[1], OpTy);
      Constant *Op1 = nullptr;
      if (Record.size() == 4) {
        Type *IdxTy = getTypeByID(Record[2]);
        if (!IdxTy)
          return error("Invalid record");
        Op1 = ValueList.getConstantFwdRef(Record[3], IdxTy);
      } else // TODO: Remove with llvm 4.0
        Op1 = ValueList.getConstantFwdRef(Record[2], Type::getInt32Ty(Context));
      if (!Op1)
        return error("Invalid record");
      V = ConstantExpr::getExtractElement(Op0, Op1);
      break;
    }
    case bitc::CST_CODE_CE_INSERTELT
        : { // CE_INSERTELT: [opval, opval, opty, opval]
      VectorType *OpTy = dyn_cast<VectorType>(CurTy);
      if (Record.size() < 3 || !OpTy)
        return error("Invalid record");
      Constant *Op0 = ValueList.getConstantFwdRef(Record[0], OpTy);
      Constant *Op1 = ValueList.getConstantFwdRef(Record[1],
                                                  OpTy->getElementType());
      Constant *Op2 = nullptr;
      if (Record.size() == 4) {
        Type *IdxTy = getTypeByID(Record[2]);
        if (!IdxTy)
          return error("Invalid record");
        Op2 = ValueList.getConstantFwdRef(Record[3], IdxTy);
      } else // TODO: Remove with llvm 4.0
        Op2 = ValueList.getConstantFwdRef(Record[2], Type::getInt32Ty(Context));
      if (!Op2)
        return error("Invalid record");
      V = ConstantExpr::getInsertElement(Op0, Op1, Op2);
      break;
    }
    case bitc::CST_CODE_CE_SHUFFLEVEC: { // CE_SHUFFLEVEC: [opval, opval, opval]
      VectorType *OpTy = dyn_cast<VectorType>(CurTy);
      if (Record.size() < 3 || !OpTy)
        return error("Invalid record");
      Constant *Op0 = ValueList.getConstantFwdRef(Record[0], OpTy);
      Constant *Op1 = ValueList.getConstantFwdRef(Record[1], OpTy);
      Type *ShufTy = VectorType::get(Type::getInt32Ty(Context),
                                                 OpTy->getNumElements());
      Constant *Op2 = ValueList.getConstantFwdRef(Record[2], ShufTy);
      V = ConstantExpr::getShuffleVector(Op0, Op1, Op2);
      break;
    }
    case bitc::CST_CODE_CE_SHUFVEC_EX: { // [opty, opval, opval, opval]
      VectorType *RTy = dyn_cast<VectorType>(CurTy);
      VectorType *OpTy =
        dyn_cast_or_null<VectorType>(getTypeByID(Record[0]));
      if (Record.size() < 4 || !RTy || !OpTy)
        return error("Invalid record");
      Constant *Op0 = ValueList.getConstantFwdRef(Record[1], OpTy);
      Constant *Op1 = ValueList.getConstantFwdRef(Record[2], OpTy);
      Type *ShufTy = VectorType::get(Type::getInt32Ty(Context),
                                                 RTy->getNumElements());
      Constant *Op2 = ValueList.getConstantFwdRef(Record[3], ShufTy);
      V = ConstantExpr::getShuffleVector(Op0, Op1, Op2);
      break;
    }
    case bitc::CST_CODE_CE_CMP: {     // CE_CMP: [opty, opval, opval, pred]
      if (Record.size() < 4)
        return error("Invalid record");
      Type *OpTy = getTypeByID(Record[0]);
      if (!OpTy)
        return error("Invalid record");
      Constant *Op0 = ValueList.getConstantFwdRef(Record[1], OpTy);
      Constant *Op1 = ValueList.getConstantFwdRef(Record[2], OpTy);

      if (OpTy->isFPOrFPVectorTy())
        V = ConstantExpr::getFCmp(Record[3], Op0, Op1);
      else
        V = ConstantExpr::getICmp(Record[3], Op0, Op1);
      break;
    }
    // This maintains backward compatibility, pre-asm dialect keywords.
    // FIXME: Remove with the 4.0 release.
    case bitc::CST_CODE_INLINEASM_OLD: {
      if (Record.size() < 2)
        return error("Invalid record");
      std::string AsmStr, ConstrStr;
      bool HasSideEffects = Record[0] & 1;
      bool IsAlignStack = Record[0] >> 1;
      unsigned AsmStrSize = Record[1];
      if (2+AsmStrSize >= Record.size())
        return error("Invalid record");
      unsigned ConstStrSize = Record[2+AsmStrSize];
      if (3+AsmStrSize+ConstStrSize > Record.size())
        return error("Invalid record");

      for (unsigned i = 0; i != AsmStrSize; ++i)
        AsmStr += (char)Record[2+i];
      for (unsigned i = 0; i != ConstStrSize; ++i)
        ConstrStr += (char)Record[3+AsmStrSize+i];
      PointerType *PTy = cast<PointerType>(CurTy);
      V = InlineAsm::get(cast<FunctionType>(PTy->getElementType()),
                         AsmStr, ConstrStr, HasSideEffects, IsAlignStack);
      break;
    }
    // This version adds support for the asm dialect keywords (e.g.,
    // inteldialect).
    case bitc::CST_CODE_INLINEASM: {
      if (Record.size() < 2)
        return error("Invalid record");
      std::string AsmStr, ConstrStr;
      bool HasSideEffects = Record[0] & 1;
      bool IsAlignStack = (Record[0] >> 1) & 1;
      unsigned AsmDialect = Record[0] >> 2;
      unsigned AsmStrSize = Record[1];
      if (2+AsmStrSize >= Record.size())
        return error("Invalid record");
      unsigned ConstStrSize = Record[2+AsmStrSize];
      if (3+AsmStrSize+ConstStrSize > Record.size())
        return error("Invalid record");

      for (unsigned i = 0; i != AsmStrSize; ++i)
        AsmStr += (char)Record[2+i];
      for (unsigned i = 0; i != ConstStrSize; ++i)
        ConstrStr += (char)Record[3+AsmStrSize+i];
      PointerType *PTy = cast<PointerType>(CurTy);
      V = InlineAsm::get(cast<FunctionType>(PTy->getElementType()),
                         AsmStr, ConstrStr, HasSideEffects, IsAlignStack,
                         InlineAsm::AsmDialect(AsmDialect));
      break;
    }
    case bitc::CST_CODE_BLOCKADDRESS:{
      if (Record.size() < 3)
        return error("Invalid record");
      Type *FnTy = getTypeByID(Record[0]);
      if (!FnTy)
        return error("Invalid record");
      Function *Fn =
        dyn_cast_or_null<Function>(ValueList.getConstantFwdRef(Record[1],FnTy));
      if (!Fn)
        return error("Invalid record");

      // If the function is already parsed we can insert the block address right
      // away.
      BasicBlock *BB;
      unsigned BBID = Record[2];
      if (!BBID)
        // Invalid reference to entry block.
        return error("Invalid ID");
      if (!Fn->empty()) {
        Function::iterator BBI = Fn->begin(), BBE = Fn->end();
        for (size_t I = 0, E = BBID; I != E; ++I) {
          if (BBI == BBE)
            return error("Invalid ID");
          ++BBI;
        }
        BB = &*BBI;
      } else {
        // Otherwise insert a placeholder and remember it so it can be inserted
        // when the function is parsed.
        auto &FwdBBs = BasicBlockFwdRefs[Fn];
        if (FwdBBs.empty())
          BasicBlockFwdRefQueue.push_back(Fn);
        if (FwdBBs.size() < BBID + 1)
          FwdBBs.resize(BBID + 1);
        if (!FwdBBs[BBID])
          FwdBBs[BBID] = BasicBlock::Create(Context);
        BB = FwdBBs[BBID];
      }
      V = BlockAddress::get(Fn, BB);
      break;
    }
    }

    ValueList.assignValue(V, NextCstNo);
    ++NextCstNo;
  }
}

Error BitcodeReader::parseUseLists() {
  if (Stream.EnterSubBlock(bitc::USELIST_BLOCK_ID))
    return error("Invalid record");

  // Read all the records.
  SmallVector<uint64_t, 64> Record;

  while (true) {
    BitstreamEntry Entry = Stream.advanceSkippingSubblocks();

    switch (Entry.Kind) {
    case BitstreamEntry::SubBlock: // Handled for us already.
    case BitstreamEntry::Error:
      return error("Malformed block");
    case BitstreamEntry::EndBlock:
      return Error::success();
    case BitstreamEntry::Record:
      // The interesting case.
      break;
    }

    // Read a use list record.
    Record.clear();
    bool IsBB = false;
    switch (Stream.readRecord(Entry.ID, Record)) {
    default:  // Default behavior: unknown type.
      break;
    case bitc::USELIST_CODE_BB:
      IsBB = true;
      LLVM_FALLTHROUGH;
    case bitc::USELIST_CODE_DEFAULT: {
      unsigned RecordLength = Record.size();
      if (RecordLength < 3)
        // Records should have at least an ID and two indexes.
        return error("Invalid record");
      unsigned ID = Record.back();
      Record.pop_back();

      Value *V;
      if (IsBB) {
        assert(ID < FunctionBBs.size() && "Basic block not found");
        V = FunctionBBs[ID];
      } else
        V = ValueList[ID];
      unsigned NumUses = 0;
      SmallDenseMap<const Use *, unsigned, 16> Order;
      for (const Use &U : V->materialized_uses()) {
        if (++NumUses > Record.size())
          break;
        Order[&U] = Record[NumUses - 1];
      }
      if (Order.size() != Record.size() || NumUses > Record.size())
        // Mismatches can happen if the functions are being materialized lazily
        // (out-of-order), or a value has been upgraded.
        break;

      V->sortUseList([&](const Use &L, const Use &R) {
        return Order.lookup(&L) < Order.lookup(&R);
      });
      break;
    }
    }
  }
}

/// When we see the block for metadata, remember where it is and then skip it.
/// This lets us lazily deserialize the metadata.
Error BitcodeReader::rememberAndSkipMetadata() {
  // Save the current stream state.
  uint64_t CurBit = Stream.GetCurrentBitNo();
  DeferredMetadataInfo.push_back(CurBit);

  // Skip over the block for now.
  if (Stream.SkipBlock())
    return error("Invalid record");
  return Error::success();
}

Error BitcodeReader::materializeMetadata() {
  for (uint64_t BitPos : DeferredMetadataInfo) {
    // Move the bit stream to the saved position.
    Stream.JumpToBit(BitPos);
    if (Error Err = parseMetadata(true))
      return Err;
  }
  DeferredMetadataInfo.clear();
  return Error::success();
}

void BitcodeReader::setStripDebugInfo() { StripDebugInfo = true; }

/// When we see the block for a function body, remember where it is and then
/// skip it.  This lets us lazily deserialize the functions.
Error BitcodeReader::rememberAndSkipFunctionBody() {
  // Get the function we are talking about.
  if (FunctionsWithBodies.empty())
    return error("Insufficient function protos");

  Function *Fn = FunctionsWithBodies.back();
  FunctionsWithBodies.pop_back();

  // Save the current stream state.
  uint64_t CurBit = Stream.GetCurrentBitNo();
  assert(
      (DeferredFunctionInfo[Fn] == 0 || DeferredFunctionInfo[Fn] == CurBit) &&
      "Mismatch between VST and scanned function offsets");
  DeferredFunctionInfo[Fn] = CurBit;

  // Skip over the function block for now.
  if (Stream.SkipBlock())
    return error("Invalid record");
  return Error::success();
}

Error BitcodeReader::globalCleanup() {
  // Patch the initializers for globals and aliases up.
  if (Error Err = resolveGlobalAndIndirectSymbolInits())
    return Err;
  if (!GlobalInits.empty() || !IndirectSymbolInits.empty())
    return error("Malformed global initializer set");

  // Look for intrinsic functions which need to be upgraded at some point
  for (Function &F : *TheModule) {
    Function *NewFn;
    if (UpgradeIntrinsicFunction(&F, NewFn))
      UpgradedIntrinsics[&F] = NewFn;
    else if (auto Remangled = Intrinsic::remangleIntrinsicFunction(&F))
      // Some types could be renamed during loading if several modules are
      // loaded in the same LLVMContext (LTO scenario). In this case we should
      // remangle intrinsics names as well.
      RemangledIntrinsics[&F] = Remangled.getValue();
  }

  // Look for global variables which need to be renamed.
  for (GlobalVariable &GV : TheModule->globals())
    UpgradeGlobalVariable(&GV);

  // Force deallocation of memory for these vectors to favor the client that
  // want lazy deserialization.
  std::vector<std::pair<GlobalVariable*, unsigned> >().swap(GlobalInits);
  std::vector<std::pair<GlobalIndirectSymbol*, unsigned> >().swap(
      IndirectSymbolInits);
  return Error::success();
}

/// Support for lazy parsing of function bodies. This is required if we
/// either have an old bitcode file without a VST forward declaration record,
/// or if we have an anonymous function being materialized, since anonymous
/// functions do not have a name and are therefore not in the VST.
Error BitcodeReader::rememberAndSkipFunctionBodies() {
  Stream.JumpToBit(NextUnreadBit);

  if (Stream.AtEndOfStream())
    return error("Could not find function in stream");

  if (!SeenFirstFunctionBody)
    return error("Trying to materialize functions before seeing function blocks");

  // An old bitcode file with the symbol table at the end would have
  // finished the parse greedily.
  assert(SeenValueSymbolTable);

  SmallVector<uint64_t, 64> Record;

  while (true) {
    BitstreamEntry Entry = Stream.advance();
    switch (Entry.Kind) {
    default:
      return error("Expect SubBlock");
    case BitstreamEntry::SubBlock:
      switch (Entry.ID) {
      default:
        return error("Expect function block");
      case bitc::FUNCTION_BLOCK_ID:
        if (Error Err = rememberAndSkipFunctionBody())
          return Err;
        NextUnreadBit = Stream.GetCurrentBitNo();
        return Error::success();
      }
    }
  }
}

bool BitcodeReaderBase::readBlockInfo() {
  Optional<BitstreamBlockInfo> NewBlockInfo = Stream.ReadBlockInfoBlock();
  if (!NewBlockInfo)
    return true;
  BlockInfo = std::move(*NewBlockInfo);
  return false;
}

Error BitcodeReader::parseModule(uint64_t ResumeBit,
                                 bool ShouldLazyLoadMetadata) {
  if (ResumeBit)
    Stream.JumpToBit(ResumeBit);
  else if (Stream.EnterSubBlock(bitc::MODULE_BLOCK_ID))
    return error("Invalid record");

  SmallVector<uint64_t, 64> Record;
  std::vector<std::string> SectionTable;
  std::vector<std::string> GCTable;

  // Read all the records for this module.
  while (true) {
    BitstreamEntry Entry = Stream.advance();

    switch (Entry.Kind) {
    case BitstreamEntry::Error:
      return error("Malformed block");
    case BitstreamEntry::EndBlock:
      return globalCleanup();

    case BitstreamEntry::SubBlock:
      switch (Entry.ID) {
      default:  // Skip unknown content.
        if (Stream.SkipBlock())
          return error("Invalid record");
        break;
      case bitc::BLOCKINFO_BLOCK_ID:
        if (readBlockInfo())
          return error("Malformed block");
        break;
      case bitc::PARAMATTR_BLOCK_ID:
        if (Error Err = parseAttributeBlock())
          return Err;
        break;
      case bitc::PARAMATTR_GROUP_BLOCK_ID:
        if (Error Err = parseAttributeGroupBlock())
          return Err;
        break;
      case bitc::TYPE_BLOCK_ID_NEW:
        if (Error Err = parseTypeTable())
          return Err;
        break;
      case bitc::VALUE_SYMTAB_BLOCK_ID:
        if (!SeenValueSymbolTable) {
          // Either this is an old form VST without function index and an
          // associated VST forward declaration record (which would have caused
          // the VST to be jumped to and parsed before it was encountered
          // normally in the stream), or there were no function blocks to
          // trigger an earlier parsing of the VST.
          assert(VSTOffset == 0 || FunctionsWithBodies.empty());
          if (Error Err = parseValueSymbolTable())
            return Err;
          SeenValueSymbolTable = true;
        } else {
          // We must have had a VST forward declaration record, which caused
          // the parser to jump to and parse the VST earlier.
          assert(VSTOffset > 0);
          if (Stream.SkipBlock())
            return error("Invalid record");
        }
        break;
      case bitc::CONSTANTS_BLOCK_ID:
        if (Error Err = parseConstants())
          return Err;
        if (Error Err = resolveGlobalAndIndirectSymbolInits())
          return Err;
        break;
      case bitc::METADATA_BLOCK_ID:
        if (ShouldLazyLoadMetadata && !IsMetadataMaterialized) {
          if (Error Err = rememberAndSkipMetadata())
            return Err;
          break;
        }
        assert(DeferredMetadataInfo.empty() && "Unexpected deferred metadata");
        if (Error Err = parseMetadata(true))
          return Err;
        break;
      case bitc::METADATA_KIND_BLOCK_ID:
        if (Error Err = parseMetadataKinds())
          return Err;
        break;
      case bitc::FUNCTION_BLOCK_ID:
        // If this is the first function body we've seen, reverse the
        // FunctionsWithBodies list.
        if (!SeenFirstFunctionBody) {
          std::reverse(FunctionsWithBodies.begin(), FunctionsWithBodies.end());
          if (Error Err = globalCleanup())
            return Err;
          SeenFirstFunctionBody = true;
        }

        if (VSTOffset > 0) {
          // If we have a VST forward declaration record, make sure we
          // parse the VST now if we haven't already. It is needed to
          // set up the DeferredFunctionInfo vector for lazy reading.
          if (!SeenValueSymbolTable) {
            if (Error Err = BitcodeReader::parseValueSymbolTable(VSTOffset))
              return Err;
            SeenValueSymbolTable = true;
            // Fall through so that we record the NextUnreadBit below.
            // This is necessary in case we have an anonymous function that
            // is later materialized. Since it will not have a VST entry we
            // need to fall back to the lazy parse to find its offset.
          } else {
            // If we have a VST forward declaration record, but have already
            // parsed the VST (just above, when the first function body was
            // encountered here), then we are resuming the parse after
            // materializing functions. The ResumeBit points to the
            // start of the last function block recorded in the
            // DeferredFunctionInfo map. Skip it.
            if (Stream.SkipBlock())
              return error("Invalid record");
            continue;
          }
        }

        // Support older bitcode files that did not have the function
        // index in the VST, nor a VST forward declaration record, as
        // well as anonymous functions that do not have VST entries.
        // Build the DeferredFunctionInfo vector on the fly.
        if (Error Err = rememberAndSkipFunctionBody())
          return Err;

        // Suspend parsing when we reach the function bodies. Subsequent
        // materialization calls will resume it when necessary. If the bitcode
        // file is old, the symbol table will be at the end instead and will not
        // have been seen yet. In this case, just finish the parse now.
        if (SeenValueSymbolTable) {
          NextUnreadBit = Stream.GetCurrentBitNo();
          // After the VST has been parsed, we need to make sure intrinsic name
          // are auto-upgraded.
          return globalCleanup();
        }
        break;
      case bitc::USELIST_BLOCK_ID:
        if (Error Err = parseUseLists())
          return Err;
        break;
      case bitc::OPERAND_BUNDLE_TAGS_BLOCK_ID:
        if (Error Err = parseOperandBundleTags())
          return Err;
        break;
      }
      continue;

    case BitstreamEntry::Record:
      // The interesting case.
      break;
    }

    // Read a record.
    auto BitCode = Stream.readRecord(Entry.ID, Record);
    switch (BitCode) {
    default: break;  // Default behavior, ignore unknown content.
    case bitc::MODULE_CODE_VERSION: {  // VERSION: [version#]
      if (Record.size() < 1)
        return error("Invalid record");
      // Only version #0 and #1 are supported so far.
      unsigned module_version = Record[0];
      switch (module_version) {
        default:
          return error("Invalid value");
        case 0:
          UseRelativeIDs = false;
          break;
        case 1:
          UseRelativeIDs = true;
          break;
      }
      break;
    }
    case bitc::MODULE_CODE_TRIPLE: {  // TRIPLE: [strchr x N]
      std::string S;
      if (convertToString(Record, 0, S))
        return error("Invalid record");
      TheModule->setTargetTriple(S);
      break;
    }
    case bitc::MODULE_CODE_DATALAYOUT: {  // DATALAYOUT: [strchr x N]
      std::string S;
      if (convertToString(Record, 0, S))
        return error("Invalid record");
      TheModule->setDataLayout(S);
      break;
    }
    case bitc::MODULE_CODE_ASM: {  // ASM: [strchr x N]
      std::string S;
      if (convertToString(Record, 0, S))
        return error("Invalid record");
      TheModule->setModuleInlineAsm(S);
      break;
    }
    case bitc::MODULE_CODE_DEPLIB: {  // DEPLIB: [strchr x N]
      // FIXME: Remove in 4.0.
      std::string S;
      if (convertToString(Record, 0, S))
        return error("Invalid record");
      // Ignore value.
      break;
    }
    case bitc::MODULE_CODE_SECTIONNAME: {  // SECTIONNAME: [strchr x N]
      std::string S;
      if (convertToString(Record, 0, S))
        return error("Invalid record");
      SectionTable.push_back(S);
      break;
    }
    case bitc::MODULE_CODE_GCNAME: {  // SECTIONNAME: [strchr x N]
      std::string S;
      if (convertToString(Record, 0, S))
        return error("Invalid record");
      GCTable.push_back(S);
      break;
    }
    case bitc::MODULE_CODE_COMDAT: { // COMDAT: [selection_kind, name]
      if (Record.size() < 2)
        return error("Invalid record");
      Comdat::SelectionKind SK = getDecodedComdatSelectionKind(Record[0]);
      unsigned ComdatNameSize = Record[1];
      std::string ComdatName;
      ComdatName.reserve(ComdatNameSize);
      for (unsigned i = 0; i != ComdatNameSize; ++i)
        ComdatName += (char)Record[2 + i];
      Comdat *C = TheModule->getOrInsertComdat(ComdatName);
      C->setSelectionKind(SK);
      ComdatList.push_back(C);
      break;
    }
    // GLOBALVAR: [pointer type, isconst, initid,
    //             linkage, alignment, section, visibility, threadlocal,
    //             unnamed_addr, externally_initialized, dllstorageclass,
    //             comdat]
    case bitc::MODULE_CODE_GLOBALVAR: {
      if (Record.size() < 6)
        return error("Invalid record");
      Type *Ty = getTypeByID(Record[0]);
      if (!Ty)
        return error("Invalid record");
      bool isConstant = Record[1] & 1;
      bool explicitType = Record[1] & 2;
      unsigned AddressSpace;
      if (explicitType) {
        AddressSpace = Record[1] >> 2;
      } else {
        if (!Ty->isPointerTy())
          return error("Invalid type for value");
        AddressSpace = cast<PointerType>(Ty)->getAddressSpace();
        Ty = cast<PointerType>(Ty)->getElementType();
      }

      uint64_t RawLinkage = Record[3];
      GlobalValue::LinkageTypes Linkage = getDecodedLinkage(RawLinkage);
      unsigned Alignment;
      if (Error Err = parseAlignmentValue(Record[4], Alignment))
        return Err;
      std::string Section;
      if (Record[5]) {
        if (Record[5]-1 >= SectionTable.size())
          return error("Invalid ID");
        Section = SectionTable[Record[5]-1];
      }
      GlobalValue::VisibilityTypes Visibility = GlobalValue::DefaultVisibility;
      // Local linkage must have default visibility.
      if (Record.size() > 6 && !GlobalValue::isLocalLinkage(Linkage))
        // FIXME: Change to an error if non-default in 4.0.
        Visibility = getDecodedVisibility(Record[6]);

      GlobalVariable::ThreadLocalMode TLM = GlobalVariable::NotThreadLocal;
      if (Record.size() > 7)
        TLM = getDecodedThreadLocalMode(Record[7]);

      GlobalValue::UnnamedAddr UnnamedAddr = GlobalValue::UnnamedAddr::None;
      if (Record.size() > 8)
        UnnamedAddr = getDecodedUnnamedAddrType(Record[8]);

      bool ExternallyInitialized = false;
      if (Record.size() > 9)
        ExternallyInitialized = Record[9];

      GlobalVariable *NewGV =
        new GlobalVariable(*TheModule, Ty, isConstant, Linkage, nullptr, "", nullptr,
                           TLM, AddressSpace, ExternallyInitialized);
      NewGV->setAlignment(Alignment);
      if (!Section.empty())
        NewGV->setSection(Section);
      NewGV->setVisibility(Visibility);
      NewGV->setUnnamedAddr(UnnamedAddr);

      if (Record.size() > 10)
        NewGV->setDLLStorageClass(getDecodedDLLStorageClass(Record[10]));
      else
        upgradeDLLImportExportLinkage(NewGV, RawLinkage);

      ValueList.push_back(NewGV);

      // Remember which value to use for the global initializer.
      if (unsigned InitID = Record[2])
        GlobalInits.push_back(std::make_pair(NewGV, InitID-1));

      if (Record.size() > 11) {
        if (unsigned ComdatID = Record[11]) {
          if (ComdatID > ComdatList.size())
            return error("Invalid global variable comdat ID");
          NewGV->setComdat(ComdatList[ComdatID - 1]);
        }
      } else if (hasImplicitComdat(RawLinkage)) {
        NewGV->setComdat(reinterpret_cast<Comdat *>(1));
      }

      break;
    }
    // FUNCTION:  [type, callingconv, isproto, linkage, paramattr,
    //             alignment, section, visibility, gc, unnamed_addr,
    //             prologuedata, dllstorageclass, comdat, prefixdata]
    case bitc::MODULE_CODE_FUNCTION: {
      if (Record.size() < 8)
        return error("Invalid record");
      Type *Ty = getTypeByID(Record[0]);
      if (!Ty)
        return error("Invalid record");
      if (auto *PTy = dyn_cast<PointerType>(Ty))
        Ty = PTy->getElementType();
      auto *FTy = dyn_cast<FunctionType>(Ty);
      if (!FTy)
        return error("Invalid type for value");
      auto CC = static_cast<CallingConv::ID>(Record[1]);
      if (CC & ~CallingConv::MaxID)
        return error("Invalid calling convention ID");

      Function *Func = Function::Create(FTy, GlobalValue::ExternalLinkage,
                                        "", TheModule);

      Func->setCallingConv(CC);
      bool isProto = Record[2];
      uint64_t RawLinkage = Record[3];
      Func->setLinkage(getDecodedLinkage(RawLinkage));
      Func->setAttributes(getAttributes(Record[4]));

      unsigned Alignment;
      if (Error Err = parseAlignmentValue(Record[5], Alignment))
        return Err;
      Func->setAlignment(Alignment);
      if (Record[6]) {
        if (Record[6]-1 >= SectionTable.size())
          return error("Invalid ID");
        Func->setSection(SectionTable[Record[6]-1]);
      }
      // Local linkage must have default visibility.
      if (!Func->hasLocalLinkage())
        // FIXME: Change to an error if non-default in 4.0.
        Func->setVisibility(getDecodedVisibility(Record[7]));
      if (Record.size() > 8 && Record[8]) {
        if (Record[8]-1 >= GCTable.size())
          return error("Invalid ID");
        Func->setGC(GCTable[Record[8] - 1]);
      }
      GlobalValue::UnnamedAddr UnnamedAddr = GlobalValue::UnnamedAddr::None;
      if (Record.size() > 9)
        UnnamedAddr = getDecodedUnnamedAddrType(Record[9]);
      Func->setUnnamedAddr(UnnamedAddr);
      if (Record.size() > 10 && Record[10] != 0)
        FunctionPrologues.push_back(std::make_pair(Func, Record[10]-1));

      if (Record.size() > 11)
        Func->setDLLStorageClass(getDecodedDLLStorageClass(Record[11]));
      else
        upgradeDLLImportExportLinkage(Func, RawLinkage);

      if (Record.size() > 12) {
        if (unsigned ComdatID = Record[12]) {
          if (ComdatID > ComdatList.size())
            return error("Invalid function comdat ID");
          Func->setComdat(ComdatList[ComdatID - 1]);
        }
      } else if (hasImplicitComdat(RawLinkage)) {
        Func->setComdat(reinterpret_cast<Comdat *>(1));
      }

      if (Record.size() > 13 && Record[13] != 0)
        FunctionPrefixes.push_back(std::make_pair(Func, Record[13]-1));

      if (Record.size() > 14 && Record[14] != 0)
        FunctionPersonalityFns.push_back(std::make_pair(Func, Record[14] - 1));

      ValueList.push_back(Func);

      // If this is a function with a body, remember the prototype we are
      // creating now, so that we can match up the body with them later.
      if (!isProto) {
        Func->setIsMaterializable(true);
        FunctionsWithBodies.push_back(Func);
        DeferredFunctionInfo[Func] = 0;
      }
      break;
    }
    // ALIAS: [alias type, addrspace, aliasee val#, linkage]
    // ALIAS: [alias type, addrspace, aliasee val#, linkage, visibility, dllstorageclass]
    // IFUNC: [alias type, addrspace, aliasee val#, linkage, visibility, dllstorageclass]
    case bitc::MODULE_CODE_IFUNC:
    case bitc::MODULE_CODE_ALIAS:
    case bitc::MODULE_CODE_ALIAS_OLD: {
      bool NewRecord = BitCode != bitc::MODULE_CODE_ALIAS_OLD;
      if (Record.size() < (3 + (unsigned)NewRecord))
        return error("Invalid record");
      unsigned OpNum = 0;
      Type *Ty = getTypeByID(Record[OpNum++]);
      if (!Ty)
        return error("Invalid record");

      unsigned AddrSpace;
      if (!NewRecord) {
        auto *PTy = dyn_cast<PointerType>(Ty);
        if (!PTy)
          return error("Invalid type for value");
        Ty = PTy->getElementType();
        AddrSpace = PTy->getAddressSpace();
      } else {
        AddrSpace = Record[OpNum++];
      }

      auto Val = Record[OpNum++];
      auto Linkage = Record[OpNum++];
      GlobalIndirectSymbol *NewGA;
      if (BitCode == bitc::MODULE_CODE_ALIAS ||
          BitCode == bitc::MODULE_CODE_ALIAS_OLD)
        NewGA = GlobalAlias::create(Ty, AddrSpace, getDecodedLinkage(Linkage),
                                    "", TheModule);
      else
        NewGA = GlobalIFunc::create(Ty, AddrSpace, getDecodedLinkage(Linkage),
                                    "", nullptr, TheModule);
      // Old bitcode files didn't have visibility field.
      // Local linkage must have default visibility.
      if (OpNum != Record.size()) {
        auto VisInd = OpNum++;
        if (!NewGA->hasLocalLinkage())
          // FIXME: Change to an error if non-default in 4.0.
          NewGA->setVisibility(getDecodedVisibility(Record[VisInd]));
      }
      if (OpNum != Record.size())
        NewGA->setDLLStorageClass(getDecodedDLLStorageClass(Record[OpNum++]));
      else
        upgradeDLLImportExportLinkage(NewGA, Linkage);
      if (OpNum != Record.size())
        NewGA->setThreadLocalMode(getDecodedThreadLocalMode(Record[OpNum++]));
      if (OpNum != Record.size())
        NewGA->setUnnamedAddr(getDecodedUnnamedAddrType(Record[OpNum++]));
      ValueList.push_back(NewGA);
      IndirectSymbolInits.push_back(std::make_pair(NewGA, Val));
      break;
    }
    /// MODULE_CODE_PURGEVALS: [numvals]
    case bitc::MODULE_CODE_PURGEVALS:
      // Trim down the value list to the specified size.
      if (Record.size() < 1 || Record[0] > ValueList.size())
        return error("Invalid record");
      ValueList.shrinkTo(Record[0]);
      break;
    /// MODULE_CODE_VSTOFFSET: [offset]
    case bitc::MODULE_CODE_VSTOFFSET:
      if (Record.size() < 1)
        return error("Invalid record");
      // Note that we subtract 1 here because the offset is relative to one word
      // before the start of the identification or module block, which was
      // historically always the start of the regular bitcode header.
      VSTOffset = Record[0] - 1;
      break;
    /// MODULE_CODE_SOURCE_FILENAME: [namechar x N]
    case bitc::MODULE_CODE_SOURCE_FILENAME:
      SmallString<128> ValueName;
      if (convertToString(Record, 0, ValueName))
        return error("Invalid record");
      TheModule->setSourceFileName(ValueName);
      break;
    }
    Record.clear();
  }
}

Error BitcodeReader::parseBitcodeInto(Module *M, bool ShouldLazyLoadMetadata) {
  TheModule = M;
  return parseModule(0, ShouldLazyLoadMetadata);
}

Error BitcodeReader::parseGlobalObjectAttachment(GlobalObject &GO,
                                                 ArrayRef<uint64_t> Record) {
  assert(Record.size() % 2 == 0);
  for (unsigned I = 0, E = Record.size(); I != E; I += 2) {
    auto K = MDKindMap.find(Record[I]);
    if (K == MDKindMap.end())
      return error("Invalid ID");
    MDNode *MD = MetadataList.getMDNodeFwdRefOrNull(Record[I + 1]);
    if (!MD)
      return error("Invalid metadata attachment");
    GO.addMetadata(K->second, *MD);
  }
  return Error::success();
}

/// Parse metadata attachments.
Error BitcodeReader::parseMetadataAttachment(Function &F) {
  if (Stream.EnterSubBlock(bitc::METADATA_ATTACHMENT_ID))
    return error("Invalid record");

  SmallVector<uint64_t, 64> Record;

  while (true) {
    BitstreamEntry Entry = Stream.advanceSkippingSubblocks();

    switch (Entry.Kind) {
    case BitstreamEntry::SubBlock: // Handled for us already.
    case BitstreamEntry::Error:
      return error("Malformed block");
    case BitstreamEntry::EndBlock:
      return Error::success();
    case BitstreamEntry::Record:
      // The interesting case.
      break;
    }

    // Read a metadata attachment record.
    Record.clear();
    switch (Stream.readRecord(Entry.ID, Record)) {
    default:  // Default behavior: ignore.
      break;
    case bitc::METADATA_ATTACHMENT: {
      unsigned RecordLength = Record.size();
      if (Record.empty())
        return error("Invalid record");
      if (RecordLength % 2 == 0) {
        // A function attachment.
        if (Error Err = parseGlobalObjectAttachment(F, Record))
          return Err;
        continue;
      }

      // An instruction attachment.
      Instruction *Inst = InstructionList[Record[0]];
      for (unsigned i = 1; i != RecordLength; i = i+2) {
        unsigned Kind = Record[i];
        DenseMap<unsigned, unsigned>::iterator I =
          MDKindMap.find(Kind);
        if (I == MDKindMap.end())
          return error("Invalid ID");
        Metadata *Node = MetadataList.getMetadataFwdRef(Record[i + 1]);
        if (isa<LocalAsMetadata>(Node))
          // Drop the attachment.  This used to be legal, but there's no
          // upgrade path.
          break;
        MDNode *MD = dyn_cast_or_null<MDNode>(Node);
        if (!MD)
          return error("Invalid metadata attachment");

        if (HasSeenOldLoopTags && I->second == LLVMContext::MD_loop)
          MD = upgradeInstructionLoopAttachment(*MD);

        if (I->second == LLVMContext::MD_tbaa) {
          assert(!MD->isTemporary() && "should load MDs before attachments");
          MD = UpgradeTBAANode(*MD);
        }
        Inst->setMetadata(I->second, MD);
      }
      break;
    }
    }
  }
}

Error BitcodeReader::typeCheckLoadStoreInst(Type *ValType, Type *PtrType) {
  if (!isa<PointerType>(PtrType))
    return error("Load/Store operand is not a pointer type");
  Type *ElemType = cast<PointerType>(PtrType)->getElementType();

  if (ValType && ValType != ElemType)
    return error("Explicit load/store type does not match pointee "
                 "type of pointer operand");
  if (!PointerType::isLoadableOrStorableType(ElemType))
    return error("Cannot load/store from pointer");
  return Error::success();
}

/// Lazily parse the specified function body block.
Error BitcodeReader::parseFunctionBody(Function *F) {
  if (Stream.EnterSubBlock(bitc::FUNCTION_BLOCK_ID))
    return error("Invalid record");

  // Unexpected unresolved metadata when parsing function.
  if (MetadataList.hasFwdRefs())
    return error("Invalid function metadata: incoming forward references");

  InstructionList.clear();
  unsigned ModuleValueListSize = ValueList.size();
  unsigned ModuleMetadataListSize = MetadataList.size();

  // Add all the function arguments to the value table.
  for (Argument &I : F->args())
    ValueList.push_back(&I);

  unsigned NextValueNo = ValueList.size();
  BasicBlock *CurBB = nullptr;
  unsigned CurBBNo = 0;

  DebugLoc LastLoc;
  auto getLastInstruction = [&]() -> Instruction * {
    if (CurBB && !CurBB->empty())
      return &CurBB->back();
    else if (CurBBNo && FunctionBBs[CurBBNo - 1] &&
             !FunctionBBs[CurBBNo - 1]->empty())
      return &FunctionBBs[CurBBNo - 1]->back();
    return nullptr;
  };

  std::vector<OperandBundleDef> OperandBundles;

  // Read all the records.
  SmallVector<uint64_t, 64> Record;

  while (true) {
    BitstreamEntry Entry = Stream.advance();

    switch (Entry.Kind) {
    case BitstreamEntry::Error:
      return error("Malformed block");
    case BitstreamEntry::EndBlock:
      goto OutOfRecordLoop;

    case BitstreamEntry::SubBlock:
      switch (Entry.ID) {
      default:  // Skip unknown content.
        if (Stream.SkipBlock())
          return error("Invalid record");
        break;
      case bitc::CONSTANTS_BLOCK_ID:
        if (Error Err = parseConstants())
          return Err;
        NextValueNo = ValueList.size();
        break;
      case bitc::VALUE_SYMTAB_BLOCK_ID:
        if (Error Err = parseValueSymbolTable())
          return Err;
        break;
      case bitc::METADATA_ATTACHMENT_ID:
        if (Error Err = parseMetadataAttachment(*F))
          return Err;
        break;
      case bitc::METADATA_BLOCK_ID:
        if (Error Err = parseMetadata())
          return Err;
        break;
      case bitc::USELIST_BLOCK_ID:
        if (Error Err = parseUseLists())
          return Err;
        break;
      }
      continue;

    case BitstreamEntry::Record:
      // The interesting case.
      break;
    }

    // Read a record.
    Record.clear();
    Instruction *I = nullptr;
    unsigned BitCode = Stream.readRecord(Entry.ID, Record);
    switch (BitCode) {
    default: // Default behavior: reject
      return error("Invalid value");
    case bitc::FUNC_CODE_DECLAREBLOCKS: {   // DECLAREBLOCKS: [nblocks]
      if (Record.size() < 1 || Record[0] == 0)
        return error("Invalid record");
      // Create all the basic blocks for the function.
      FunctionBBs.resize(Record[0]);

      // See if anything took the address of blocks in this function.
      auto BBFRI = BasicBlockFwdRefs.find(F);
      if (BBFRI == BasicBlockFwdRefs.end()) {
        for (unsigned i = 0, e = FunctionBBs.size(); i != e; ++i)
          FunctionBBs[i] = BasicBlock::Create(Context, "", F);
      } else {
        auto &BBRefs = BBFRI->second;
        // Check for invalid basic block references.
        if (BBRefs.size() > FunctionBBs.size())
          return error("Invalid ID");
        assert(!BBRefs.empty() && "Unexpected empty array");
        assert(!BBRefs.front() && "Invalid reference to entry block");
        for (unsigned I = 0, E = FunctionBBs.size(), RE = BBRefs.size(); I != E;
             ++I)
          if (I < RE && BBRefs[I]) {
            BBRefs[I]->insertInto(F);
            FunctionBBs[I] = BBRefs[I];
          } else {
            FunctionBBs[I] = BasicBlock::Create(Context, "", F);
          }

        // Erase from the table.
        BasicBlockFwdRefs.erase(BBFRI);
      }

      CurBB = FunctionBBs[0];
      continue;
    }

    case bitc::FUNC_CODE_DEBUG_LOC_AGAIN:  // DEBUG_LOC_AGAIN
      // This record indicates that the last instruction is at the same
      // location as the previous instruction with a location.
      I = getLastInstruction();

      if (!I)
        return error("Invalid record");
      I->setDebugLoc(LastLoc);
      I = nullptr;
      continue;

    case bitc::FUNC_CODE_DEBUG_LOC: {      // DEBUG_LOC: [line, col, scope, ia]
      I = getLastInstruction();
      if (!I || Record.size() < 4)
        return error("Invalid record");

      unsigned Line = Record[0], Col = Record[1];
      unsigned ScopeID = Record[2], IAID = Record[3];

      MDNode *Scope = nullptr, *IA = nullptr;
      if (ScopeID) {
        Scope = MetadataList.getMDNodeFwdRefOrNull(ScopeID - 1);
        if (!Scope)
          return error("Invalid record");
      }
      if (IAID) {
        IA = MetadataList.getMDNodeFwdRefOrNull(IAID - 1);
        if (!IA)
          return error("Invalid record");
      }
      LastLoc = DebugLoc::get(Line, Col, Scope, IA);
      I->setDebugLoc(LastLoc);
      I = nullptr;
      continue;
    }

    case bitc::FUNC_CODE_INST_BINOP: {    // BINOP: [opval, ty, opval, opcode]
      unsigned OpNum = 0;
      Value *LHS, *RHS;
      if (getValueTypePair(Record, OpNum, NextValueNo, LHS) ||
          popValue(Record, OpNum, NextValueNo, LHS->getType(), RHS) ||
          OpNum+1 > Record.size())
        return error("Invalid record");

      int Opc = getDecodedBinaryOpcode(Record[OpNum++], LHS->getType());
      if (Opc == -1)
        return error("Invalid record");
      I = BinaryOperator::Create((Instruction::BinaryOps)Opc, LHS, RHS);
      InstructionList.push_back(I);
      if (OpNum < Record.size()) {
        if (Opc == Instruction::Add ||
            Opc == Instruction::Sub ||
            Opc == Instruction::Mul ||
            Opc == Instruction::Shl) {
          if (Record[OpNum] & (1 << bitc::OBO_NO_SIGNED_WRAP))
            cast<BinaryOperator>(I)->setHasNoSignedWrap(true);
          if (Record[OpNum] & (1 << bitc::OBO_NO_UNSIGNED_WRAP))
            cast<BinaryOperator>(I)->setHasNoUnsignedWrap(true);
        } else if (Opc == Instruction::SDiv ||
                   Opc == Instruction::UDiv ||
                   Opc == Instruction::LShr ||
                   Opc == Instruction::AShr) {
          if (Record[OpNum] & (1 << bitc::PEO_EXACT))
            cast<BinaryOperator>(I)->setIsExact(true);
        } else if (isa<FPMathOperator>(I)) {
          FastMathFlags FMF = getDecodedFastMathFlags(Record[OpNum]);
          if (FMF.any())
            I->setFastMathFlags(FMF);
        }

      }
      break;
    }
    case bitc::FUNC_CODE_INST_CAST: {    // CAST: [opval, opty, destty, castopc]
      unsigned OpNum = 0;
      Value *Op;
      if (getValueTypePair(Record, OpNum, NextValueNo, Op) ||
          OpNum+2 != Record.size())
        return error("Invalid record");

      Type *ResTy = getTypeByID(Record[OpNum]);
      int Opc = getDecodedCastOpcode(Record[OpNum + 1]);
      if (Opc == -1 || !ResTy)
        return error("Invalid record");
      Instruction *Temp = nullptr;
      if ((I = UpgradeBitCastInst(Opc, Op, ResTy, Temp))) {
        if (Temp) {
          InstructionList.push_back(Temp);
          CurBB->getInstList().push_back(Temp);
        }
      } else {
        auto CastOp = (Instruction::CastOps)Opc;
        if (!CastInst::castIsValid(CastOp, Op, ResTy))
          return error("Invalid cast");
        I = CastInst::Create(CastOp, Op, ResTy);
      }
      InstructionList.push_back(I);
      break;
    }
    case bitc::FUNC_CODE_INST_INBOUNDS_GEP_OLD:
    case bitc::FUNC_CODE_INST_GEP_OLD:
    case bitc::FUNC_CODE_INST_GEP: { // GEP: type, [n x operands]
      unsigned OpNum = 0;

      Type *Ty;
      bool InBounds;

      if (BitCode == bitc::FUNC_CODE_INST_GEP) {
        InBounds = Record[OpNum++];
        Ty = getTypeByID(Record[OpNum++]);
      } else {
        InBounds = BitCode == bitc::FUNC_CODE_INST_INBOUNDS_GEP_OLD;
        Ty = nullptr;
      }

      Value *BasePtr;
      if (getValueTypePair(Record, OpNum, NextValueNo, BasePtr))
        return error("Invalid record");

      if (!Ty)
        Ty = cast<PointerType>(BasePtr->getType()->getScalarType())
                 ->getElementType();
      else if (Ty !=
               cast<PointerType>(BasePtr->getType()->getScalarType())
                   ->getElementType())
        return error(
            "Explicit gep type does not match pointee type of pointer operand");

      SmallVector<Value*, 16> GEPIdx;
      while (OpNum != Record.size()) {
        Value *Op;
        if (getValueTypePair(Record, OpNum, NextValueNo, Op))
          return error("Invalid record");
        GEPIdx.push_back(Op);
      }

      I = GetElementPtrInst::Create(Ty, BasePtr, GEPIdx);

      InstructionList.push_back(I);
      if (InBounds)
        cast<GetElementPtrInst>(I)->setIsInBounds(true);
      break;
    }

    case bitc::FUNC_CODE_INST_EXTRACTVAL: {
                                       // EXTRACTVAL: [opty, opval, n x indices]
      unsigned OpNum = 0;
      Value *Agg;
      if (getValueTypePair(Record, OpNum, NextValueNo, Agg))
        return error("Invalid record");

      unsigned RecSize = Record.size();
      if (OpNum == RecSize)
        return error("EXTRACTVAL: Invalid instruction with 0 indices");

      SmallVector<unsigned, 4> EXTRACTVALIdx;
      Type *CurTy = Agg->getType();
      for (; OpNum != RecSize; ++OpNum) {
        bool IsArray = CurTy->isArrayTy();
        bool IsStruct = CurTy->isStructTy();
        uint64_t Index = Record[OpNum];

        if (!IsStruct && !IsArray)
          return error("EXTRACTVAL: Invalid type");
        if ((unsigned)Index != Index)
          return error("Invalid value");
        if (IsStruct && Index >= CurTy->subtypes().size())
          return error("EXTRACTVAL: Invalid struct index");
        if (IsArray && Index >= CurTy->getArrayNumElements())
          return error("EXTRACTVAL: Invalid array index");
        EXTRACTVALIdx.push_back((unsigned)Index);

        if (IsStruct)
          CurTy = CurTy->subtypes()[Index];
        else
          CurTy = CurTy->subtypes()[0];
      }

      I = ExtractValueInst::Create(Agg, EXTRACTVALIdx);
      InstructionList.push_back(I);
      break;
    }

    case bitc::FUNC_CODE_INST_INSERTVAL: {
                           // INSERTVAL: [opty, opval, opty, opval, n x indices]
      unsigned OpNum = 0;
      Value *Agg;
      if (getValueTypePair(Record, OpNum, NextValueNo, Agg))
        return error("Invalid record");
      Value *Val;
      if (getValueTypePair(Record, OpNum, NextValueNo, Val))
        return error("Invalid record");

      unsigned RecSize = Record.size();
      if (OpNum == RecSize)
        return error("INSERTVAL: Invalid instruction with 0 indices");

      SmallVector<unsigned, 4> INSERTVALIdx;
      Type *CurTy = Agg->getType();
      for (; OpNum != RecSize; ++OpNum) {
        bool IsArray = CurTy->isArrayTy();
        bool IsStruct = CurTy->isStructTy();
        uint64_t Index = Record[OpNum];

        if (!IsStruct && !IsArray)
          return error("INSERTVAL: Invalid type");
        if ((unsigned)Index != Index)
          return error("Invalid value");
        if (IsStruct && Index >= CurTy->subtypes().size())
          return error("INSERTVAL: Invalid struct index");
        if (IsArray && Index >= CurTy->getArrayNumElements())
          return error("INSERTVAL: Invalid array index");

        INSERTVALIdx.push_back((unsigned)Index);
        if (IsStruct)
          CurTy = CurTy->subtypes()[Index];
        else
          CurTy = CurTy->subtypes()[0];
      }

      if (CurTy != Val->getType())
        return error("Inserted value type doesn't match aggregate type");

      I = InsertValueInst::Create(Agg, Val, INSERTVALIdx);
      InstructionList.push_back(I);
      break;
    }

    case bitc::FUNC_CODE_INST_SELECT: { // SELECT: [opval, ty, opval, opval]
      // obsolete form of select
      // handles select i1 ... in old bitcode
      unsigned OpNum = 0;
      Value *TrueVal, *FalseVal, *Cond;
      if (getValueTypePair(Record, OpNum, NextValueNo, TrueVal) ||
          popValue(Record, OpNum, NextValueNo, TrueVal->getType(), FalseVal) ||
          popValue(Record, OpNum, NextValueNo, Type::getInt1Ty(Context), Cond))
        return error("Invalid record");

      I = SelectInst::Create(Cond, TrueVal, FalseVal);
      InstructionList.push_back(I);
      break;
    }

    case bitc::FUNC_CODE_INST_VSELECT: {// VSELECT: [ty,opval,opval,predty,pred]
      // new form of select
      // handles select i1 or select [N x i1]
      unsigned OpNum = 0;
      Value *TrueVal, *FalseVal, *Cond;
      if (getValueTypePair(Record, OpNum, NextValueNo, TrueVal) ||
          popValue(Record, OpNum, NextValueNo, TrueVal->getType(), FalseVal) ||
          getValueTypePair(Record, OpNum, NextValueNo, Cond))
        return error("Invalid record");

      // select condition can be either i1 or [N x i1]
      if (VectorType* vector_type =
          dyn_cast<VectorType>(Cond->getType())) {
        // expect <n x i1>
        if (vector_type->getElementType() != Type::getInt1Ty(Context))
          return error("Invalid type for value");
      } else {
        // expect i1
        if (Cond->getType() != Type::getInt1Ty(Context))
          return error("Invalid type for value");
      }

      I = SelectInst::Create(Cond, TrueVal, FalseVal);
      InstructionList.push_back(I);
      break;
    }

    case bitc::FUNC_CODE_INST_EXTRACTELT: { // EXTRACTELT: [opty, opval, opval]
      unsigned OpNum = 0;
      Value *Vec, *Idx;
      if (getValueTypePair(Record, OpNum, NextValueNo, Vec) ||
          getValueTypePair(Record, OpNum, NextValueNo, Idx))
        return error("Invalid record");
      if (!Vec->getType()->isVectorTy())
        return error("Invalid type for value");
      I = ExtractElementInst::Create(Vec, Idx);
      InstructionList.push_back(I);
      break;
    }

    case bitc::FUNC_CODE_INST_INSERTELT: { // INSERTELT: [ty, opval,opval,opval]
      unsigned OpNum = 0;
      Value *Vec, *Elt, *Idx;
      if (getValueTypePair(Record, OpNum, NextValueNo, Vec))
        return error("Invalid record");
      if (!Vec->getType()->isVectorTy())
        return error("Invalid type for value");
      if (popValue(Record, OpNum, NextValueNo,
                   cast<VectorType>(Vec->getType())->getElementType(), Elt) ||
          getValueTypePair(Record, OpNum, NextValueNo, Idx))
        return error("Invalid record");
      I = InsertElementInst::Create(Vec, Elt, Idx);
      InstructionList.push_back(I);
      break;
    }

    case bitc::FUNC_CODE_INST_SHUFFLEVEC: {// SHUFFLEVEC: [opval,ty,opval,opval]
      unsigned OpNum = 0;
      Value *Vec1, *Vec2, *Mask;
      if (getValueTypePair(Record, OpNum, NextValueNo, Vec1) ||
          popValue(Record, OpNum, NextValueNo, Vec1->getType(), Vec2))
        return error("Invalid record");

      if (getValueTypePair(Record, OpNum, NextValueNo, Mask))
        return error("Invalid record");
      if (!Vec1->getType()->isVectorTy() || !Vec2->getType()->isVectorTy())
        return error("Invalid type for value");
      I = new ShuffleVectorInst(Vec1, Vec2, Mask);
      InstructionList.push_back(I);
      break;
    }

    case bitc::FUNC_CODE_INST_CMP:   // CMP: [opty, opval, opval, pred]
      // Old form of ICmp/FCmp returning bool
      // Existed to differentiate between icmp/fcmp and vicmp/vfcmp which were
      // both legal on vectors but had different behaviour.
    case bitc::FUNC_CODE_INST_CMP2: { // CMP2: [opty, opval, opval, pred]
      // FCmp/ICmp returning bool or vector of bool

      unsigned OpNum = 0;
      Value *LHS, *RHS;
      if (getValueTypePair(Record, OpNum, NextValueNo, LHS) ||
          popValue(Record, OpNum, NextValueNo, LHS->getType(), RHS))
        return error("Invalid record");

      unsigned PredVal = Record[OpNum];
      bool IsFP = LHS->getType()->isFPOrFPVectorTy();
      FastMathFlags FMF;
      if (IsFP && Record.size() > OpNum+1)
        FMF = getDecodedFastMathFlags(Record[++OpNum]);

      if (OpNum+1 != Record.size())
        return error("Invalid record");

      if (LHS->getType()->isFPOrFPVectorTy())
        I = new FCmpInst((FCmpInst::Predicate)PredVal, LHS, RHS);
      else
        I = new ICmpInst((ICmpInst::Predicate)PredVal, LHS, RHS);

      if (FMF.any())
        I->setFastMathFlags(FMF);
      InstructionList.push_back(I);
      break;
    }

    case bitc::FUNC_CODE_INST_RET: // RET: [opty,opval<optional>]
      {
        unsigned Size = Record.size();
        if (Size == 0) {
          I = ReturnInst::Create(Context);
          InstructionList.push_back(I);
          break;
        }

        unsigned OpNum = 0;
        Value *Op = nullptr;
        if (getValueTypePair(Record, OpNum, NextValueNo, Op))
          return error("Invalid record");
        if (OpNum != Record.size())
          return error("Invalid record");

        I = ReturnInst::Create(Context, Op);
        InstructionList.push_back(I);
        break;
      }
    case bitc::FUNC_CODE_INST_BR: { // BR: [bb#, bb#, opval] or [bb#]
      if (Record.size() != 1 && Record.size() != 3)
        return error("Invalid record");
      BasicBlock *TrueDest = getBasicBlock(Record[0]);
      if (!TrueDest)
        return error("Invalid record");

      if (Record.size() == 1) {
        I = BranchInst::Create(TrueDest);
        InstructionList.push_back(I);
      }
      else {
        BasicBlock *FalseDest = getBasicBlock(Record[1]);
        Value *Cond = getValue(Record, 2, NextValueNo,
                               Type::getInt1Ty(Context));
        if (!FalseDest || !Cond)
          return error("Invalid record");
        I = BranchInst::Create(TrueDest, FalseDest, Cond);
        InstructionList.push_back(I);
      }
      break;
    }
    case bitc::FUNC_CODE_INST_CLEANUPRET: { // CLEANUPRET: [val] or [val,bb#]
      if (Record.size() != 1 && Record.size() != 2)
        return error("Invalid record");
      unsigned Idx = 0;
      Value *CleanupPad =
          getValue(Record, Idx++, NextValueNo, Type::getTokenTy(Context));
      if (!CleanupPad)
        return error("Invalid record");
      BasicBlock *UnwindDest = nullptr;
      if (Record.size() == 2) {
        UnwindDest = getBasicBlock(Record[Idx++]);
        if (!UnwindDest)
          return error("Invalid record");
      }

      I = CleanupReturnInst::Create(CleanupPad, UnwindDest);
      InstructionList.push_back(I);
      break;
    }
    case bitc::FUNC_CODE_INST_CATCHRET: { // CATCHRET: [val,bb#]
      if (Record.size() != 2)
        return error("Invalid record");
      unsigned Idx = 0;
      Value *CatchPad =
          getValue(Record, Idx++, NextValueNo, Type::getTokenTy(Context));
      if (!CatchPad)
        return error("Invalid record");
      BasicBlock *BB = getBasicBlock(Record[Idx++]);
      if (!BB)
        return error("Invalid record");

      I = CatchReturnInst::Create(CatchPad, BB);
      InstructionList.push_back(I);
      break;
    }
    case bitc::FUNC_CODE_INST_CATCHSWITCH: { // CATCHSWITCH: [tok,num,(bb)*,bb?]
      // We must have, at minimum, the outer scope and the number of arguments.
      if (Record.size() < 2)
        return error("Invalid record");

      unsigned Idx = 0;

      Value *ParentPad =
          getValue(Record, Idx++, NextValueNo, Type::getTokenTy(Context));

      unsigned NumHandlers = Record[Idx++];

      SmallVector<BasicBlock *, 2> Handlers;
      for (unsigned Op = 0; Op != NumHandlers; ++Op) {
        BasicBlock *BB = getBasicBlock(Record[Idx++]);
        if (!BB)
          return error("Invalid record");
        Handlers.push_back(BB);
      }

      BasicBlock *UnwindDest = nullptr;
      if (Idx + 1 == Record.size()) {
        UnwindDest = getBasicBlock(Record[Idx++]);
        if (!UnwindDest)
          return error("Invalid record");
      }

      if (Record.size() != Idx)
        return error("Invalid record");

      auto *CatchSwitch =
          CatchSwitchInst::Create(ParentPad, UnwindDest, NumHandlers);
      for (BasicBlock *Handler : Handlers)
        CatchSwitch->addHandler(Handler);
      I = CatchSwitch;
      InstructionList.push_back(I);
      break;
    }
    case bitc::FUNC_CODE_INST_CATCHPAD:
    case bitc::FUNC_CODE_INST_CLEANUPPAD: { // [tok,num,(ty,val)*]
      // We must have, at minimum, the outer scope and the number of arguments.
      if (Record.size() < 2)
        return error("Invalid record");

      unsigned Idx = 0;

      Value *ParentPad =
          getValue(Record, Idx++, NextValueNo, Type::getTokenTy(Context));

      unsigned NumArgOperands = Record[Idx++];

      SmallVector<Value *, 2> Args;
      for (unsigned Op = 0; Op != NumArgOperands; ++Op) {
        Value *Val;
        if (getValueTypePair(Record, Idx, NextValueNo, Val))
          return error("Invalid record");
        Args.push_back(Val);
      }

      if (Record.size() != Idx)
        return error("Invalid record");

      if (BitCode == bitc::FUNC_CODE_INST_CLEANUPPAD)
        I = CleanupPadInst::Create(ParentPad, Args);
      else
        I = CatchPadInst::Create(ParentPad, Args);
      InstructionList.push_back(I);
      break;
    }
    case bitc::FUNC_CODE_INST_SWITCH: { // SWITCH: [opty, op0, op1, ...]
      // Check magic
      if ((Record[0] >> 16) == SWITCH_INST_MAGIC) {
        // "New" SwitchInst format with case ranges. The changes to write this
        // format were reverted but we still recognize bitcode that uses it.
        // Hopefully someday we will have support for case ranges and can use
        // this format again.

        Type *OpTy = getTypeByID(Record[1]);
        unsigned ValueBitWidth = cast<IntegerType>(OpTy)->getBitWidth();

        Value *Cond = getValue(Record, 2, NextValueNo, OpTy);
        BasicBlock *Default = getBasicBlock(Record[3]);
        if (!OpTy || !Cond || !Default)
          return error("Invalid record");

        unsigned NumCases = Record[4];

        SwitchInst *SI = SwitchInst::Create(Cond, Default, NumCases);
        InstructionList.push_back(SI);

        unsigned CurIdx = 5;
        for (unsigned i = 0; i != NumCases; ++i) {
          SmallVector<ConstantInt*, 1> CaseVals;
          unsigned NumItems = Record[CurIdx++];
          for (unsigned ci = 0; ci != NumItems; ++ci) {
            bool isSingleNumber = Record[CurIdx++];

            APInt Low;
            unsigned ActiveWords = 1;
            if (ValueBitWidth > 64)
              ActiveWords = Record[CurIdx++];
            Low = readWideAPInt(makeArrayRef(&Record[CurIdx], ActiveWords),
                                ValueBitWidth);
            CurIdx += ActiveWords;

            if (!isSingleNumber) {
              ActiveWords = 1;
              if (ValueBitWidth > 64)
                ActiveWords = Record[CurIdx++];
              APInt High = readWideAPInt(
                  makeArrayRef(&Record[CurIdx], ActiveWords), ValueBitWidth);
              CurIdx += ActiveWords;

              // FIXME: It is not clear whether values in the range should be
              // compared as signed or unsigned values. The partially
              // implemented changes that used this format in the past used
              // unsigned comparisons.
              for ( ; Low.ule(High); ++Low)
                CaseVals.push_back(ConstantInt::get(Context, Low));
            } else
              CaseVals.push_back(ConstantInt::get(Context, Low));
          }
          BasicBlock *DestBB = getBasicBlock(Record[CurIdx++]);
          for (SmallVector<ConstantInt*, 1>::iterator cvi = CaseVals.begin(),
                 cve = CaseVals.end(); cvi != cve; ++cvi)
            SI->addCase(*cvi, DestBB);
        }
        I = SI;
        break;
      }

      // Old SwitchInst format without case ranges.

      if (Record.size() < 3 || (Record.size() & 1) == 0)
        return error("Invalid record");
      Type *OpTy = getTypeByID(Record[0]);
      Value *Cond = getValue(Record, 1, NextValueNo, OpTy);
      BasicBlock *Default = getBasicBlock(Record[2]);
      if (!OpTy || !Cond || !Default)
        return error("Invalid record");
      unsigned NumCases = (Record.size()-3)/2;
      SwitchInst *SI = SwitchInst::Create(Cond, Default, NumCases);
      InstructionList.push_back(SI);
      for (unsigned i = 0, e = NumCases; i != e; ++i) {
        ConstantInt *CaseVal =
          dyn_cast_or_null<ConstantInt>(getFnValueByID(Record[3+i*2], OpTy));
        BasicBlock *DestBB = getBasicBlock(Record[1+3+i*2]);
        if (!CaseVal || !DestBB) {
          delete SI;
          return error("Invalid record");
        }
        SI->addCase(CaseVal, DestBB);
      }
      I = SI;
      break;
    }
    case bitc::FUNC_CODE_INST_INDIRECTBR: { // INDIRECTBR: [opty, op0, op1, ...]
      if (Record.size() < 2)
        return error("Invalid record");
      Type *OpTy = getTypeByID(Record[0]);
      Value *Address = getValue(Record, 1, NextValueNo, OpTy);
      if (!OpTy || !Address)
        return error("Invalid record");
      unsigned NumDests = Record.size()-2;
      IndirectBrInst *IBI = IndirectBrInst::Create(Address, NumDests);
      InstructionList.push_back(IBI);
      for (unsigned i = 0, e = NumDests; i != e; ++i) {
        if (BasicBlock *DestBB = getBasicBlock(Record[2+i])) {
          IBI->addDestination(DestBB);
        } else {
          delete IBI;
          return error("Invalid record");
        }
      }
      I = IBI;
      break;
    }

    case bitc::FUNC_CODE_INST_INVOKE: {
      // INVOKE: [attrs, cc, normBB, unwindBB, fnty, op0,op1,op2, ...]
      if (Record.size() < 4)
        return error("Invalid record");
      unsigned OpNum = 0;
      AttributeSet PAL = getAttributes(Record[OpNum++]);
      unsigned CCInfo = Record[OpNum++];
      BasicBlock *NormalBB = getBasicBlock(Record[OpNum++]);
      BasicBlock *UnwindBB = getBasicBlock(Record[OpNum++]);

      FunctionType *FTy = nullptr;
      if (CCInfo >> 13 & 1 &&
          !(FTy = dyn_cast<FunctionType>(getTypeByID(Record[OpNum++]))))
        return error("Explicit invoke type is not a function type");

      Value *Callee;
      if (getValueTypePair(Record, OpNum, NextValueNo, Callee))
        return error("Invalid record");

      PointerType *CalleeTy = dyn_cast<PointerType>(Callee->getType());
      if (!CalleeTy)
        return error("Callee is not a pointer");
      if (!FTy) {
        FTy = dyn_cast<FunctionType>(CalleeTy->getElementType());
        if (!FTy)
          return error("Callee is not of pointer to function type");
      } else if (CalleeTy->getElementType() != FTy)
        return error("Explicit invoke type does not match pointee type of "
                     "callee operand");
      if (Record.size() < FTy->getNumParams() + OpNum)
        return error("Insufficient operands to call");

      SmallVector<Value*, 16> Ops;
      for (unsigned i = 0, e = FTy->getNumParams(); i != e; ++i, ++OpNum) {
        Ops.push_back(getValue(Record, OpNum, NextValueNo,
                               FTy->getParamType(i)));
        if (!Ops.back())
          return error("Invalid record");
      }

      if (!FTy->isVarArg()) {
        if (Record.size() != OpNum)
          return error("Invalid record");
      } else {
        // Read type/value pairs for varargs params.
        while (OpNum != Record.size()) {
          Value *Op;
          if (getValueTypePair(Record, OpNum, NextValueNo, Op))
            return error("Invalid record");
          Ops.push_back(Op);
        }
      }

      I = InvokeInst::Create(Callee, NormalBB, UnwindBB, Ops, OperandBundles);
      OperandBundles.clear();
      InstructionList.push_back(I);
      cast<InvokeInst>(I)->setCallingConv(
          static_cast<CallingConv::ID>(CallingConv::MaxID & CCInfo));
      cast<InvokeInst>(I)->setAttributes(PAL);
      break;
    }
    case bitc::FUNC_CODE_INST_RESUME: { // RESUME: [opval]
      unsigned Idx = 0;
      Value *Val = nullptr;
      if (getValueTypePair(Record, Idx, NextValueNo, Val))
        return error("Invalid record");
      I = ResumeInst::Create(Val);
      InstructionList.push_back(I);
      break;
    }
    case bitc::FUNC_CODE_INST_UNREACHABLE: // UNREACHABLE
      I = new UnreachableInst(Context);
      InstructionList.push_back(I);
      break;
    case bitc::FUNC_CODE_INST_PHI: { // PHI: [ty, val0,bb0, ...]
      if (Record.size() < 1 || ((Record.size()-1)&1))
        return error("Invalid record");
      Type *Ty = getTypeByID(Record[0]);
      if (!Ty)
        return error("Invalid record");

      PHINode *PN = PHINode::Create(Ty, (Record.size()-1)/2);
      InstructionList.push_back(PN);

      for (unsigned i = 0, e = Record.size()-1; i != e; i += 2) {
        Value *V;
        // With the new function encoding, it is possible that operands have
        // negative IDs (for forward references).  Use a signed VBR
        // representation to keep the encoding small.
        if (UseRelativeIDs)
          V = getValueSigned(Record, 1+i, NextValueNo, Ty);
        else
          V = getValue(Record, 1+i, NextValueNo, Ty);
        BasicBlock *BB = getBasicBlock(Record[2+i]);
        if (!V || !BB)
          return error("Invalid record");
        PN->addIncoming(V, BB);
      }
      I = PN;
      break;
    }

    case bitc::FUNC_CODE_INST_LANDINGPAD:
    case bitc::FUNC_CODE_INST_LANDINGPAD_OLD: {
      // LANDINGPAD: [ty, val, val, num, (id0,val0 ...)?]
      unsigned Idx = 0;
      if (BitCode == bitc::FUNC_CODE_INST_LANDINGPAD) {
        if (Record.size() < 3)
          return error("Invalid record");
      } else {
        assert(BitCode == bitc::FUNC_CODE_INST_LANDINGPAD_OLD);
        if (Record.size() < 4)
          return error("Invalid record");
      }
      Type *Ty = getTypeByID(Record[Idx++]);
      if (!Ty)
        return error("Invalid record");
      if (BitCode == bitc::FUNC_CODE_INST_LANDINGPAD_OLD) {
        Value *PersFn = nullptr;
        if (getValueTypePair(Record, Idx, NextValueNo, PersFn))
          return error("Invalid record");

        if (!F->hasPersonalityFn())
          F->setPersonalityFn(cast<Constant>(PersFn));
        else if (F->getPersonalityFn() != cast<Constant>(PersFn))
          return error("Personality function mismatch");
      }

      bool IsCleanup = !!Record[Idx++];
      unsigned NumClauses = Record[Idx++];
      LandingPadInst *LP = LandingPadInst::Create(Ty, NumClauses);
      LP->setCleanup(IsCleanup);
      for (unsigned J = 0; J != NumClauses; ++J) {
        LandingPadInst::ClauseType CT =
          LandingPadInst::ClauseType(Record[Idx++]); (void)CT;
        Value *Val;

        if (getValueTypePair(Record, Idx, NextValueNo, Val)) {
          delete LP;
          return error("Invalid record");
        }

        assert((CT != LandingPadInst::Catch ||
                !isa<ArrayType>(Val->getType())) &&
               "Catch clause has a invalid type!");
        assert((CT != LandingPadInst::Filter ||
                isa<ArrayType>(Val->getType())) &&
               "Filter clause has invalid type!");
        LP->addClause(cast<Constant>(Val));
      }

      I = LP;
      InstructionList.push_back(I);
      break;
    }

    case bitc::FUNC_CODE_INST_ALLOCA: { // ALLOCA: [instty, opty, op, align]
      if (Record.size() != 4)
        return error("Invalid record");
      uint64_t AlignRecord = Record[3];
      const uint64_t InAllocaMask = uint64_t(1) << 5;
      const uint64_t ExplicitTypeMask = uint64_t(1) << 6;
      const uint64_t SwiftErrorMask = uint64_t(1) << 7;
      const uint64_t FlagMask = InAllocaMask | ExplicitTypeMask |
                                SwiftErrorMask;
      bool InAlloca = AlignRecord & InAllocaMask;
      bool SwiftError = AlignRecord & SwiftErrorMask;
      Type *Ty = getTypeByID(Record[0]);
      if ((AlignRecord & ExplicitTypeMask) == 0) {
        auto *PTy = dyn_cast_or_null<PointerType>(Ty);
        if (!PTy)
          return error("Old-style alloca with a non-pointer type");
        Ty = PTy->getElementType();
      }
      Type *OpTy = getTypeByID(Record[1]);
      Value *Size = getFnValueByID(Record[2], OpTy);
      unsigned Align;
      if (Error Err = parseAlignmentValue(AlignRecord & ~FlagMask, Align)) {
        return Err;
      }
      if (!Ty || !Size)
        return error("Invalid record");
      AllocaInst *AI = new AllocaInst(Ty, Size, Align);
      AI->setUsedWithInAlloca(InAlloca);
      AI->setSwiftError(SwiftError);
      I = AI;
      InstructionList.push_back(I);
      break;
    }
    case bitc::FUNC_CODE_INST_LOAD: { // LOAD: [opty, op, align, vol]
      unsigned OpNum = 0;
      Value *Op;
      if (getValueTypePair(Record, OpNum, NextValueNo, Op) ||
          (OpNum + 2 != Record.size() && OpNum + 3 != Record.size()))
        return error("Invalid record");

      Type *Ty = nullptr;
      if (OpNum + 3 == Record.size())
        Ty = getTypeByID(Record[OpNum++]);
      if (Error Err = typeCheckLoadStoreInst(Ty, Op->getType()))
        return Err;
      if (!Ty)
        Ty = cast<PointerType>(Op->getType())->getElementType();

      unsigned Align;
      if (Error Err = parseAlignmentValue(Record[OpNum], Align))
        return Err;
      I = new LoadInst(Ty, Op, "", Record[OpNum + 1], Align);

      InstructionList.push_back(I);
      break;
    }
    case bitc::FUNC_CODE_INST_LOADATOMIC: {
       // LOADATOMIC: [opty, op, align, vol, ordering, synchscope]
      unsigned OpNum = 0;
      Value *Op;
      if (getValueTypePair(Record, OpNum, NextValueNo, Op) ||
          (OpNum + 4 != Record.size() && OpNum + 5 != Record.size()))
        return error("Invalid record");

      Type *Ty = nullptr;
      if (OpNum + 5 == Record.size())
        Ty = getTypeByID(Record[OpNum++]);
      if (Error Err = typeCheckLoadStoreInst(Ty, Op->getType()))
        return Err;
      if (!Ty)
        Ty = cast<PointerType>(Op->getType())->getElementType();

      AtomicOrdering Ordering = getDecodedOrdering(Record[OpNum + 2]);
      if (Ordering == AtomicOrdering::NotAtomic ||
          Ordering == AtomicOrdering::Release ||
          Ordering == AtomicOrdering::AcquireRelease)
        return error("Invalid record");
      if (Ordering != AtomicOrdering::NotAtomic && Record[OpNum] == 0)
        return error("Invalid record");
      SynchronizationScope SynchScope = getDecodedSynchScope(Record[OpNum + 3]);

      unsigned Align;
      if (Error Err = parseAlignmentValue(Record[OpNum], Align))
        return Err;
      I = new LoadInst(Op, "", Record[OpNum+1], Align, Ordering, SynchScope);

      InstructionList.push_back(I);
      break;
    }
    case bitc::FUNC_CODE_INST_STORE:
    case bitc::FUNC_CODE_INST_STORE_OLD: { // STORE2:[ptrty, ptr, val, align, vol]
      unsigned OpNum = 0;
      Value *Val, *Ptr;
      if (getValueTypePair(Record, OpNum, NextValueNo, Ptr) ||
          (BitCode == bitc::FUNC_CODE_INST_STORE
               ? getValueTypePair(Record, OpNum, NextValueNo, Val)
               : popValue(Record, OpNum, NextValueNo,
                          cast<PointerType>(Ptr->getType())->getElementType(),
                          Val)) ||
          OpNum + 2 != Record.size())
        return error("Invalid record");

      if (Error Err = typeCheckLoadStoreInst(Val->getType(), Ptr->getType()))
        return Err;
      unsigned Align;
      if (Error Err = parseAlignmentValue(Record[OpNum], Align))
        return Err;
      I = new StoreInst(Val, Ptr, Record[OpNum+1], Align);
      InstructionList.push_back(I);
      break;
    }
    case bitc::FUNC_CODE_INST_STOREATOMIC:
    case bitc::FUNC_CODE_INST_STOREATOMIC_OLD: {
      // STOREATOMIC: [ptrty, ptr, val, align, vol, ordering, synchscope]
      unsigned OpNum = 0;
      Value *Val, *Ptr;
      if (getValueTypePair(Record, OpNum, NextValueNo, Ptr) ||
          !isa<PointerType>(Ptr->getType()) ||
          (BitCode == bitc::FUNC_CODE_INST_STOREATOMIC
               ? getValueTypePair(Record, OpNum, NextValueNo, Val)
               : popValue(Record, OpNum, NextValueNo,
                          cast<PointerType>(Ptr->getType())->getElementType(),
                          Val)) ||
          OpNum + 4 != Record.size())
        return error("Invalid record");

      if (Error Err = typeCheckLoadStoreInst(Val->getType(), Ptr->getType()))
        return Err;
      AtomicOrdering Ordering = getDecodedOrdering(Record[OpNum + 2]);
      if (Ordering == AtomicOrdering::NotAtomic ||
          Ordering == AtomicOrdering::Acquire ||
          Ordering == AtomicOrdering::AcquireRelease)
        return error("Invalid record");
      SynchronizationScope SynchScope = getDecodedSynchScope(Record[OpNum + 3]);
      if (Ordering != AtomicOrdering::NotAtomic && Record[OpNum] == 0)
        return error("Invalid record");

      unsigned Align;
      if (Error Err = parseAlignmentValue(Record[OpNum], Align))
        return Err;
      I = new StoreInst(Val, Ptr, Record[OpNum+1], Align, Ordering, SynchScope);
      InstructionList.push_back(I);
      break;
    }
    case bitc::FUNC_CODE_INST_CMPXCHG_OLD:
    case bitc::FUNC_CODE_INST_CMPXCHG: {
      // CMPXCHG:[ptrty, ptr, cmp, new, vol, successordering, synchscope,
      //          failureordering?, isweak?]
      unsigned OpNum = 0;
      Value *Ptr, *Cmp, *New;
      if (getValueTypePair(Record, OpNum, NextValueNo, Ptr) ||
          (BitCode == bitc::FUNC_CODE_INST_CMPXCHG
               ? getValueTypePair(Record, OpNum, NextValueNo, Cmp)
               : popValue(Record, OpNum, NextValueNo,
                          cast<PointerType>(Ptr->getType())->getElementType(),
                          Cmp)) ||
          popValue(Record, OpNum, NextValueNo, Cmp->getType(), New) ||
          Record.size() < OpNum + 3 || Record.size() > OpNum + 5)
        return error("Invalid record");
      AtomicOrdering SuccessOrdering = getDecodedOrdering(Record[OpNum + 1]);
      if (SuccessOrdering == AtomicOrdering::NotAtomic ||
          SuccessOrdering == AtomicOrdering::Unordered)
        return error("Invalid record");
      SynchronizationScope SynchScope = getDecodedSynchScope(Record[OpNum + 2]);

      if (Error Err = typeCheckLoadStoreInst(Cmp->getType(), Ptr->getType()))
        return Err;
      AtomicOrdering FailureOrdering;
      if (Record.size() < 7)
        FailureOrdering =
            AtomicCmpXchgInst::getStrongestFailureOrdering(SuccessOrdering);
      else
        FailureOrdering = getDecodedOrdering(Record[OpNum + 3]);

      I = new AtomicCmpXchgInst(Ptr, Cmp, New, SuccessOrdering, FailureOrdering,
                                SynchScope);
      cast<AtomicCmpXchgInst>(I)->setVolatile(Record[OpNum]);

      if (Record.size() < 8) {
        // Before weak cmpxchgs existed, the instruction simply returned the
        // value loaded from memory, so bitcode files from that era will be
        // expecting the first component of a modern cmpxchg.
        CurBB->getInstList().push_back(I);
        I = ExtractValueInst::Create(I, 0);
      } else {
        cast<AtomicCmpXchgInst>(I)->setWeak(Record[OpNum+4]);
      }

      InstructionList.push_back(I);
      break;
    }
    case bitc::FUNC_CODE_INST_ATOMICRMW: {
      // ATOMICRMW:[ptrty, ptr, val, op, vol, ordering, synchscope]
      unsigned OpNum = 0;
      Value *Ptr, *Val;
      if (getValueTypePair(Record, OpNum, NextValueNo, Ptr) ||
          !isa<PointerType>(Ptr->getType()) ||
          popValue(Record, OpNum, NextValueNo,
                    cast<PointerType>(Ptr->getType())->getElementType(), Val) ||
          OpNum+4 != Record.size())
        return error("Invalid record");
      AtomicRMWInst::BinOp Operation = getDecodedRMWOperation(Record[OpNum]);
      if (Operation < AtomicRMWInst::FIRST_BINOP ||
          Operation > AtomicRMWInst::LAST_BINOP)
        return error("Invalid record");
      AtomicOrdering Ordering = getDecodedOrdering(Record[OpNum + 2]);
      if (Ordering == AtomicOrdering::NotAtomic ||
          Ordering == AtomicOrdering::Unordered)
        return error("Invalid record");
      SynchronizationScope SynchScope = getDecodedSynchScope(Record[OpNum + 3]);
      I = new AtomicRMWInst(Operation, Ptr, Val, Ordering, SynchScope);
      cast<AtomicRMWInst>(I)->setVolatile(Record[OpNum+1]);
      InstructionList.push_back(I);
      break;
    }
    case bitc::FUNC_CODE_INST_FENCE: { // FENCE:[ordering, synchscope]
      if (2 != Record.size())
        return error("Invalid record");
      AtomicOrdering Ordering = getDecodedOrdering(Record[0]);
      if (Ordering == AtomicOrdering::NotAtomic ||
          Ordering == AtomicOrdering::Unordered ||
          Ordering == AtomicOrdering::Monotonic)
        return error("Invalid record");
      SynchronizationScope SynchScope = getDecodedSynchScope(Record[1]);
      I = new FenceInst(Context, Ordering, SynchScope);
      InstructionList.push_back(I);
      break;
    }
    case bitc::FUNC_CODE_INST_CALL: {
      // CALL: [paramattrs, cc, fmf, fnty, fnid, arg0, arg1...]
      if (Record.size() < 3)
        return error("Invalid record");

      unsigned OpNum = 0;
      AttributeSet PAL = getAttributes(Record[OpNum++]);
      unsigned CCInfo = Record[OpNum++];

      FastMathFlags FMF;
      if ((CCInfo >> bitc::CALL_FMF) & 1) {
        FMF = getDecodedFastMathFlags(Record[OpNum++]);
        if (!FMF.any())
          return error("Fast math flags indicator set for call with no FMF");
      }

      FunctionType *FTy = nullptr;
      if (CCInfo >> bitc::CALL_EXPLICIT_TYPE & 1 &&
          !(FTy = dyn_cast<FunctionType>(getTypeByID(Record[OpNum++]))))
        return error("Explicit call type is not a function type");

      Value *Callee;
      if (getValueTypePair(Record, OpNum, NextValueNo, Callee))
        return error("Invalid record");

      PointerType *OpTy = dyn_cast<PointerType>(Callee->getType());
      if (!OpTy)
        return error("Callee is not a pointer type");
      if (!FTy) {
        FTy = dyn_cast<FunctionType>(OpTy->getElementType());
        if (!FTy)
          return error("Callee is not of pointer to function type");
      } else if (OpTy->getElementType() != FTy)
        return error("Explicit call type does not match pointee type of "
                     "callee operand");
      if (Record.size() < FTy->getNumParams() + OpNum)
        return error("Insufficient operands to call");

      SmallVector<Value*, 16> Args;
      // Read the fixed params.
      for (unsigned i = 0, e = FTy->getNumParams(); i != e; ++i, ++OpNum) {
        if (FTy->getParamType(i)->isLabelTy())
          Args.push_back(getBasicBlock(Record[OpNum]));
        else
          Args.push_back(getValue(Record, OpNum, NextValueNo,
                                  FTy->getParamType(i)));
        if (!Args.back())
          return error("Invalid record");
      }

      // Read type/value pairs for varargs params.
      if (!FTy->isVarArg()) {
        if (OpNum != Record.size())
          return error("Invalid record");
      } else {
        while (OpNum != Record.size()) {
          Value *Op;
          if (getValueTypePair(Record, OpNum, NextValueNo, Op))
            return error("Invalid record");
          Args.push_back(Op);
        }
      }

      I = CallInst::Create(FTy, Callee, Args, OperandBundles);
      OperandBundles.clear();
      InstructionList.push_back(I);
      cast<CallInst>(I)->setCallingConv(
          static_cast<CallingConv::ID>((0x7ff & CCInfo) >> bitc::CALL_CCONV));
      CallInst::TailCallKind TCK = CallInst::TCK_None;
      if (CCInfo & 1 << bitc::CALL_TAIL)
        TCK = CallInst::TCK_Tail;
      if (CCInfo & (1 << bitc::CALL_MUSTTAIL))
        TCK = CallInst::TCK_MustTail;
      if (CCInfo & (1 << bitc::CALL_NOTAIL))
        TCK = CallInst::TCK_NoTail;
      cast<CallInst>(I)->setTailCallKind(TCK);
      cast<CallInst>(I)->setAttributes(PAL);
      if (FMF.any()) {
        if (!isa<FPMathOperator>(I))
          return error("Fast-math-flags specified for call without "
                       "floating-point scalar or vector return type");
        I->setFastMathFlags(FMF);
      }
      break;
    }
    case bitc::FUNC_CODE_INST_VAARG: { // VAARG: [valistty, valist, instty]
      if (Record.size() < 3)
        return error("Invalid record");
      Type *OpTy = getTypeByID(Record[0]);
      Value *Op = getValue(Record, 1, NextValueNo, OpTy);
      Type *ResTy = getTypeByID(Record[2]);
      if (!OpTy || !Op || !ResTy)
        return error("Invalid record");
      I = new VAArgInst(Op, ResTy);
      InstructionList.push_back(I);
      break;
    }

    case bitc::FUNC_CODE_OPERAND_BUNDLE: {
      // A call or an invoke can be optionally prefixed with some variable
      // number of operand bundle blocks.  These blocks are read into
      // OperandBundles and consumed at the next call or invoke instruction.

      if (Record.size() < 1 || Record[0] >= BundleTags.size())
        return error("Invalid record");

      std::vector<Value *> Inputs;

      unsigned OpNum = 1;
      while (OpNum != Record.size()) {
        Value *Op;
        if (getValueTypePair(Record, OpNum, NextValueNo, Op))
          return error("Invalid record");
        Inputs.push_back(Op);
      }

      OperandBundles.emplace_back(BundleTags[Record[0]], std::move(Inputs));
      continue;
    }
    }

    // Add instruction to end of current BB.  If there is no current BB, reject
    // this file.
    if (!CurBB) {
      delete I;
      return error("Invalid instruction with no BB");
    }
    if (!OperandBundles.empty()) {
      delete I;
      return error("Operand bundles found with no consumer");
    }
    CurBB->getInstList().push_back(I);

    // If this was a terminator instruction, move to the next block.
    if (isa<TerminatorInst>(I)) {
      ++CurBBNo;
      CurBB = CurBBNo < FunctionBBs.size() ? FunctionBBs[CurBBNo] : nullptr;
    }

    // Non-void values get registered in the value table for future use.
    if (I && !I->getType()->isVoidTy())
      ValueList.assignValue(I, NextValueNo++);
  }

OutOfRecordLoop:

  if (!OperandBundles.empty())
    return error("Operand bundles found with no consumer");

  // Check the function list for unresolved values.
  if (Argument *A = dyn_cast<Argument>(ValueList.back())) {
    if (!A->getParent()) {
      // We found at least one unresolved value.  Nuke them all to avoid leaks.
      for (unsigned i = ModuleValueListSize, e = ValueList.size(); i != e; ++i){
        if ((A = dyn_cast_or_null<Argument>(ValueList[i])) && !A->getParent()) {
          A->replaceAllUsesWith(UndefValue::get(A->getType()));
          delete A;
        }
      }
      return error("Never resolved value found in function");
    }
  }

  // Unexpected unresolved metadata about to be dropped.
  if (MetadataList.hasFwdRefs())
    return error("Invalid function metadata: outgoing forward refs");

  // Trim the value list down to the size it was before we parsed this function.
  ValueList.shrinkTo(ModuleValueListSize);
  MetadataList.shrinkTo(ModuleMetadataListSize);
  std::vector<BasicBlock*>().swap(FunctionBBs);
  return Error::success();
}

/// Find the function body in the bitcode stream
Error BitcodeReader::findFunctionInStream(
    Function *F,
    DenseMap<Function *, uint64_t>::iterator DeferredFunctionInfoIterator) {
  while (DeferredFunctionInfoIterator->second == 0) {
    // This is the fallback handling for the old format bitcode that
    // didn't contain the function index in the VST, or when we have
    // an anonymous function which would not have a VST entry.
    // Assert that we have one of those two cases.
    assert(VSTOffset == 0 || !F->hasName());
    // Parse the next body in the stream and set its position in the
    // DeferredFunctionInfo map.
    if (Error Err = rememberAndSkipFunctionBodies())
      return Err;
  }
  return Error::success();
}

//===----------------------------------------------------------------------===//
// GVMaterializer implementation
//===----------------------------------------------------------------------===//

Error BitcodeReader::materialize(GlobalValue *GV) {
  Function *F = dyn_cast<Function>(GV);
  // If it's not a function or is already material, ignore the request.
  if (!F || !F->isMaterializable())
    return Error::success();

  DenseMap<Function*, uint64_t>::iterator DFII = DeferredFunctionInfo.find(F);
  assert(DFII != DeferredFunctionInfo.end() && "Deferred function not found!");
  // If its position is recorded as 0, its body is somewhere in the stream
  // but we haven't seen it yet.
  if (DFII->second == 0)
    if (Error Err = findFunctionInStream(F, DFII))
      return Err;

  // Materialize metadata before parsing any function bodies.
  if (Error Err = materializeMetadata())
    return Err;

  // Move the bit stream to the saved position of the deferred function body.
  Stream.JumpToBit(DFII->second);

  if (Error Err = parseFunctionBody(F))
    return Err;
  F->setIsMaterializable(false);

  if (StripDebugInfo)
    stripDebugInfo(*F);

  // Upgrade any old intrinsic calls in the function.
  for (auto &I : UpgradedIntrinsics) {
    for (auto UI = I.first->materialized_user_begin(), UE = I.first->user_end();
         UI != UE;) {
      User *U = *UI;
      ++UI;
      if (CallInst *CI = dyn_cast<CallInst>(U))
        UpgradeIntrinsicCall(CI, I.second);
    }
  }

  // Update calls to the remangled intrinsics
  for (auto &I : RemangledIntrinsics)
    for (auto UI = I.first->materialized_user_begin(), UE = I.first->user_end();
         UI != UE;)
      // Don't expect any other users than call sites
      CallSite(*UI++).setCalledFunction(I.second);

  // Finish fn->subprogram upgrade for materialized functions.
  if (DISubprogram *SP = FunctionsWithSPs.lookup(F))
    F->setSubprogram(SP);

  // Bring in any functions that this function forward-referenced via
  // blockaddresses.
  return materializeForwardReferencedFunctions();
}

Error BitcodeReader::materializeModule() {
  if (Error Err = materializeMetadata())
    return Err;

  // Promise to materialize all forward references.
  WillMaterializeAllForwardRefs = true;

  // Iterate over the module, deserializing any functions that are still on
  // disk.
  for (Function &F : *TheModule) {
    if (Error Err = materialize(&F))
      return Err;
  }
  // At this point, if there are any function bodies, parse the rest of
  // the bits in the module past the last function block we have recorded
  // through either lazy scanning or the VST.
  if (LastFunctionBlockBit || NextUnreadBit)
    if (Error Err = parseModule(LastFunctionBlockBit > NextUnreadBit
                                    ? LastFunctionBlockBit
                                    : NextUnreadBit))
      return Err;

  // Check that all block address forward references got resolved (as we
  // promised above).
  if (!BasicBlockFwdRefs.empty())
    return error("Never resolved function from blockaddress");

  // Upgrade any intrinsic calls that slipped through (should not happen!) and
  // delete the old functions to clean up. We can't do this unless the entire
  // module is materialized because there could always be another function body
  // with calls to the old function.
  for (auto &I : UpgradedIntrinsics) {
    for (auto *U : I.first->users()) {
      if (CallInst *CI = dyn_cast<CallInst>(U))
        UpgradeIntrinsicCall(CI, I.second);
    }
    if (!I.first->use_empty())
      I.first->replaceAllUsesWith(I.second);
    I.first->eraseFromParent();
  }
  UpgradedIntrinsics.clear();
  // Do the same for remangled intrinsics
  for (auto &I : RemangledIntrinsics) {
    I.first->replaceAllUsesWith(I.second);
    I.first->eraseFromParent();
  }
  RemangledIntrinsics.clear();

  UpgradeDebugInfo(*TheModule);

  UpgradeModuleFlags(*TheModule);
  return Error::success();
}

std::vector<StructType *> BitcodeReader::getIdentifiedStructTypes() const {
  return IdentifiedStructTypes;
}

ModuleSummaryIndexBitcodeReader::ModuleSummaryIndexBitcodeReader(
    BitstreamCursor Cursor, ModuleSummaryIndex &TheIndex)
    : BitcodeReaderBase(std::move(Cursor)), TheIndex(TheIndex) {}

std::pair<GlobalValue::GUID, GlobalValue::GUID>
ModuleSummaryIndexBitcodeReader::getGUIDFromValueId(unsigned ValueId) {
  auto VGI = ValueIdToCallGraphGUIDMap.find(ValueId);
  assert(VGI != ValueIdToCallGraphGUIDMap.end());
  return VGI->second;
}

// Specialized value symbol table parser used when reading module index
// blocks where we don't actually create global values. The parsed information
// is saved in the bitcode reader for use when later parsing summaries.
Error ModuleSummaryIndexBitcodeReader::parseValueSymbolTable(
    uint64_t Offset,
    DenseMap<unsigned, GlobalValue::LinkageTypes> &ValueIdToLinkageMap) {
  assert(Offset > 0 && "Expected non-zero VST offset");
  uint64_t CurrentBit = jumpToValueSymbolTable(Offset, Stream);

  if (Stream.EnterSubBlock(bitc::VALUE_SYMTAB_BLOCK_ID))
    return error("Invalid record");

  SmallVector<uint64_t, 64> Record;

  // Read all the records for this value table.
  SmallString<128> ValueName;

  while (true) {
    BitstreamEntry Entry = Stream.advanceSkippingSubblocks();

    switch (Entry.Kind) {
    case BitstreamEntry::SubBlock: // Handled for us already.
    case BitstreamEntry::Error:
      return error("Malformed block");
    case BitstreamEntry::EndBlock:
      // Done parsing VST, jump back to wherever we came from.
      Stream.JumpToBit(CurrentBit);
      return Error::success();
    case BitstreamEntry::Record:
      // The interesting case.
      break;
    }

    // Read a record.
    Record.clear();
    switch (Stream.readRecord(Entry.ID, Record)) {
    default: // Default behavior: ignore (e.g. VST_CODE_BBENTRY records).
      break;
    case bitc::VST_CODE_ENTRY: { // VST_CODE_ENTRY: [valueid, namechar x N]
      if (convertToString(Record, 1, ValueName))
        return error("Invalid record");
      unsigned ValueID = Record[0];
      assert(!SourceFileName.empty());
      auto VLI = ValueIdToLinkageMap.find(ValueID);
      assert(VLI != ValueIdToLinkageMap.end() &&
             "No linkage found for VST entry?");
      auto Linkage = VLI->second;
      std::string GlobalId =
          GlobalValue::getGlobalIdentifier(ValueName, Linkage, SourceFileName);
      auto ValueGUID = GlobalValue::getGUID(GlobalId);
      auto OriginalNameID = ValueGUID;
      if (GlobalValue::isLocalLinkage(Linkage))
        OriginalNameID = GlobalValue::getGUID(ValueName);
      if (PrintSummaryGUIDs)
        dbgs() << "GUID " << ValueGUID << "(" << OriginalNameID << ") is "
               << ValueName << "\n";
      ValueIdToCallGraphGUIDMap[ValueID] =
          std::make_pair(ValueGUID, OriginalNameID);
      ValueName.clear();
      break;
    }
    case bitc::VST_CODE_FNENTRY: {
      // VST_CODE_FNENTRY: [valueid, offset, namechar x N]
      if (convertToString(Record, 2, ValueName))
        return error("Invalid record");
      unsigned ValueID = Record[0];
      assert(!SourceFileName.empty());
      auto VLI = ValueIdToLinkageMap.find(ValueID);
      assert(VLI != ValueIdToLinkageMap.end() &&
             "No linkage found for VST entry?");
      auto Linkage = VLI->second;
      std::string FunctionGlobalId = GlobalValue::getGlobalIdentifier(
          ValueName, VLI->second, SourceFileName);
      auto FunctionGUID = GlobalValue::getGUID(FunctionGlobalId);
      auto OriginalNameID = FunctionGUID;
      if (GlobalValue::isLocalLinkage(Linkage))
        OriginalNameID = GlobalValue::getGUID(ValueName);
      if (PrintSummaryGUIDs)
        dbgs() << "GUID " << FunctionGUID << "(" << OriginalNameID << ") is "
               << ValueName << "\n";
      ValueIdToCallGraphGUIDMap[ValueID] =
          std::make_pair(FunctionGUID, OriginalNameID);

      ValueName.clear();
      break;
    }
    case bitc::VST_CODE_COMBINED_ENTRY: {
      // VST_CODE_COMBINED_ENTRY: [valueid, refguid]
      unsigned ValueID = Record[0];
      GlobalValue::GUID RefGUID = Record[1];
      // The "original name", which is the second value of the pair will be
      // overriden later by a FS_COMBINED_ORIGINAL_NAME in the combined index.
      ValueIdToCallGraphGUIDMap[ValueID] = std::make_pair(RefGUID, RefGUID);
      break;
    }
    }
  }
}

// Parse just the blocks needed for building the index out of the module.
// At the end of this routine the module Index is populated with a map
// from global value id to GlobalValueSummary objects.
Error ModuleSummaryIndexBitcodeReader::parseModule(StringRef ModulePath) {
  if (Stream.EnterSubBlock(bitc::MODULE_BLOCK_ID))
    return error("Invalid record");

  SmallVector<uint64_t, 64> Record;
  DenseMap<unsigned, GlobalValue::LinkageTypes> ValueIdToLinkageMap;
  unsigned ValueId = 0;

  // Read the index for this module.
  while (true) {
    BitstreamEntry Entry = Stream.advance();

    switch (Entry.Kind) {
    case BitstreamEntry::Error:
      return error("Malformed block");
    case BitstreamEntry::EndBlock:
      return Error::success();

    case BitstreamEntry::SubBlock:
      switch (Entry.ID) {
      default: // Skip unknown content.
        if (Stream.SkipBlock())
          return error("Invalid record");
        break;
      case bitc::BLOCKINFO_BLOCK_ID:
        // Need to parse these to get abbrev ids (e.g. for VST)
        if (readBlockInfo())
          return error("Malformed block");
        break;
      case bitc::VALUE_SYMTAB_BLOCK_ID:
        // Should have been parsed earlier via VSTOffset, unless there
        // is no summary section.
        assert(((SeenValueSymbolTable && VSTOffset > 0) ||
                !SeenGlobalValSummary) &&
               "Expected early VST parse via VSTOffset record");
        if (Stream.SkipBlock())
          return error("Invalid record");
        break;
      case bitc::GLOBALVAL_SUMMARY_BLOCK_ID:
        assert(!SeenValueSymbolTable &&
               "Already read VST when parsing summary block?");
        // We might not have a VST if there were no values in the
        // summary. An empty summary block generated when we are
        // performing ThinLTO compiles so we don't later invoke
        // the regular LTO process on them.
        if (VSTOffset > 0) {
          if (Error Err = parseValueSymbolTable(VSTOffset, ValueIdToLinkageMap))
            return Err;
          SeenValueSymbolTable = true;
        }
        SeenGlobalValSummary = true;
        if (Error Err = parseEntireSummary(ModulePath))
          return Err;
        break;
      case bitc::MODULE_STRTAB_BLOCK_ID:
        if (Error Err = parseModuleStringTable())
          return Err;
        break;
      }
      continue;

    case BitstreamEntry::Record: {
        Record.clear();
        auto BitCode = Stream.readRecord(Entry.ID, Record);
        switch (BitCode) {
        default:
          break; // Default behavior, ignore unknown content.
        /// MODULE_CODE_SOURCE_FILENAME: [namechar x N]
        case bitc::MODULE_CODE_SOURCE_FILENAME: {
          SmallString<128> ValueName;
          if (convertToString(Record, 0, ValueName))
            return error("Invalid record");
          SourceFileName = ValueName.c_str();
          break;
        }
        /// MODULE_CODE_HASH: [5*i32]
        case bitc::MODULE_CODE_HASH: {
          if (Record.size() != 5)
            return error("Invalid hash length " + Twine(Record.size()).str());
          if (TheIndex.modulePaths().empty())
            // We always seed the index with the module.
            TheIndex.addModulePath(ModulePath, 0);
          if (TheIndex.modulePaths().size() != 1)
            return error("Don't expect multiple modules defined?");
          auto &Hash = TheIndex.modulePaths().begin()->second.second;
          int Pos = 0;
          for (auto &Val : Record) {
            assert(!(Val >> 32) && "Unexpected high bits set");
            Hash[Pos++] = Val;
          }
          break;
        }
        /// MODULE_CODE_VSTOFFSET: [offset]
        case bitc::MODULE_CODE_VSTOFFSET:
          if (Record.size() < 1)
            return error("Invalid record");
          // Note that we subtract 1 here because the offset is relative to one
          // word before the start of the identification or module block, which
          // was historically always the start of the regular bitcode header.
          VSTOffset = Record[0] - 1;
          break;
        // GLOBALVAR: [pointer type, isconst, initid,
        //             linkage, alignment, section, visibility, threadlocal,
        //             unnamed_addr, externally_initialized, dllstorageclass,
        //             comdat]
        case bitc::MODULE_CODE_GLOBALVAR: {
          if (Record.size() < 6)
            return error("Invalid record");
          uint64_t RawLinkage = Record[3];
          GlobalValue::LinkageTypes Linkage = getDecodedLinkage(RawLinkage);
          ValueIdToLinkageMap[ValueId++] = Linkage;
          break;
        }
        // FUNCTION:  [type, callingconv, isproto, linkage, paramattr,
        //             alignment, section, visibility, gc, unnamed_addr,
        //             prologuedata, dllstorageclass, comdat, prefixdata]
        case bitc::MODULE_CODE_FUNCTION: {
          if (Record.size() < 8)
            return error("Invalid record");
          uint64_t RawLinkage = Record[3];
          GlobalValue::LinkageTypes Linkage = getDecodedLinkage(RawLinkage);
          ValueIdToLinkageMap[ValueId++] = Linkage;
          break;
        }
        // ALIAS: [alias type, addrspace, aliasee val#, linkage, visibility,
        // dllstorageclass]
        case bitc::MODULE_CODE_ALIAS: {
          if (Record.size() < 6)
            return error("Invalid record");
          uint64_t RawLinkage = Record[3];
          GlobalValue::LinkageTypes Linkage = getDecodedLinkage(RawLinkage);
          ValueIdToLinkageMap[ValueId++] = Linkage;
          break;
        }
        }
      }
      continue;
    }
  }
}

// Eagerly parse the entire summary block. This populates the GlobalValueSummary
// objects in the index.
Error ModuleSummaryIndexBitcodeReader::parseEntireSummary(
    StringRef ModulePath) {
  if (Stream.EnterSubBlock(bitc::GLOBALVAL_SUMMARY_BLOCK_ID))
    return error("Invalid record");
  SmallVector<uint64_t, 64> Record;

  // Parse version
  {
    BitstreamEntry Entry = Stream.advanceSkippingSubblocks();
    if (Entry.Kind != BitstreamEntry::Record)
      return error("Invalid Summary Block: record for version expected");
    if (Stream.readRecord(Entry.ID, Record) != bitc::FS_VERSION)
      return error("Invalid Summary Block: version expected");
  }
  const uint64_t Version = Record[0];
  const bool IsOldProfileFormat = Version == 1;
  if (!IsOldProfileFormat && Version != 2)
    return error("Invalid summary version " + Twine(Version) +
                 ", 1 or 2 expected");
  Record.clear();

  // Keep around the last seen summary to be used when we see an optional
  // "OriginalName" attachement.
  GlobalValueSummary *LastSeenSummary = nullptr;
  bool Combined = false;

  while (true) {
    BitstreamEntry Entry = Stream.advanceSkippingSubblocks();

    switch (Entry.Kind) {
    case BitstreamEntry::SubBlock: // Handled for us already.
    case BitstreamEntry::Error:
      return error("Malformed block");
    case BitstreamEntry::EndBlock:
      // For a per-module index, remove any entries that still have empty
      // summaries. The VST parsing creates entries eagerly for all symbols,
      // but not all have associated summaries (e.g. it doesn't know how to
      // distinguish between VST_CODE_ENTRY for function declarations vs global
      // variables with initializers that end up with a summary). Remove those
      // entries now so that we don't need to rely on the combined index merger
      // to clean them up (especially since that may not run for the first
      // module's index if we merge into that).
      if (!Combined)
        TheIndex.removeEmptySummaryEntries();
      return Error::success();
    case BitstreamEntry::Record:
      // The interesting case.
      break;
    }

    // Read a record. The record format depends on whether this
    // is a per-module index or a combined index file. In the per-module
    // case the records contain the associated value's ID for correlation
    // with VST entries. In the combined index the correlation is done
    // via the bitcode offset of the summary records (which were saved
    // in the combined index VST entries). The records also contain
    // information used for ThinLTO renaming and importing.
    Record.clear();
    auto BitCode = Stream.readRecord(Entry.ID, Record);
    switch (BitCode) {
    default: // Default behavior: ignore.
      break;
    // FS_PERMODULE: [valueid, flags, instcount, numrefs, numrefs x valueid,
    //                n x (valueid)]
    // FS_PERMODULE_PROFILE: [valueid, flags, instcount, numrefs,
    //                        numrefs x valueid,
    //                        n x (valueid, hotness)]
    case bitc::FS_PERMODULE:
    case bitc::FS_PERMODULE_PROFILE: {
      unsigned ValueID = Record[0];
      uint64_t RawFlags = Record[1];
      unsigned InstCount = Record[2];
      unsigned NumRefs = Record[3];
      auto Flags = getDecodedGVSummaryFlags(RawFlags, Version);
      std::unique_ptr<FunctionSummary> FS =
          llvm::make_unique<FunctionSummary>(Flags, InstCount);
      // The module path string ref set in the summary must be owned by the
      // index's module string table. Since we don't have a module path
      // string table section in the per-module index, we create a single
      // module path string table entry with an empty (0) ID to take
      // ownership.
      FS->setModulePath(TheIndex.addModulePath(ModulePath, 0)->first());
      static int RefListStartIndex = 4;
      int CallGraphEdgeStartIndex = RefListStartIndex + NumRefs;
      assert(Record.size() >= RefListStartIndex + NumRefs &&
             "Record size inconsistent with number of references");
      for (unsigned I = 4, E = CallGraphEdgeStartIndex; I != E; ++I) {
        unsigned RefValueId = Record[I];
        GlobalValue::GUID RefGUID = getGUIDFromValueId(RefValueId).first;
        FS->addRefEdge(RefGUID);
      }
      bool HasProfile = (BitCode == bitc::FS_PERMODULE_PROFILE);
      for (unsigned I = CallGraphEdgeStartIndex, E = Record.size(); I != E;
           ++I) {
        CalleeInfo::HotnessType Hotness;
        GlobalValue::GUID CalleeGUID;
        std::tie(CalleeGUID, Hotness) =
            readCallGraphEdge(Record, I, IsOldProfileFormat, HasProfile);
        FS->addCallGraphEdge(CalleeGUID, CalleeInfo(Hotness));
      }
      auto GUID = getGUIDFromValueId(ValueID);
      FS->setOriginalName(GUID.second);
      TheIndex.addGlobalValueSummary(GUID.first, std::move(FS));
      break;
    }
    // FS_ALIAS: [valueid, flags, valueid]
    // Aliases must be emitted (and parsed) after all FS_PERMODULE entries, as
    // they expect all aliasee summaries to be available.
    case bitc::FS_ALIAS: {
      unsigned ValueID = Record[0];
      uint64_t RawFlags = Record[1];
      unsigned AliaseeID = Record[2];
      auto Flags = getDecodedGVSummaryFlags(RawFlags, Version);
      std::unique_ptr<AliasSummary> AS = llvm::make_unique<AliasSummary>(Flags);
      // The module path string ref set in the summary must be owned by the
      // index's module string table. Since we don't have a module path
      // string table section in the per-module index, we create a single
      // module path string table entry with an empty (0) ID to take
      // ownership.
      AS->setModulePath(TheIndex.addModulePath(ModulePath, 0)->first());

      GlobalValue::GUID AliaseeGUID = getGUIDFromValueId(AliaseeID).first;
      auto *AliaseeSummary = TheIndex.getGlobalValueSummary(AliaseeGUID);
      if (!AliaseeSummary)
        return error("Alias expects aliasee summary to be parsed");
      AS->setAliasee(AliaseeSummary);

      auto GUID = getGUIDFromValueId(ValueID);
      AS->setOriginalName(GUID.second);
      TheIndex.addGlobalValueSummary(GUID.first, std::move(AS));
      break;
    }
    // FS_PERMODULE_GLOBALVAR_INIT_REFS: [valueid, flags, n x valueid]
    case bitc::FS_PERMODULE_GLOBALVAR_INIT_REFS: {
      unsigned ValueID = Record[0];
      uint64_t RawFlags = Record[1];
      auto Flags = getDecodedGVSummaryFlags(RawFlags, Version);
      std::unique_ptr<GlobalVarSummary> FS =
          llvm::make_unique<GlobalVarSummary>(Flags);
      FS->setModulePath(TheIndex.addModulePath(ModulePath, 0)->first());
      for (unsigned I = 2, E = Record.size(); I != E; ++I) {
        unsigned RefValueId = Record[I];
        GlobalValue::GUID RefGUID = getGUIDFromValueId(RefValueId).first;
        FS->addRefEdge(RefGUID);
      }
      auto GUID = getGUIDFromValueId(ValueID);
      FS->setOriginalName(GUID.second);
      TheIndex.addGlobalValueSummary(GUID.first, std::move(FS));
      break;
    }
    // FS_COMBINED: [valueid, modid, flags, instcount, numrefs,
    //               numrefs x valueid, n x (valueid)]
    // FS_COMBINED_PROFILE: [valueid, modid, flags, instcount, numrefs,
    //                       numrefs x valueid, n x (valueid, hotness)]
    case bitc::FS_COMBINED:
    case bitc::FS_COMBINED_PROFILE: {
      unsigned ValueID = Record[0];
      uint64_t ModuleId = Record[1];
      uint64_t RawFlags = Record[2];
      unsigned InstCount = Record[3];
      unsigned NumRefs = Record[4];
      auto Flags = getDecodedGVSummaryFlags(RawFlags, Version);
      std::unique_ptr<FunctionSummary> FS =
          llvm::make_unique<FunctionSummary>(Flags, InstCount);
      LastSeenSummary = FS.get();
      FS->setModulePath(ModuleIdMap[ModuleId]);
      static int RefListStartIndex = 5;
      int CallGraphEdgeStartIndex = RefListStartIndex + NumRefs;
      assert(Record.size() >= RefListStartIndex + NumRefs &&
             "Record size inconsistent with number of references");
      for (unsigned I = RefListStartIndex, E = CallGraphEdgeStartIndex; I != E;
           ++I) {
        unsigned RefValueId = Record[I];
        GlobalValue::GUID RefGUID = getGUIDFromValueId(RefValueId).first;
        FS->addRefEdge(RefGUID);
      }
      bool HasProfile = (BitCode == bitc::FS_COMBINED_PROFILE);
      for (unsigned I = CallGraphEdgeStartIndex, E = Record.size(); I != E;
           ++I) {
        CalleeInfo::HotnessType Hotness;
        GlobalValue::GUID CalleeGUID;
        std::tie(CalleeGUID, Hotness) =
            readCallGraphEdge(Record, I, IsOldProfileFormat, HasProfile);
        FS->addCallGraphEdge(CalleeGUID, CalleeInfo(Hotness));
      }
      GlobalValue::GUID GUID = getGUIDFromValueId(ValueID).first;
      TheIndex.addGlobalValueSummary(GUID, std::move(FS));
      Combined = true;
      break;
    }
    // FS_COMBINED_ALIAS: [valueid, modid, flags, valueid]
    // Aliases must be emitted (and parsed) after all FS_COMBINED entries, as
    // they expect all aliasee summaries to be available.
    case bitc::FS_COMBINED_ALIAS: {
      unsigned ValueID = Record[0];
      uint64_t ModuleId = Record[1];
      uint64_t RawFlags = Record[2];
      unsigned AliaseeValueId = Record[3];
      auto Flags = getDecodedGVSummaryFlags(RawFlags, Version);
      std::unique_ptr<AliasSummary> AS = llvm::make_unique<AliasSummary>(Flags);
      LastSeenSummary = AS.get();
      AS->setModulePath(ModuleIdMap[ModuleId]);

      auto AliaseeGUID = getGUIDFromValueId(AliaseeValueId).first;
      auto AliaseeInModule =
          TheIndex.findSummaryInModule(AliaseeGUID, AS->modulePath());
      if (!AliaseeInModule)
        return error("Alias expects aliasee summary to be parsed");
      AS->setAliasee(AliaseeInModule);

      GlobalValue::GUID GUID = getGUIDFromValueId(ValueID).first;
      TheIndex.addGlobalValueSummary(GUID, std::move(AS));
      Combined = true;
      break;
    }
    // FS_COMBINED_GLOBALVAR_INIT_REFS: [valueid, modid, flags, n x valueid]
    case bitc::FS_COMBINED_GLOBALVAR_INIT_REFS: {
      unsigned ValueID = Record[0];
      uint64_t ModuleId = Record[1];
      uint64_t RawFlags = Record[2];
      auto Flags = getDecodedGVSummaryFlags(RawFlags, Version);
      std::unique_ptr<GlobalVarSummary> FS =
          llvm::make_unique<GlobalVarSummary>(Flags);
      LastSeenSummary = FS.get();
      FS->setModulePath(ModuleIdMap[ModuleId]);
      for (unsigned I = 3, E = Record.size(); I != E; ++I) {
        unsigned RefValueId = Record[I];
        GlobalValue::GUID RefGUID = getGUIDFromValueId(RefValueId).first;
        FS->addRefEdge(RefGUID);
      }
      GlobalValue::GUID GUID = getGUIDFromValueId(ValueID).first;
      TheIndex.addGlobalValueSummary(GUID, std::move(FS));
      Combined = true;
      break;
    }
    // FS_COMBINED_ORIGINAL_NAME: [original_name]
    case bitc::FS_COMBINED_ORIGINAL_NAME: {
      uint64_t OriginalName = Record[0];
      if (!LastSeenSummary)
        return error("Name attachment that does not follow a combined record");
      LastSeenSummary->setOriginalName(OriginalName);
      // Reset the LastSeenSummary
      LastSeenSummary = nullptr;
    }
    }
  }
  llvm_unreachable("Exit infinite loop");
}

std::pair<GlobalValue::GUID, CalleeInfo::HotnessType>
ModuleSummaryIndexBitcodeReader::readCallGraphEdge(
    const SmallVector<uint64_t, 64> &Record, unsigned int &I,
    const bool IsOldProfileFormat, const bool HasProfile) {

  auto Hotness = CalleeInfo::HotnessType::Unknown;
  unsigned CalleeValueId = Record[I];
  GlobalValue::GUID CalleeGUID = getGUIDFromValueId(CalleeValueId).first;
  if (IsOldProfileFormat) {
    I += 1; // Skip old callsitecount field
    if (HasProfile)
      I += 1; // Skip old profilecount field
  } else if (HasProfile)
    Hotness = static_cast<CalleeInfo::HotnessType>(Record[++I]);
  return {CalleeGUID, Hotness};
}

// Parse the  module string table block into the Index.
// This populates the ModulePathStringTable map in the index.
Error ModuleSummaryIndexBitcodeReader::parseModuleStringTable() {
  if (Stream.EnterSubBlock(bitc::MODULE_STRTAB_BLOCK_ID))
    return error("Invalid record");

  SmallVector<uint64_t, 64> Record;

  SmallString<128> ModulePath;
  ModulePathStringTableTy::iterator LastSeenModulePath;

  while (true) {
    BitstreamEntry Entry = Stream.advanceSkippingSubblocks();

    switch (Entry.Kind) {
    case BitstreamEntry::SubBlock: // Handled for us already.
    case BitstreamEntry::Error:
      return error("Malformed block");
    case BitstreamEntry::EndBlock:
      return Error::success();
    case BitstreamEntry::Record:
      // The interesting case.
      break;
    }

    Record.clear();
    switch (Stream.readRecord(Entry.ID, Record)) {
    default: // Default behavior: ignore.
      break;
    case bitc::MST_CODE_ENTRY: {
      // MST_ENTRY: [modid, namechar x N]
      uint64_t ModuleId = Record[0];

      if (convertToString(Record, 1, ModulePath))
        return error("Invalid record");

      LastSeenModulePath = TheIndex.addModulePath(ModulePath, ModuleId);
      ModuleIdMap[ModuleId] = LastSeenModulePath->first();

      ModulePath.clear();
      break;
    }
    /// MST_CODE_HASH: [5*i32]
    case bitc::MST_CODE_HASH: {
      if (Record.size() != 5)
        return error("Invalid hash length " + Twine(Record.size()).str());
      if (LastSeenModulePath == TheIndex.modulePaths().end())
        return error("Invalid hash that does not follow a module path");
      int Pos = 0;
      for (auto &Val : Record) {
        assert(!(Val >> 32) && "Unexpected high bits set");
        LastSeenModulePath->second.second[Pos++] = Val;
      }
      // Reset LastSeenModulePath to avoid overriding the hash unexpectedly.
      LastSeenModulePath = TheIndex.modulePaths().end();
      break;
    }
    }
  }
  llvm_unreachable("Exit infinite loop");
}

namespace {

// FIXME: This class is only here to support the transition to llvm::Error. It
// will be removed once this transition is complete. Clients should prefer to
// deal with the Error value directly, rather than converting to error_code.
class BitcodeErrorCategoryType : public std::error_category {
  const char *name() const noexcept override {
    return "llvm.bitcode";
  }
  std::string message(int IE) const override {
    BitcodeError E = static_cast<BitcodeError>(IE);
    switch (E) {
    case BitcodeError::CorruptedBitcode:
      return "Corrupted bitcode";
    }
    llvm_unreachable("Unknown error type!");
  }
};

} // end anonymous namespace

static ManagedStatic<BitcodeErrorCategoryType> ErrorCategory;

const std::error_category &llvm::BitcodeErrorCategory() {
  return *ErrorCategory;
}

//===----------------------------------------------------------------------===//
// External interface
//===----------------------------------------------------------------------===//

Expected<std::vector<BitcodeModule>>
llvm::getBitcodeModuleList(MemoryBufferRef Buffer) {
  Expected<BitstreamCursor> StreamOrErr = initStream(Buffer);
  if (!StreamOrErr)
    return StreamOrErr.takeError();
  BitstreamCursor &Stream = *StreamOrErr;

  std::vector<BitcodeModule> Modules;
  while (true) {
    uint64_t BCBegin = Stream.getCurrentByteNo();

    // We may be consuming bitcode from a client that leaves garbage at the end
    // of the bitcode stream (e.g. Apple's ar tool). If we are close enough to
    // the end that there cannot possibly be another module, stop looking.
    if (BCBegin + 8 >= Stream.getBitcodeBytes().size())
      return Modules;

    BitstreamEntry Entry = Stream.advance();
    switch (Entry.Kind) {
    case BitstreamEntry::EndBlock:
    case BitstreamEntry::Error:
      return error("Malformed block");

    case BitstreamEntry::SubBlock: {
      uint64_t IdentificationBit = -1ull;
      if (Entry.ID == bitc::IDENTIFICATION_BLOCK_ID) {
        IdentificationBit = Stream.GetCurrentBitNo() - BCBegin * 8;
        if (Stream.SkipBlock())
          return error("Malformed block");

        Entry = Stream.advance();
        if (Entry.Kind != BitstreamEntry::SubBlock ||
            Entry.ID != bitc::MODULE_BLOCK_ID)
          return error("Malformed block");
      }

      if (Entry.ID == bitc::MODULE_BLOCK_ID) {
        uint64_t ModuleBit = Stream.GetCurrentBitNo() - BCBegin * 8;
        if (Stream.SkipBlock())
          return error("Malformed block");

        Modules.push_back({Stream.getBitcodeBytes().slice(
                               BCBegin, Stream.getCurrentByteNo() - BCBegin),
                           Buffer.getBufferIdentifier(), IdentificationBit,
                           ModuleBit});
        continue;
      }

      if (Stream.SkipBlock())
        return error("Malformed block");
      continue;
    }
    case BitstreamEntry::Record:
      Stream.skipRecord(Entry.ID);
      continue;
    }
  }
}

/// \brief Get a lazy one-at-time loading module from bitcode.
///
/// This isn't always used in a lazy context.  In particular, it's also used by
/// \a parseModule().  If this is truly lazy, then we need to eagerly pull
/// in forward-referenced functions from block address references.
///
/// \param[in] MaterializeAll Set to \c true if we should materialize
/// everything.
Expected<std::unique_ptr<Module>>
BitcodeModule::getModuleImpl(LLVMContext &Context, bool MaterializeAll,
                             bool ShouldLazyLoadMetadata) {
  BitstreamCursor Stream(Buffer);

  std::string ProducerIdentification;
  if (IdentificationBit != -1ull) {
    Stream.JumpToBit(IdentificationBit);
    Expected<std::string> ProducerIdentificationOrErr =
        readIdentificationBlock(Stream);
    if (!ProducerIdentificationOrErr)
      return ProducerIdentificationOrErr.takeError();

    ProducerIdentification = *ProducerIdentificationOrErr;
  }

  Stream.JumpToBit(ModuleBit);
  auto *R =
      new BitcodeReader(std::move(Stream), ProducerIdentification, Context);

  std::unique_ptr<Module> M =
      llvm::make_unique<Module>(ModuleIdentifier, Context);
  M->setMaterializer(R);

  // Delay parsing Metadata if ShouldLazyLoadMetadata is true.
  if (Error Err = R->parseBitcodeInto(M.get(), ShouldLazyLoadMetadata))
    return std::move(Err);

  if (MaterializeAll) {
    // Read in the entire module, and destroy the BitcodeReader.
    if (Error Err = M->materializeAll())
      return std::move(Err);
  } else {
    // Resolve forward references from blockaddresses.
    if (Error Err = R->materializeForwardReferencedFunctions())
      return std::move(Err);
  }
  return std::move(M);
}

Expected<std::unique_ptr<Module>>
BitcodeModule::getLazyModule(LLVMContext &Context,
                             bool ShouldLazyLoadMetadata) {
  return getModuleImpl(Context, false, ShouldLazyLoadMetadata);
}

// Parse the specified bitcode buffer, returning the function info index.
Expected<std::unique_ptr<ModuleSummaryIndex>> BitcodeModule::getSummary() {
  BitstreamCursor Stream(Buffer);
  Stream.JumpToBit(ModuleBit);

  auto Index = llvm::make_unique<ModuleSummaryIndex>();
  ModuleSummaryIndexBitcodeReader R(std::move(Stream), *Index);

  if (Error Err = R.parseModule(ModuleIdentifier))
    return std::move(Err);

  return std::move(Index);
}

// Check if the given bitcode buffer contains a global value summary block.
Expected<bool> BitcodeModule::hasSummary() {
  BitstreamCursor Stream(Buffer);
  Stream.JumpToBit(ModuleBit);

  if (Stream.EnterSubBlock(bitc::MODULE_BLOCK_ID))
    return error("Invalid record");

  while (true) {
    BitstreamEntry Entry = Stream.advance();

    switch (Entry.Kind) {
    case BitstreamEntry::Error:
      return error("Malformed block");
    case BitstreamEntry::EndBlock:
      return false;

    case BitstreamEntry::SubBlock:
      if (Entry.ID == bitc::GLOBALVAL_SUMMARY_BLOCK_ID)
        return true;

      // Ignore other sub-blocks.
      if (Stream.SkipBlock())
        return error("Malformed block");
      continue;

    case BitstreamEntry::Record:
      Stream.skipRecord(Entry.ID);
      continue;
    }
  }
}

static Expected<BitcodeModule> getSingleModule(MemoryBufferRef Buffer) {
  Expected<std::vector<BitcodeModule>> MsOrErr = getBitcodeModuleList(Buffer);
  if (!MsOrErr)
    return MsOrErr.takeError();

  if (MsOrErr->size() != 1)
    return error("Expected a single module");

  return (*MsOrErr)[0];
}

Expected<std::unique_ptr<Module>>
llvm::getLazyBitcodeModule(MemoryBufferRef Buffer,
                           LLVMContext &Context, bool ShouldLazyLoadMetadata) {
  Expected<BitcodeModule> BM = getSingleModule(Buffer);
  if (!BM)
    return BM.takeError();

  return BM->getLazyModule(Context, ShouldLazyLoadMetadata);
}

Expected<std::unique_ptr<Module>>
llvm::getOwningLazyBitcodeModule(std::unique_ptr<MemoryBuffer> &&Buffer,
                                 LLVMContext &Context,
                                 bool ShouldLazyLoadMetadata) {
  auto MOrErr = getLazyBitcodeModule(*Buffer, Context, ShouldLazyLoadMetadata);
  if (MOrErr)
    (*MOrErr)->setOwnedMemoryBuffer(std::move(Buffer));
  return MOrErr;
}

Expected<std::unique_ptr<Module>>
BitcodeModule::parseModule(LLVMContext &Context) {
  return getModuleImpl(Context, true, false);
  // TODO: Restore the use-lists to the in-memory state when the bitcode was
  // written.  We must defer until the Module has been fully materialized.
}

Expected<std::unique_ptr<Module>> llvm::parseBitcodeFile(MemoryBufferRef Buffer,
                                                         LLVMContext &Context) {
  Expected<BitcodeModule> BM = getSingleModule(Buffer);
  if (!BM)
    return BM.takeError();

  return BM->parseModule(Context);
}

Expected<std::string> llvm::getBitcodeTargetTriple(MemoryBufferRef Buffer) {
  Expected<BitstreamCursor> StreamOrErr = initStream(Buffer);
  if (!StreamOrErr)
    return StreamOrErr.takeError();

  return readTriple(*StreamOrErr);
}

Expected<bool> llvm::isBitcodeContainingObjCCategory(MemoryBufferRef Buffer) {
  Expected<BitstreamCursor> StreamOrErr = initStream(Buffer);
  if (!StreamOrErr)
    return StreamOrErr.takeError();

  return hasObjCCategory(*StreamOrErr);
}

Expected<std::string> llvm::getBitcodeProducerString(MemoryBufferRef Buffer) {
  Expected<BitstreamCursor> StreamOrErr = initStream(Buffer);
  if (!StreamOrErr)
    return StreamOrErr.takeError();

  return readIdentificationCode(*StreamOrErr);
}

Expected<std::unique_ptr<ModuleSummaryIndex>>
llvm::getModuleSummaryIndex(MemoryBufferRef Buffer) {
  Expected<BitcodeModule> BM = getSingleModule(Buffer);
  if (!BM)
    return BM.takeError();

  return BM->getSummary();
}

Expected<bool> llvm::hasGlobalValueSummary(MemoryBufferRef Buffer) {
  Expected<BitcodeModule> BM = getSingleModule(Buffer);
  if (!BM)
    return BM.takeError();

  return BM->hasSummary();
}
