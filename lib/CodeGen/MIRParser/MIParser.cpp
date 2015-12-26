//===- MIParser.cpp - Machine instructions parser implementation ----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the parsing of machine instructions.
//
//===----------------------------------------------------------------------===//

#include "MIParser.h"
#include "MILexer.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/AsmParser/Parser.h"
#include "llvm/AsmParser/SlotMapping.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ModuleSlotTracker.h"
#include "llvm/IR/ValueSymbolTable.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Target/TargetSubtargetInfo.h"
#include "llvm/Target/TargetInstrInfo.h"

using namespace llvm;

namespace {

/// A wrapper struct around the 'MachineOperand' struct that includes a source
/// range and other attributes.
struct ParsedMachineOperand {
  MachineOperand Operand;
  StringRef::iterator Begin;
  StringRef::iterator End;
  Optional<unsigned> TiedDefIdx;

  ParsedMachineOperand(const MachineOperand &Operand, StringRef::iterator Begin,
                       StringRef::iterator End, Optional<unsigned> &TiedDefIdx)
      : Operand(Operand), Begin(Begin), End(End), TiedDefIdx(TiedDefIdx) {
    if (TiedDefIdx)
      assert(Operand.isReg() && Operand.isUse() &&
             "Only used register operands can be tied");
  }
};

class MIParser {
  SourceMgr &SM;
  MachineFunction &MF;
  SMDiagnostic &Error;
  StringRef Source, CurrentSource;
  MIToken Token;
  const PerFunctionMIParsingState &PFS;
  /// Maps from indices to unnamed global values and metadata nodes.
  const SlotMapping &IRSlots;
  /// Maps from instruction names to op codes.
  StringMap<unsigned> Names2InstrOpCodes;
  /// Maps from register names to registers.
  StringMap<unsigned> Names2Regs;
  /// Maps from register mask names to register masks.
  StringMap<const uint32_t *> Names2RegMasks;
  /// Maps from subregister names to subregister indices.
  StringMap<unsigned> Names2SubRegIndices;
  /// Maps from slot numbers to function's unnamed basic blocks.
  DenseMap<unsigned, const BasicBlock *> Slots2BasicBlocks;
  /// Maps from slot numbers to function's unnamed values.
  DenseMap<unsigned, const Value *> Slots2Values;
  /// Maps from target index names to target indices.
  StringMap<int> Names2TargetIndices;
  /// Maps from direct target flag names to the direct target flag values.
  StringMap<unsigned> Names2DirectTargetFlags;
  /// Maps from direct target flag names to the bitmask target flag values.
  StringMap<unsigned> Names2BitmaskTargetFlags;

public:
  MIParser(SourceMgr &SM, MachineFunction &MF, SMDiagnostic &Error,
           StringRef Source, const PerFunctionMIParsingState &PFS,
           const SlotMapping &IRSlots);

  void lex();

  /// Report an error at the current location with the given message.
  ///
  /// This function always return true.
  bool error(const Twine &Msg);

  /// Report an error at the given location with the given message.
  ///
  /// This function always return true.
  bool error(StringRef::iterator Loc, const Twine &Msg);

  bool
  parseBasicBlockDefinitions(DenseMap<unsigned, MachineBasicBlock *> &MBBSlots);
  bool parseBasicBlocks();
  bool parse(MachineInstr *&MI);
  bool parseStandaloneMBB(MachineBasicBlock *&MBB);
  bool parseStandaloneNamedRegister(unsigned &Reg);
  bool parseStandaloneVirtualRegister(unsigned &Reg);
  bool parseStandaloneStackObject(int &FI);
  bool parseStandaloneMDNode(MDNode *&Node);

  bool
  parseBasicBlockDefinition(DenseMap<unsigned, MachineBasicBlock *> &MBBSlots);
  bool parseBasicBlock(MachineBasicBlock &MBB);
  bool parseBasicBlockLiveins(MachineBasicBlock &MBB);
  bool parseBasicBlockSuccessors(MachineBasicBlock &MBB);

  bool parseRegister(unsigned &Reg);
  bool parseRegisterFlag(unsigned &Flags);
  bool parseSubRegisterIndex(unsigned &SubReg);
  bool parseRegisterTiedDefIndex(unsigned &TiedDefIdx);
  bool parseRegisterOperand(MachineOperand &Dest,
                            Optional<unsigned> &TiedDefIdx, bool IsDef = false);
  bool parseImmediateOperand(MachineOperand &Dest);
  bool parseIRConstant(StringRef::iterator Loc, StringRef Source,
                       const Constant *&C);
  bool parseIRConstant(StringRef::iterator Loc, const Constant *&C);
  bool parseTypedImmediateOperand(MachineOperand &Dest);
  bool parseFPImmediateOperand(MachineOperand &Dest);
  bool parseMBBReference(MachineBasicBlock *&MBB);
  bool parseMBBOperand(MachineOperand &Dest);
  bool parseStackFrameIndex(int &FI);
  bool parseStackObjectOperand(MachineOperand &Dest);
  bool parseFixedStackFrameIndex(int &FI);
  bool parseFixedStackObjectOperand(MachineOperand &Dest);
  bool parseGlobalValue(GlobalValue *&GV);
  bool parseGlobalAddressOperand(MachineOperand &Dest);
  bool parseConstantPoolIndexOperand(MachineOperand &Dest);
  bool parseJumpTableIndexOperand(MachineOperand &Dest);
  bool parseExternalSymbolOperand(MachineOperand &Dest);
  bool parseMDNode(MDNode *&Node);
  bool parseMetadataOperand(MachineOperand &Dest);
  bool parseCFIOffset(int &Offset);
  bool parseCFIRegister(unsigned &Reg);
  bool parseCFIOperand(MachineOperand &Dest);
  bool parseIRBlock(BasicBlock *&BB, const Function &F);
  bool parseBlockAddressOperand(MachineOperand &Dest);
  bool parseTargetIndexOperand(MachineOperand &Dest);
  bool parseLiveoutRegisterMaskOperand(MachineOperand &Dest);
  bool parseMachineOperand(MachineOperand &Dest,
                           Optional<unsigned> &TiedDefIdx);
  bool parseMachineOperandAndTargetFlags(MachineOperand &Dest,
                                         Optional<unsigned> &TiedDefIdx);
  bool parseOffset(int64_t &Offset);
  bool parseAlignment(unsigned &Alignment);
  bool parseOperandsOffset(MachineOperand &Op);
  bool parseIRValue(const Value *&V);
  bool parseMemoryOperandFlag(unsigned &Flags);
  bool parseMemoryPseudoSourceValue(const PseudoSourceValue *&PSV);
  bool parseMachinePointerInfo(MachinePointerInfo &Dest);
  bool parseMachineMemoryOperand(MachineMemOperand *&Dest);

private:
  /// Convert the integer literal in the current token into an unsigned integer.
  ///
  /// Return true if an error occurred.
  bool getUnsigned(unsigned &Result);

  /// Convert the integer literal in the current token into an uint64.
  ///
  /// Return true if an error occurred.
  bool getUint64(uint64_t &Result);

  /// If the current token is of the given kind, consume it and return false.
  /// Otherwise report an error and return true.
  bool expectAndConsume(MIToken::TokenKind TokenKind);

  /// If the current token is of the given kind, consume it and return true.
  /// Otherwise return false.
  bool consumeIfPresent(MIToken::TokenKind TokenKind);

  void initNames2InstrOpCodes();

  /// Try to convert an instruction name to an opcode. Return true if the
  /// instruction name is invalid.
  bool parseInstrName(StringRef InstrName, unsigned &OpCode);

  bool parseInstruction(unsigned &OpCode, unsigned &Flags);

  bool assignRegisterTies(MachineInstr &MI,
                          ArrayRef<ParsedMachineOperand> Operands);

  bool verifyImplicitOperands(ArrayRef<ParsedMachineOperand> Operands,
                              const MCInstrDesc &MCID);

  void initNames2Regs();

  /// Try to convert a register name to a register number. Return true if the
  /// register name is invalid.
  bool getRegisterByName(StringRef RegName, unsigned &Reg);

  void initNames2RegMasks();

  /// Check if the given identifier is a name of a register mask.
  ///
  /// Return null if the identifier isn't a register mask.
  const uint32_t *getRegMask(StringRef Identifier);

  void initNames2SubRegIndices();

  /// Check if the given identifier is a name of a subregister index.
  ///
  /// Return 0 if the name isn't a subregister index class.
  unsigned getSubRegIndex(StringRef Name);

  const BasicBlock *getIRBlock(unsigned Slot);
  const BasicBlock *getIRBlock(unsigned Slot, const Function &F);

  const Value *getIRValue(unsigned Slot);

  void initNames2TargetIndices();

  /// Try to convert a name of target index to the corresponding target index.
  ///
  /// Return true if the name isn't a name of a target index.
  bool getTargetIndex(StringRef Name, int &Index);

  void initNames2DirectTargetFlags();

  /// Try to convert a name of a direct target flag to the corresponding
  /// target flag.
  ///
  /// Return true if the name isn't a name of a direct flag.
  bool getDirectTargetFlag(StringRef Name, unsigned &Flag);

  void initNames2BitmaskTargetFlags();

  /// Try to convert a name of a bitmask target flag to the corresponding
  /// target flag.
  ///
  /// Return true if the name isn't a name of a bitmask target flag.
  bool getBitmaskTargetFlag(StringRef Name, unsigned &Flag);
};

} // end anonymous namespace

MIParser::MIParser(SourceMgr &SM, MachineFunction &MF, SMDiagnostic &Error,
                   StringRef Source, const PerFunctionMIParsingState &PFS,
                   const SlotMapping &IRSlots)
    : SM(SM), MF(MF), Error(Error), Source(Source), CurrentSource(Source),
      PFS(PFS), IRSlots(IRSlots) {}

void MIParser::lex() {
  CurrentSource = lexMIToken(
      CurrentSource, Token,
      [this](StringRef::iterator Loc, const Twine &Msg) { error(Loc, Msg); });
}

bool MIParser::error(const Twine &Msg) { return error(Token.location(), Msg); }

bool MIParser::error(StringRef::iterator Loc, const Twine &Msg) {
  assert(Loc >= Source.data() && Loc <= (Source.data() + Source.size()));
  const MemoryBuffer &Buffer = *SM.getMemoryBuffer(SM.getMainFileID());
  if (Loc >= Buffer.getBufferStart() && Loc <= Buffer.getBufferEnd()) {
    // Create an ordinary diagnostic when the source manager's buffer is the
    // source string.
    Error = SM.GetMessage(SMLoc::getFromPointer(Loc), SourceMgr::DK_Error, Msg);
    return true;
  }
  // Create a diagnostic for a YAML string literal.
  Error = SMDiagnostic(SM, SMLoc(), Buffer.getBufferIdentifier(), 1,
                       Loc - Source.data(), SourceMgr::DK_Error, Msg.str(),
                       Source, None, None);
  return true;
}

static const char *toString(MIToken::TokenKind TokenKind) {
  switch (TokenKind) {
  case MIToken::comma:
    return "','";
  case MIToken::equal:
    return "'='";
  case MIToken::colon:
    return "':'";
  case MIToken::lparen:
    return "'('";
  case MIToken::rparen:
    return "')'";
  default:
    return "<unknown token>";
  }
}

bool MIParser::expectAndConsume(MIToken::TokenKind TokenKind) {
  if (Token.isNot(TokenKind))
    return error(Twine("expected ") + toString(TokenKind));
  lex();
  return false;
}

bool MIParser::consumeIfPresent(MIToken::TokenKind TokenKind) {
  if (Token.isNot(TokenKind))
    return false;
  lex();
  return true;
}

bool MIParser::parseBasicBlockDefinition(
    DenseMap<unsigned, MachineBasicBlock *> &MBBSlots) {
  assert(Token.is(MIToken::MachineBasicBlockLabel));
  unsigned ID = 0;
  if (getUnsigned(ID))
    return true;
  auto Loc = Token.location();
  auto Name = Token.stringValue();
  lex();
  bool HasAddressTaken = false;
  bool IsLandingPad = false;
  unsigned Alignment = 0;
  BasicBlock *BB = nullptr;
  if (consumeIfPresent(MIToken::lparen)) {
    do {
      // TODO: Report an error when multiple same attributes are specified.
      switch (Token.kind()) {
      case MIToken::kw_address_taken:
        HasAddressTaken = true;
        lex();
        break;
      case MIToken::kw_landing_pad:
        IsLandingPad = true;
        lex();
        break;
      case MIToken::kw_align:
        if (parseAlignment(Alignment))
          return true;
        break;
      case MIToken::IRBlock:
        // TODO: Report an error when both name and ir block are specified.
        if (parseIRBlock(BB, *MF.getFunction()))
          return true;
        lex();
        break;
      default:
        break;
      }
    } while (consumeIfPresent(MIToken::comma));
    if (expectAndConsume(MIToken::rparen))
      return true;
  }
  if (expectAndConsume(MIToken::colon))
    return true;

  if (!Name.empty()) {
    BB = dyn_cast_or_null<BasicBlock>(
        MF.getFunction()->getValueSymbolTable().lookup(Name));
    if (!BB)
      return error(Loc, Twine("basic block '") + Name +
                            "' is not defined in the function '" +
                            MF.getName() + "'");
  }
  auto *MBB = MF.CreateMachineBasicBlock(BB);
  MF.insert(MF.end(), MBB);
  bool WasInserted = MBBSlots.insert(std::make_pair(ID, MBB)).second;
  if (!WasInserted)
    return error(Loc, Twine("redefinition of machine basic block with id #") +
                          Twine(ID));
  if (Alignment)
    MBB->setAlignment(Alignment);
  if (HasAddressTaken)
    MBB->setHasAddressTaken();
  MBB->setIsEHPad(IsLandingPad);
  return false;
}

bool MIParser::parseBasicBlockDefinitions(
    DenseMap<unsigned, MachineBasicBlock *> &MBBSlots) {
  lex();
  // Skip until the first machine basic block.
  while (Token.is(MIToken::Newline))
    lex();
  if (Token.isErrorOrEOF())
    return Token.isError();
  if (Token.isNot(MIToken::MachineBasicBlockLabel))
    return error("expected a basic block definition before instructions");
  unsigned BraceDepth = 0;
  do {
    if (parseBasicBlockDefinition(MBBSlots))
      return true;
    bool IsAfterNewline = false;
    // Skip until the next machine basic block.
    while (true) {
      if ((Token.is(MIToken::MachineBasicBlockLabel) && IsAfterNewline) ||
          Token.isErrorOrEOF())
        break;
      else if (Token.is(MIToken::MachineBasicBlockLabel))
        return error("basic block definition should be located at the start of "
                     "the line");
      else if (consumeIfPresent(MIToken::Newline)) {
        IsAfterNewline = true;
        continue;
      }
      IsAfterNewline = false;
      if (Token.is(MIToken::lbrace))
        ++BraceDepth;
      if (Token.is(MIToken::rbrace)) {
        if (!BraceDepth)
          return error("extraneous closing brace ('}')");
        --BraceDepth;
      }
      lex();
    }
    // Verify that we closed all of the '{' at the end of a file or a block.
    if (!Token.isError() && BraceDepth)
      return error("expected '}'"); // FIXME: Report a note that shows '{'.
  } while (!Token.isErrorOrEOF());
  return Token.isError();
}

bool MIParser::parseBasicBlockLiveins(MachineBasicBlock &MBB) {
  assert(Token.is(MIToken::kw_liveins));
  lex();
  if (expectAndConsume(MIToken::colon))
    return true;
  if (Token.isNewlineOrEOF()) // Allow an empty list of liveins.
    return false;
  do {
    if (Token.isNot(MIToken::NamedRegister))
      return error("expected a named register");
    unsigned Reg = 0;
    if (parseRegister(Reg))
      return true;
    MBB.addLiveIn(Reg);
    lex();
  } while (consumeIfPresent(MIToken::comma));
  return false;
}

bool MIParser::parseBasicBlockSuccessors(MachineBasicBlock &MBB) {
  assert(Token.is(MIToken::kw_successors));
  lex();
  if (expectAndConsume(MIToken::colon))
    return true;
  if (Token.isNewlineOrEOF()) // Allow an empty list of successors.
    return false;
  do {
    if (Token.isNot(MIToken::MachineBasicBlock))
      return error("expected a machine basic block reference");
    MachineBasicBlock *SuccMBB = nullptr;
    if (parseMBBReference(SuccMBB))
      return true;
    lex();
    unsigned Weight = 0;
    if (consumeIfPresent(MIToken::lparen)) {
      if (Token.isNot(MIToken::IntegerLiteral))
        return error("expected an integer literal after '('");
      if (getUnsigned(Weight))
        return true;
      lex();
      if (expectAndConsume(MIToken::rparen))
        return true;
    }
    MBB.addSuccessor(SuccMBB, BranchProbability::getRaw(Weight));
  } while (consumeIfPresent(MIToken::comma));
  MBB.normalizeSuccProbs();
  return false;
}

bool MIParser::parseBasicBlock(MachineBasicBlock &MBB) {
  // Skip the definition.
  assert(Token.is(MIToken::MachineBasicBlockLabel));
  lex();
  if (consumeIfPresent(MIToken::lparen)) {
    while (Token.isNot(MIToken::rparen) && !Token.isErrorOrEOF())
      lex();
    consumeIfPresent(MIToken::rparen);
  }
  consumeIfPresent(MIToken::colon);

  // Parse the liveins and successors.
  // N.B: Multiple lists of successors and liveins are allowed and they're
  // merged into one.
  // Example:
  //   liveins: %edi
  //   liveins: %esi
  //
  // is equivalent to
  //   liveins: %edi, %esi
  while (true) {
    if (Token.is(MIToken::kw_successors)) {
      if (parseBasicBlockSuccessors(MBB))
        return true;
    } else if (Token.is(MIToken::kw_liveins)) {
      if (parseBasicBlockLiveins(MBB))
        return true;
    } else if (consumeIfPresent(MIToken::Newline)) {
      continue;
    } else
      break;
    if (!Token.isNewlineOrEOF())
      return error("expected line break at the end of a list");
    lex();
  }

  // Parse the instructions.
  bool IsInBundle = false;
  MachineInstr *PrevMI = nullptr;
  while (true) {
    if (Token.is(MIToken::MachineBasicBlockLabel) || Token.is(MIToken::Eof))
      return false;
    else if (consumeIfPresent(MIToken::Newline))
      continue;
    if (consumeIfPresent(MIToken::rbrace)) {
      // The first parsing pass should verify that all closing '}' have an
      // opening '{'.
      assert(IsInBundle);
      IsInBundle = false;
      continue;
    }
    MachineInstr *MI = nullptr;
    if (parse(MI))
      return true;
    MBB.insert(MBB.end(), MI);
    if (IsInBundle) {
      PrevMI->setFlag(MachineInstr::BundledSucc);
      MI->setFlag(MachineInstr::BundledPred);
    }
    PrevMI = MI;
    if (Token.is(MIToken::lbrace)) {
      if (IsInBundle)
        return error("nested instruction bundles are not allowed");
      lex();
      // This instruction is the start of the bundle.
      MI->setFlag(MachineInstr::BundledSucc);
      IsInBundle = true;
      if (!Token.is(MIToken::Newline))
        // The next instruction can be on the same line.
        continue;
    }
    assert(Token.isNewlineOrEOF() && "MI is not fully parsed");
    lex();
  }
  return false;
}

bool MIParser::parseBasicBlocks() {
  lex();
  // Skip until the first machine basic block.
  while (Token.is(MIToken::Newline))
    lex();
  if (Token.isErrorOrEOF())
    return Token.isError();
  // The first parsing pass should have verified that this token is a MBB label
  // in the 'parseBasicBlockDefinitions' method.
  assert(Token.is(MIToken::MachineBasicBlockLabel));
  do {
    MachineBasicBlock *MBB = nullptr;
    if (parseMBBReference(MBB))
      return true;
    if (parseBasicBlock(*MBB))
      return true;
    // The method 'parseBasicBlock' should parse the whole block until the next
    // block or the end of file.
    assert(Token.is(MIToken::MachineBasicBlockLabel) || Token.is(MIToken::Eof));
  } while (Token.isNot(MIToken::Eof));
  return false;
}

bool MIParser::parse(MachineInstr *&MI) {
  // Parse any register operands before '='
  MachineOperand MO = MachineOperand::CreateImm(0);
  SmallVector<ParsedMachineOperand, 8> Operands;
  while (Token.isRegister() || Token.isRegisterFlag()) {
    auto Loc = Token.location();
    Optional<unsigned> TiedDefIdx;
    if (parseRegisterOperand(MO, TiedDefIdx, /*IsDef=*/true))
      return true;
    Operands.push_back(
        ParsedMachineOperand(MO, Loc, Token.location(), TiedDefIdx));
    if (Token.isNot(MIToken::comma))
      break;
    lex();
  }
  if (!Operands.empty() && expectAndConsume(MIToken::equal))
    return true;

  unsigned OpCode, Flags = 0;
  if (Token.isError() || parseInstruction(OpCode, Flags))
    return true;

  // Parse the remaining machine operands.
  while (!Token.isNewlineOrEOF() && Token.isNot(MIToken::kw_debug_location) &&
         Token.isNot(MIToken::coloncolon) && Token.isNot(MIToken::lbrace)) {
    auto Loc = Token.location();
    Optional<unsigned> TiedDefIdx;
    if (parseMachineOperandAndTargetFlags(MO, TiedDefIdx))
      return true;
    Operands.push_back(
        ParsedMachineOperand(MO, Loc, Token.location(), TiedDefIdx));
    if (Token.isNewlineOrEOF() || Token.is(MIToken::coloncolon) ||
        Token.is(MIToken::lbrace))
      break;
    if (Token.isNot(MIToken::comma))
      return error("expected ',' before the next machine operand");
    lex();
  }

  DebugLoc DebugLocation;
  if (Token.is(MIToken::kw_debug_location)) {
    lex();
    if (Token.isNot(MIToken::exclaim))
      return error("expected a metadata node after 'debug-location'");
    MDNode *Node = nullptr;
    if (parseMDNode(Node))
      return true;
    DebugLocation = DebugLoc(Node);
  }

  // Parse the machine memory operands.
  SmallVector<MachineMemOperand *, 2> MemOperands;
  if (Token.is(MIToken::coloncolon)) {
    lex();
    while (!Token.isNewlineOrEOF()) {
      MachineMemOperand *MemOp = nullptr;
      if (parseMachineMemoryOperand(MemOp))
        return true;
      MemOperands.push_back(MemOp);
      if (Token.isNewlineOrEOF())
        break;
      if (Token.isNot(MIToken::comma))
        return error("expected ',' before the next machine memory operand");
      lex();
    }
  }

  const auto &MCID = MF.getSubtarget().getInstrInfo()->get(OpCode);
  if (!MCID.isVariadic()) {
    // FIXME: Move the implicit operand verification to the machine verifier.
    if (verifyImplicitOperands(Operands, MCID))
      return true;
  }

  // TODO: Check for extraneous machine operands.
  MI = MF.CreateMachineInstr(MCID, DebugLocation, /*NoImplicit=*/true);
  MI->setFlags(Flags);
  for (const auto &Operand : Operands)
    MI->addOperand(MF, Operand.Operand);
  if (assignRegisterTies(*MI, Operands))
    return true;
  if (MemOperands.empty())
    return false;
  MachineInstr::mmo_iterator MemRefs =
      MF.allocateMemRefsArray(MemOperands.size());
  std::copy(MemOperands.begin(), MemOperands.end(), MemRefs);
  MI->setMemRefs(MemRefs, MemRefs + MemOperands.size());
  return false;
}

bool MIParser::parseStandaloneMBB(MachineBasicBlock *&MBB) {
  lex();
  if (Token.isNot(MIToken::MachineBasicBlock))
    return error("expected a machine basic block reference");
  if (parseMBBReference(MBB))
    return true;
  lex();
  if (Token.isNot(MIToken::Eof))
    return error(
        "expected end of string after the machine basic block reference");
  return false;
}

bool MIParser::parseStandaloneNamedRegister(unsigned &Reg) {
  lex();
  if (Token.isNot(MIToken::NamedRegister))
    return error("expected a named register");
  if (parseRegister(Reg))
    return true;
  lex();
  if (Token.isNot(MIToken::Eof))
    return error("expected end of string after the register reference");
  return false;
}

bool MIParser::parseStandaloneVirtualRegister(unsigned &Reg) {
  lex();
  if (Token.isNot(MIToken::VirtualRegister))
    return error("expected a virtual register");
  if (parseRegister(Reg))
    return true;
  lex();
  if (Token.isNot(MIToken::Eof))
    return error("expected end of string after the register reference");
  return false;
}

bool MIParser::parseStandaloneStackObject(int &FI) {
  lex();
  if (Token.isNot(MIToken::StackObject))
    return error("expected a stack object");
  if (parseStackFrameIndex(FI))
    return true;
  if (Token.isNot(MIToken::Eof))
    return error("expected end of string after the stack object reference");
  return false;
}

bool MIParser::parseStandaloneMDNode(MDNode *&Node) {
  lex();
  if (Token.isNot(MIToken::exclaim))
    return error("expected a metadata node");
  if (parseMDNode(Node))
    return true;
  if (Token.isNot(MIToken::Eof))
    return error("expected end of string after the metadata node");
  return false;
}

static const char *printImplicitRegisterFlag(const MachineOperand &MO) {
  assert(MO.isImplicit());
  return MO.isDef() ? "implicit-def" : "implicit";
}

static std::string getRegisterName(const TargetRegisterInfo *TRI,
                                   unsigned Reg) {
  assert(TargetRegisterInfo::isPhysicalRegister(Reg) && "expected phys reg");
  return StringRef(TRI->getName(Reg)).lower();
}

/// Return true if the parsed machine operands contain a given machine operand.
static bool isImplicitOperandIn(const MachineOperand &ImplicitOperand,
                                ArrayRef<ParsedMachineOperand> Operands) {
  for (const auto &I : Operands) {
    if (ImplicitOperand.isIdenticalTo(I.Operand))
      return true;
  }
  return false;
}

bool MIParser::verifyImplicitOperands(ArrayRef<ParsedMachineOperand> Operands,
                                      const MCInstrDesc &MCID) {
  if (MCID.isCall())
    // We can't verify call instructions as they can contain arbitrary implicit
    // register and register mask operands.
    return false;

  // Gather all the expected implicit operands.
  SmallVector<MachineOperand, 4> ImplicitOperands;
  if (MCID.ImplicitDefs)
    for (const MCPhysReg *ImpDefs = MCID.getImplicitDefs(); *ImpDefs; ++ImpDefs)
      ImplicitOperands.push_back(
          MachineOperand::CreateReg(*ImpDefs, true, true));
  if (MCID.ImplicitUses)
    for (const MCPhysReg *ImpUses = MCID.getImplicitUses(); *ImpUses; ++ImpUses)
      ImplicitOperands.push_back(
          MachineOperand::CreateReg(*ImpUses, false, true));

  const auto *TRI = MF.getSubtarget().getRegisterInfo();
  assert(TRI && "Expected target register info");
  for (const auto &I : ImplicitOperands) {
    if (isImplicitOperandIn(I, Operands))
      continue;
    return error(Operands.empty() ? Token.location() : Operands.back().End,
                 Twine("missing implicit register operand '") +
                     printImplicitRegisterFlag(I) + " %" +
                     getRegisterName(TRI, I.getReg()) + "'");
  }
  return false;
}

bool MIParser::parseInstruction(unsigned &OpCode, unsigned &Flags) {
  if (Token.is(MIToken::kw_frame_setup)) {
    Flags |= MachineInstr::FrameSetup;
    lex();
  }
  if (Token.isNot(MIToken::Identifier))
    return error("expected a machine instruction");
  StringRef InstrName = Token.stringValue();
  if (parseInstrName(InstrName, OpCode))
    return error(Twine("unknown machine instruction name '") + InstrName + "'");
  lex();
  return false;
}

bool MIParser::parseRegister(unsigned &Reg) {
  switch (Token.kind()) {
  case MIToken::underscore:
    Reg = 0;
    break;
  case MIToken::NamedRegister: {
    StringRef Name = Token.stringValue();
    if (getRegisterByName(Name, Reg))
      return error(Twine("unknown register name '") + Name + "'");
    break;
  }
  case MIToken::VirtualRegister: {
    unsigned ID;
    if (getUnsigned(ID))
      return true;
    const auto RegInfo = PFS.VirtualRegisterSlots.find(ID);
    if (RegInfo == PFS.VirtualRegisterSlots.end())
      return error(Twine("use of undefined virtual register '%") + Twine(ID) +
                   "'");
    Reg = RegInfo->second;
    break;
  }
  // TODO: Parse other register kinds.
  default:
    llvm_unreachable("The current token should be a register");
  }
  return false;
}

bool MIParser::parseRegisterFlag(unsigned &Flags) {
  const unsigned OldFlags = Flags;
  switch (Token.kind()) {
  case MIToken::kw_implicit:
    Flags |= RegState::Implicit;
    break;
  case MIToken::kw_implicit_define:
    Flags |= RegState::ImplicitDefine;
    break;
  case MIToken::kw_def:
    Flags |= RegState::Define;
    break;
  case MIToken::kw_dead:
    Flags |= RegState::Dead;
    break;
  case MIToken::kw_killed:
    Flags |= RegState::Kill;
    break;
  case MIToken::kw_undef:
    Flags |= RegState::Undef;
    break;
  case MIToken::kw_internal:
    Flags |= RegState::InternalRead;
    break;
  case MIToken::kw_early_clobber:
    Flags |= RegState::EarlyClobber;
    break;
  case MIToken::kw_debug_use:
    Flags |= RegState::Debug;
    break;
  default:
    llvm_unreachable("The current token should be a register flag");
  }
  if (OldFlags == Flags)
    // We know that the same flag is specified more than once when the flags
    // weren't modified.
    return error("duplicate '" + Token.stringValue() + "' register flag");
  lex();
  return false;
}

bool MIParser::parseSubRegisterIndex(unsigned &SubReg) {
  assert(Token.is(MIToken::colon));
  lex();
  if (Token.isNot(MIToken::Identifier))
    return error("expected a subregister index after ':'");
  auto Name = Token.stringValue();
  SubReg = getSubRegIndex(Name);
  if (!SubReg)
    return error(Twine("use of unknown subregister index '") + Name + "'");
  lex();
  return false;
}

bool MIParser::parseRegisterTiedDefIndex(unsigned &TiedDefIdx) {
  if (!consumeIfPresent(MIToken::kw_tied_def))
    return error("expected 'tied-def' after '('");
  if (Token.isNot(MIToken::IntegerLiteral))
    return error("expected an integer literal after 'tied-def'");
  if (getUnsigned(TiedDefIdx))
    return true;
  lex();
  if (expectAndConsume(MIToken::rparen))
    return true;
  return false;
}

bool MIParser::assignRegisterTies(MachineInstr &MI,
                                  ArrayRef<ParsedMachineOperand> Operands) {
  SmallVector<std::pair<unsigned, unsigned>, 4> TiedRegisterPairs;
  for (unsigned I = 0, E = Operands.size(); I != E; ++I) {
    if (!Operands[I].TiedDefIdx)
      continue;
    // The parser ensures that this operand is a register use, so we just have
    // to check the tied-def operand.
    unsigned DefIdx = Operands[I].TiedDefIdx.getValue();
    if (DefIdx >= E)
      return error(Operands[I].Begin,
                   Twine("use of invalid tied-def operand index '" +
                         Twine(DefIdx) + "'; instruction has only ") +
                       Twine(E) + " operands");
    const auto &DefOperand = Operands[DefIdx].Operand;
    if (!DefOperand.isReg() || !DefOperand.isDef())
      // FIXME: add note with the def operand.
      return error(Operands[I].Begin,
                   Twine("use of invalid tied-def operand index '") +
                       Twine(DefIdx) + "'; the operand #" + Twine(DefIdx) +
                       " isn't a defined register");
    // Check that the tied-def operand wasn't tied elsewhere.
    for (const auto &TiedPair : TiedRegisterPairs) {
      if (TiedPair.first == DefIdx)
        return error(Operands[I].Begin,
                     Twine("the tied-def operand #") + Twine(DefIdx) +
                         " is already tied with another register operand");
    }
    TiedRegisterPairs.push_back(std::make_pair(DefIdx, I));
  }
  // FIXME: Verify that for non INLINEASM instructions, the def and use tied
  // indices must be less than tied max.
  for (const auto &TiedPair : TiedRegisterPairs)
    MI.tieOperands(TiedPair.first, TiedPair.second);
  return false;
}

bool MIParser::parseRegisterOperand(MachineOperand &Dest,
                                    Optional<unsigned> &TiedDefIdx,
                                    bool IsDef) {
  unsigned Reg;
  unsigned Flags = IsDef ? RegState::Define : 0;
  while (Token.isRegisterFlag()) {
    if (parseRegisterFlag(Flags))
      return true;
  }
  if (!Token.isRegister())
    return error("expected a register after register flags");
  if (parseRegister(Reg))
    return true;
  lex();
  unsigned SubReg = 0;
  if (Token.is(MIToken::colon)) {
    if (parseSubRegisterIndex(SubReg))
      return true;
  }
  if ((Flags & RegState::Define) == 0 && consumeIfPresent(MIToken::lparen)) {
    unsigned Idx;
    if (parseRegisterTiedDefIndex(Idx))
      return true;
    TiedDefIdx = Idx;
  }
  Dest = MachineOperand::CreateReg(
      Reg, Flags & RegState::Define, Flags & RegState::Implicit,
      Flags & RegState::Kill, Flags & RegState::Dead, Flags & RegState::Undef,
      Flags & RegState::EarlyClobber, SubReg, Flags & RegState::Debug,
      Flags & RegState::InternalRead);
  return false;
}

bool MIParser::parseImmediateOperand(MachineOperand &Dest) {
  assert(Token.is(MIToken::IntegerLiteral));
  const APSInt &Int = Token.integerValue();
  if (Int.getMinSignedBits() > 64)
    return error("integer literal is too large to be an immediate operand");
  Dest = MachineOperand::CreateImm(Int.getExtValue());
  lex();
  return false;
}

bool MIParser::parseIRConstant(StringRef::iterator Loc, StringRef StringValue,
                               const Constant *&C) {
  auto Source = StringValue.str(); // The source has to be null terminated.
  SMDiagnostic Err;
  C = parseConstantValue(Source.c_str(), Err, *MF.getFunction()->getParent(),
                         &IRSlots);
  if (!C)
    return error(Loc + Err.getColumnNo(), Err.getMessage());
  return false;
}

bool MIParser::parseIRConstant(StringRef::iterator Loc, const Constant *&C) {
  if (parseIRConstant(Loc, StringRef(Loc, Token.range().end() - Loc), C))
    return true;
  lex();
  return false;
}

bool MIParser::parseTypedImmediateOperand(MachineOperand &Dest) {
  assert(Token.is(MIToken::IntegerType));
  auto Loc = Token.location();
  lex();
  if (Token.isNot(MIToken::IntegerLiteral))
    return error("expected an integer literal");
  const Constant *C = nullptr;
  if (parseIRConstant(Loc, C))
    return true;
  Dest = MachineOperand::CreateCImm(cast<ConstantInt>(C));
  return false;
}

bool MIParser::parseFPImmediateOperand(MachineOperand &Dest) {
  auto Loc = Token.location();
  lex();
  if (Token.isNot(MIToken::FloatingPointLiteral))
    return error("expected a floating point literal");
  const Constant *C = nullptr;
  if (parseIRConstant(Loc, C))
    return true;
  Dest = MachineOperand::CreateFPImm(cast<ConstantFP>(C));
  return false;
}

bool MIParser::getUnsigned(unsigned &Result) {
  assert(Token.hasIntegerValue() && "Expected a token with an integer value");
  const uint64_t Limit = uint64_t(std::numeric_limits<unsigned>::max()) + 1;
  uint64_t Val64 = Token.integerValue().getLimitedValue(Limit);
  if (Val64 == Limit)
    return error("expected 32-bit integer (too large)");
  Result = Val64;
  return false;
}

bool MIParser::parseMBBReference(MachineBasicBlock *&MBB) {
  assert(Token.is(MIToken::MachineBasicBlock) ||
         Token.is(MIToken::MachineBasicBlockLabel));
  unsigned Number;
  if (getUnsigned(Number))
    return true;
  auto MBBInfo = PFS.MBBSlots.find(Number);
  if (MBBInfo == PFS.MBBSlots.end())
    return error(Twine("use of undefined machine basic block #") +
                 Twine(Number));
  MBB = MBBInfo->second;
  if (!Token.stringValue().empty() && Token.stringValue() != MBB->getName())
    return error(Twine("the name of machine basic block #") + Twine(Number) +
                 " isn't '" + Token.stringValue() + "'");
  return false;
}

bool MIParser::parseMBBOperand(MachineOperand &Dest) {
  MachineBasicBlock *MBB;
  if (parseMBBReference(MBB))
    return true;
  Dest = MachineOperand::CreateMBB(MBB);
  lex();
  return false;
}

bool MIParser::parseStackFrameIndex(int &FI) {
  assert(Token.is(MIToken::StackObject));
  unsigned ID;
  if (getUnsigned(ID))
    return true;
  auto ObjectInfo = PFS.StackObjectSlots.find(ID);
  if (ObjectInfo == PFS.StackObjectSlots.end())
    return error(Twine("use of undefined stack object '%stack.") + Twine(ID) +
                 "'");
  StringRef Name;
  if (const auto *Alloca =
          MF.getFrameInfo()->getObjectAllocation(ObjectInfo->second))
    Name = Alloca->getName();
  if (!Token.stringValue().empty() && Token.stringValue() != Name)
    return error(Twine("the name of the stack object '%stack.") + Twine(ID) +
                 "' isn't '" + Token.stringValue() + "'");
  lex();
  FI = ObjectInfo->second;
  return false;
}

bool MIParser::parseStackObjectOperand(MachineOperand &Dest) {
  int FI;
  if (parseStackFrameIndex(FI))
    return true;
  Dest = MachineOperand::CreateFI(FI);
  return false;
}

bool MIParser::parseFixedStackFrameIndex(int &FI) {
  assert(Token.is(MIToken::FixedStackObject));
  unsigned ID;
  if (getUnsigned(ID))
    return true;
  auto ObjectInfo = PFS.FixedStackObjectSlots.find(ID);
  if (ObjectInfo == PFS.FixedStackObjectSlots.end())
    return error(Twine("use of undefined fixed stack object '%fixed-stack.") +
                 Twine(ID) + "'");
  lex();
  FI = ObjectInfo->second;
  return false;
}

bool MIParser::parseFixedStackObjectOperand(MachineOperand &Dest) {
  int FI;
  if (parseFixedStackFrameIndex(FI))
    return true;
  Dest = MachineOperand::CreateFI(FI);
  return false;
}

bool MIParser::parseGlobalValue(GlobalValue *&GV) {
  switch (Token.kind()) {
  case MIToken::NamedGlobalValue: {
    const Module *M = MF.getFunction()->getParent();
    GV = M->getNamedValue(Token.stringValue());
    if (!GV)
      return error(Twine("use of undefined global value '") + Token.range() +
                   "'");
    break;
  }
  case MIToken::GlobalValue: {
    unsigned GVIdx;
    if (getUnsigned(GVIdx))
      return true;
    if (GVIdx >= IRSlots.GlobalValues.size())
      return error(Twine("use of undefined global value '@") + Twine(GVIdx) +
                   "'");
    GV = IRSlots.GlobalValues[GVIdx];
    break;
  }
  default:
    llvm_unreachable("The current token should be a global value");
  }
  return false;
}

bool MIParser::parseGlobalAddressOperand(MachineOperand &Dest) {
  GlobalValue *GV = nullptr;
  if (parseGlobalValue(GV))
    return true;
  lex();
  Dest = MachineOperand::CreateGA(GV, /*Offset=*/0);
  if (parseOperandsOffset(Dest))
    return true;
  return false;
}

bool MIParser::parseConstantPoolIndexOperand(MachineOperand &Dest) {
  assert(Token.is(MIToken::ConstantPoolItem));
  unsigned ID;
  if (getUnsigned(ID))
    return true;
  auto ConstantInfo = PFS.ConstantPoolSlots.find(ID);
  if (ConstantInfo == PFS.ConstantPoolSlots.end())
    return error("use of undefined constant '%const." + Twine(ID) + "'");
  lex();
  Dest = MachineOperand::CreateCPI(ID, /*Offset=*/0);
  if (parseOperandsOffset(Dest))
    return true;
  return false;
}

bool MIParser::parseJumpTableIndexOperand(MachineOperand &Dest) {
  assert(Token.is(MIToken::JumpTableIndex));
  unsigned ID;
  if (getUnsigned(ID))
    return true;
  auto JumpTableEntryInfo = PFS.JumpTableSlots.find(ID);
  if (JumpTableEntryInfo == PFS.JumpTableSlots.end())
    return error("use of undefined jump table '%jump-table." + Twine(ID) + "'");
  lex();
  Dest = MachineOperand::CreateJTI(JumpTableEntryInfo->second);
  return false;
}

bool MIParser::parseExternalSymbolOperand(MachineOperand &Dest) {
  assert(Token.is(MIToken::ExternalSymbol));
  const char *Symbol = MF.createExternalSymbolName(Token.stringValue());
  lex();
  Dest = MachineOperand::CreateES(Symbol);
  if (parseOperandsOffset(Dest))
    return true;
  return false;
}

bool MIParser::parseMDNode(MDNode *&Node) {
  assert(Token.is(MIToken::exclaim));
  auto Loc = Token.location();
  lex();
  if (Token.isNot(MIToken::IntegerLiteral) || Token.integerValue().isSigned())
    return error("expected metadata id after '!'");
  unsigned ID;
  if (getUnsigned(ID))
    return true;
  auto NodeInfo = IRSlots.MetadataNodes.find(ID);
  if (NodeInfo == IRSlots.MetadataNodes.end())
    return error(Loc, "use of undefined metadata '!" + Twine(ID) + "'");
  lex();
  Node = NodeInfo->second.get();
  return false;
}

bool MIParser::parseMetadataOperand(MachineOperand &Dest) {
  MDNode *Node = nullptr;
  if (parseMDNode(Node))
    return true;
  Dest = MachineOperand::CreateMetadata(Node);
  return false;
}

bool MIParser::parseCFIOffset(int &Offset) {
  if (Token.isNot(MIToken::IntegerLiteral))
    return error("expected a cfi offset");
  if (Token.integerValue().getMinSignedBits() > 32)
    return error("expected a 32 bit integer (the cfi offset is too large)");
  Offset = (int)Token.integerValue().getExtValue();
  lex();
  return false;
}

bool MIParser::parseCFIRegister(unsigned &Reg) {
  if (Token.isNot(MIToken::NamedRegister))
    return error("expected a cfi register");
  unsigned LLVMReg;
  if (parseRegister(LLVMReg))
    return true;
  const auto *TRI = MF.getSubtarget().getRegisterInfo();
  assert(TRI && "Expected target register info");
  int DwarfReg = TRI->getDwarfRegNum(LLVMReg, true);
  if (DwarfReg < 0)
    return error("invalid DWARF register");
  Reg = (unsigned)DwarfReg;
  lex();
  return false;
}

bool MIParser::parseCFIOperand(MachineOperand &Dest) {
  auto Kind = Token.kind();
  lex();
  auto &MMI = MF.getMMI();
  int Offset;
  unsigned Reg;
  unsigned CFIIndex;
  switch (Kind) {
  case MIToken::kw_cfi_same_value:
    if (parseCFIRegister(Reg))
      return true;
    CFIIndex =
        MMI.addFrameInst(MCCFIInstruction::createSameValue(nullptr, Reg));
    break;
  case MIToken::kw_cfi_offset:
    if (parseCFIRegister(Reg) || expectAndConsume(MIToken::comma) ||
        parseCFIOffset(Offset))
      return true;
    CFIIndex =
        MMI.addFrameInst(MCCFIInstruction::createOffset(nullptr, Reg, Offset));
    break;
  case MIToken::kw_cfi_def_cfa_register:
    if (parseCFIRegister(Reg))
      return true;
    CFIIndex =
        MMI.addFrameInst(MCCFIInstruction::createDefCfaRegister(nullptr, Reg));
    break;
  case MIToken::kw_cfi_def_cfa_offset:
    if (parseCFIOffset(Offset))
      return true;
    // NB: MCCFIInstruction::createDefCfaOffset negates the offset.
    CFIIndex = MMI.addFrameInst(
        MCCFIInstruction::createDefCfaOffset(nullptr, -Offset));
    break;
  case MIToken::kw_cfi_def_cfa:
    if (parseCFIRegister(Reg) || expectAndConsume(MIToken::comma) ||
        parseCFIOffset(Offset))
      return true;
    // NB: MCCFIInstruction::createDefCfa negates the offset.
    CFIIndex =
        MMI.addFrameInst(MCCFIInstruction::createDefCfa(nullptr, Reg, -Offset));
    break;
  default:
    // TODO: Parse the other CFI operands.
    llvm_unreachable("The current token should be a cfi operand");
  }
  Dest = MachineOperand::CreateCFIIndex(CFIIndex);
  return false;
}

bool MIParser::parseIRBlock(BasicBlock *&BB, const Function &F) {
  switch (Token.kind()) {
  case MIToken::NamedIRBlock: {
    BB = dyn_cast_or_null<BasicBlock>(
        F.getValueSymbolTable().lookup(Token.stringValue()));
    if (!BB)
      return error(Twine("use of undefined IR block '") + Token.range() + "'");
    break;
  }
  case MIToken::IRBlock: {
    unsigned SlotNumber = 0;
    if (getUnsigned(SlotNumber))
      return true;
    BB = const_cast<BasicBlock *>(getIRBlock(SlotNumber, F));
    if (!BB)
      return error(Twine("use of undefined IR block '%ir-block.") +
                   Twine(SlotNumber) + "'");
    break;
  }
  default:
    llvm_unreachable("The current token should be an IR block reference");
  }
  return false;
}

bool MIParser::parseBlockAddressOperand(MachineOperand &Dest) {
  assert(Token.is(MIToken::kw_blockaddress));
  lex();
  if (expectAndConsume(MIToken::lparen))
    return true;
  if (Token.isNot(MIToken::GlobalValue) &&
      Token.isNot(MIToken::NamedGlobalValue))
    return error("expected a global value");
  GlobalValue *GV = nullptr;
  if (parseGlobalValue(GV))
    return true;
  auto *F = dyn_cast<Function>(GV);
  if (!F)
    return error("expected an IR function reference");
  lex();
  if (expectAndConsume(MIToken::comma))
    return true;
  BasicBlock *BB = nullptr;
  if (Token.isNot(MIToken::IRBlock) && Token.isNot(MIToken::NamedIRBlock))
    return error("expected an IR block reference");
  if (parseIRBlock(BB, *F))
    return true;
  lex();
  if (expectAndConsume(MIToken::rparen))
    return true;
  Dest = MachineOperand::CreateBA(BlockAddress::get(F, BB), /*Offset=*/0);
  if (parseOperandsOffset(Dest))
    return true;
  return false;
}

bool MIParser::parseTargetIndexOperand(MachineOperand &Dest) {
  assert(Token.is(MIToken::kw_target_index));
  lex();
  if (expectAndConsume(MIToken::lparen))
    return true;
  if (Token.isNot(MIToken::Identifier))
    return error("expected the name of the target index");
  int Index = 0;
  if (getTargetIndex(Token.stringValue(), Index))
    return error("use of undefined target index '" + Token.stringValue() + "'");
  lex();
  if (expectAndConsume(MIToken::rparen))
    return true;
  Dest = MachineOperand::CreateTargetIndex(unsigned(Index), /*Offset=*/0);
  if (parseOperandsOffset(Dest))
    return true;
  return false;
}

bool MIParser::parseLiveoutRegisterMaskOperand(MachineOperand &Dest) {
  assert(Token.is(MIToken::kw_liveout));
  const auto *TRI = MF.getSubtarget().getRegisterInfo();
  assert(TRI && "Expected target register info");
  uint32_t *Mask = MF.allocateRegisterMask(TRI->getNumRegs());
  lex();
  if (expectAndConsume(MIToken::lparen))
    return true;
  while (true) {
    if (Token.isNot(MIToken::NamedRegister))
      return error("expected a named register");
    unsigned Reg = 0;
    if (parseRegister(Reg))
      return true;
    lex();
    Mask[Reg / 32] |= 1U << (Reg % 32);
    // TODO: Report an error if the same register is used more than once.
    if (Token.isNot(MIToken::comma))
      break;
    lex();
  }
  if (expectAndConsume(MIToken::rparen))
    return true;
  Dest = MachineOperand::CreateRegLiveOut(Mask);
  return false;
}

bool MIParser::parseMachineOperand(MachineOperand &Dest,
                                   Optional<unsigned> &TiedDefIdx) {
  switch (Token.kind()) {
  case MIToken::kw_implicit:
  case MIToken::kw_implicit_define:
  case MIToken::kw_def:
  case MIToken::kw_dead:
  case MIToken::kw_killed:
  case MIToken::kw_undef:
  case MIToken::kw_internal:
  case MIToken::kw_early_clobber:
  case MIToken::kw_debug_use:
  case MIToken::underscore:
  case MIToken::NamedRegister:
  case MIToken::VirtualRegister:
    return parseRegisterOperand(Dest, TiedDefIdx);
  case MIToken::IntegerLiteral:
    return parseImmediateOperand(Dest);
  case MIToken::IntegerType:
    return parseTypedImmediateOperand(Dest);
  case MIToken::kw_half:
  case MIToken::kw_float:
  case MIToken::kw_double:
  case MIToken::kw_x86_fp80:
  case MIToken::kw_fp128:
  case MIToken::kw_ppc_fp128:
    return parseFPImmediateOperand(Dest);
  case MIToken::MachineBasicBlock:
    return parseMBBOperand(Dest);
  case MIToken::StackObject:
    return parseStackObjectOperand(Dest);
  case MIToken::FixedStackObject:
    return parseFixedStackObjectOperand(Dest);
  case MIToken::GlobalValue:
  case MIToken::NamedGlobalValue:
    return parseGlobalAddressOperand(Dest);
  case MIToken::ConstantPoolItem:
    return parseConstantPoolIndexOperand(Dest);
  case MIToken::JumpTableIndex:
    return parseJumpTableIndexOperand(Dest);
  case MIToken::ExternalSymbol:
    return parseExternalSymbolOperand(Dest);
  case MIToken::exclaim:
    return parseMetadataOperand(Dest);
  case MIToken::kw_cfi_same_value:
  case MIToken::kw_cfi_offset:
  case MIToken::kw_cfi_def_cfa_register:
  case MIToken::kw_cfi_def_cfa_offset:
  case MIToken::kw_cfi_def_cfa:
    return parseCFIOperand(Dest);
  case MIToken::kw_blockaddress:
    return parseBlockAddressOperand(Dest);
  case MIToken::kw_target_index:
    return parseTargetIndexOperand(Dest);
  case MIToken::kw_liveout:
    return parseLiveoutRegisterMaskOperand(Dest);
  case MIToken::Error:
    return true;
  case MIToken::Identifier:
    if (const auto *RegMask = getRegMask(Token.stringValue())) {
      Dest = MachineOperand::CreateRegMask(RegMask);
      lex();
      break;
    }
  // fallthrough
  default:
    // FIXME: Parse the MCSymbol machine operand.
    return error("expected a machine operand");
  }
  return false;
}

bool MIParser::parseMachineOperandAndTargetFlags(
    MachineOperand &Dest, Optional<unsigned> &TiedDefIdx) {
  unsigned TF = 0;
  bool HasTargetFlags = false;
  if (Token.is(MIToken::kw_target_flags)) {
    HasTargetFlags = true;
    lex();
    if (expectAndConsume(MIToken::lparen))
      return true;
    if (Token.isNot(MIToken::Identifier))
      return error("expected the name of the target flag");
    if (getDirectTargetFlag(Token.stringValue(), TF)) {
      if (getBitmaskTargetFlag(Token.stringValue(), TF))
        return error("use of undefined target flag '" + Token.stringValue() +
                     "'");
    }
    lex();
    while (Token.is(MIToken::comma)) {
      lex();
      if (Token.isNot(MIToken::Identifier))
        return error("expected the name of the target flag");
      unsigned BitFlag = 0;
      if (getBitmaskTargetFlag(Token.stringValue(), BitFlag))
        return error("use of undefined target flag '" + Token.stringValue() +
                     "'");
      // TODO: Report an error when using a duplicate bit target flag.
      TF |= BitFlag;
      lex();
    }
    if (expectAndConsume(MIToken::rparen))
      return true;
  }
  auto Loc = Token.location();
  if (parseMachineOperand(Dest, TiedDefIdx))
    return true;
  if (!HasTargetFlags)
    return false;
  if (Dest.isReg())
    return error(Loc, "register operands can't have target flags");
  Dest.setTargetFlags(TF);
  return false;
}

bool MIParser::parseOffset(int64_t &Offset) {
  if (Token.isNot(MIToken::plus) && Token.isNot(MIToken::minus))
    return false;
  StringRef Sign = Token.range();
  bool IsNegative = Token.is(MIToken::minus);
  lex();
  if (Token.isNot(MIToken::IntegerLiteral))
    return error("expected an integer literal after '" + Sign + "'");
  if (Token.integerValue().getMinSignedBits() > 64)
    return error("expected 64-bit integer (too large)");
  Offset = Token.integerValue().getExtValue();
  if (IsNegative)
    Offset = -Offset;
  lex();
  return false;
}

bool MIParser::parseAlignment(unsigned &Alignment) {
  assert(Token.is(MIToken::kw_align));
  lex();
  if (Token.isNot(MIToken::IntegerLiteral) || Token.integerValue().isSigned())
    return error("expected an integer literal after 'align'");
  if (getUnsigned(Alignment))
    return true;
  lex();
  return false;
}

bool MIParser::parseOperandsOffset(MachineOperand &Op) {
  int64_t Offset = 0;
  if (parseOffset(Offset))
    return true;
  Op.setOffset(Offset);
  return false;
}

bool MIParser::parseIRValue(const Value *&V) {
  switch (Token.kind()) {
  case MIToken::NamedIRValue: {
    V = MF.getFunction()->getValueSymbolTable().lookup(Token.stringValue());
    break;
  }
  case MIToken::IRValue: {
    unsigned SlotNumber = 0;
    if (getUnsigned(SlotNumber))
      return true;
    V = getIRValue(SlotNumber);
    break;
  }
  case MIToken::NamedGlobalValue:
  case MIToken::GlobalValue: {
    GlobalValue *GV = nullptr;
    if (parseGlobalValue(GV))
      return true;
    V = GV;
    break;
  }
  case MIToken::QuotedIRValue: {
    const Constant *C = nullptr;
    if (parseIRConstant(Token.location(), Token.stringValue(), C))
      return true;
    V = C;
    break;
  }
  default:
    llvm_unreachable("The current token should be an IR block reference");
  }
  if (!V)
    return error(Twine("use of undefined IR value '") + Token.range() + "'");
  return false;
}

bool MIParser::getUint64(uint64_t &Result) {
  assert(Token.hasIntegerValue());
  if (Token.integerValue().getActiveBits() > 64)
    return error("expected 64-bit integer (too large)");
  Result = Token.integerValue().getZExtValue();
  return false;
}

bool MIParser::parseMemoryOperandFlag(unsigned &Flags) {
  const unsigned OldFlags = Flags;
  switch (Token.kind()) {
  case MIToken::kw_volatile:
    Flags |= MachineMemOperand::MOVolatile;
    break;
  case MIToken::kw_non_temporal:
    Flags |= MachineMemOperand::MONonTemporal;
    break;
  case MIToken::kw_invariant:
    Flags |= MachineMemOperand::MOInvariant;
    break;
  // TODO: parse the target specific memory operand flags.
  default:
    llvm_unreachable("The current token should be a memory operand flag");
  }
  if (OldFlags == Flags)
    // We know that the same flag is specified more than once when the flags
    // weren't modified.
    return error("duplicate '" + Token.stringValue() + "' memory operand flag");
  lex();
  return false;
}

bool MIParser::parseMemoryPseudoSourceValue(const PseudoSourceValue *&PSV) {
  switch (Token.kind()) {
  case MIToken::kw_stack:
    PSV = MF.getPSVManager().getStack();
    break;
  case MIToken::kw_got:
    PSV = MF.getPSVManager().getGOT();
    break;
  case MIToken::kw_jump_table:
    PSV = MF.getPSVManager().getJumpTable();
    break;
  case MIToken::kw_constant_pool:
    PSV = MF.getPSVManager().getConstantPool();
    break;
  case MIToken::FixedStackObject: {
    int FI;
    if (parseFixedStackFrameIndex(FI))
      return true;
    PSV = MF.getPSVManager().getFixedStack(FI);
    // The token was already consumed, so use return here instead of break.
    return false;
  }
  case MIToken::kw_call_entry: {
    lex();
    switch (Token.kind()) {
    case MIToken::GlobalValue:
    case MIToken::NamedGlobalValue: {
      GlobalValue *GV = nullptr;
      if (parseGlobalValue(GV))
        return true;
      PSV = MF.getPSVManager().getGlobalValueCallEntry(GV);
      break;
    }
    case MIToken::ExternalSymbol:
      PSV = MF.getPSVManager().getExternalSymbolCallEntry(
          MF.createExternalSymbolName(Token.stringValue()));
      break;
    default:
      return error(
          "expected a global value or an external symbol after 'call-entry'");
    }
    break;
  }
  default:
    llvm_unreachable("The current token should be pseudo source value");
  }
  lex();
  return false;
}

bool MIParser::parseMachinePointerInfo(MachinePointerInfo &Dest) {
  if (Token.is(MIToken::kw_constant_pool) || Token.is(MIToken::kw_stack) ||
      Token.is(MIToken::kw_got) || Token.is(MIToken::kw_jump_table) ||
      Token.is(MIToken::FixedStackObject) || Token.is(MIToken::kw_call_entry)) {
    const PseudoSourceValue *PSV = nullptr;
    if (parseMemoryPseudoSourceValue(PSV))
      return true;
    int64_t Offset = 0;
    if (parseOffset(Offset))
      return true;
    Dest = MachinePointerInfo(PSV, Offset);
    return false;
  }
  if (Token.isNot(MIToken::NamedIRValue) && Token.isNot(MIToken::IRValue) &&
      Token.isNot(MIToken::GlobalValue) &&
      Token.isNot(MIToken::NamedGlobalValue) &&
      Token.isNot(MIToken::QuotedIRValue))
    return error("expected an IR value reference");
  const Value *V = nullptr;
  if (parseIRValue(V))
    return true;
  if (!V->getType()->isPointerTy())
    return error("expected a pointer IR value");
  lex();
  int64_t Offset = 0;
  if (parseOffset(Offset))
    return true;
  Dest = MachinePointerInfo(V, Offset);
  return false;
}

bool MIParser::parseMachineMemoryOperand(MachineMemOperand *&Dest) {
  if (expectAndConsume(MIToken::lparen))
    return true;
  unsigned Flags = 0;
  while (Token.isMemoryOperandFlag()) {
    if (parseMemoryOperandFlag(Flags))
      return true;
  }
  if (Token.isNot(MIToken::Identifier) ||
      (Token.stringValue() != "load" && Token.stringValue() != "store"))
    return error("expected 'load' or 'store' memory operation");
  if (Token.stringValue() == "load")
    Flags |= MachineMemOperand::MOLoad;
  else
    Flags |= MachineMemOperand::MOStore;
  lex();

  if (Token.isNot(MIToken::IntegerLiteral))
    return error("expected the size integer literal after memory operation");
  uint64_t Size;
  if (getUint64(Size))
    return true;
  lex();

  const char *Word = Flags & MachineMemOperand::MOLoad ? "from" : "into";
  if (Token.isNot(MIToken::Identifier) || Token.stringValue() != Word)
    return error(Twine("expected '") + Word + "'");
  lex();

  MachinePointerInfo Ptr = MachinePointerInfo();
  if (parseMachinePointerInfo(Ptr))
    return true;
  unsigned BaseAlignment = Size;
  AAMDNodes AAInfo;
  MDNode *Range = nullptr;
  while (consumeIfPresent(MIToken::comma)) {
    switch (Token.kind()) {
    case MIToken::kw_align:
      if (parseAlignment(BaseAlignment))
        return true;
      break;
    case MIToken::md_tbaa:
      lex();
      if (parseMDNode(AAInfo.TBAA))
        return true;
      break;
    case MIToken::md_alias_scope:
      lex();
      if (parseMDNode(AAInfo.Scope))
        return true;
      break;
    case MIToken::md_noalias:
      lex();
      if (parseMDNode(AAInfo.NoAlias))
        return true;
      break;
    case MIToken::md_range:
      lex();
      if (parseMDNode(Range))
        return true;
      break;
    // TODO: Report an error on duplicate metadata nodes.
    default:
      return error("expected 'align' or '!tbaa' or '!alias.scope' or "
                   "'!noalias' or '!range'");
    }
  }
  if (expectAndConsume(MIToken::rparen))
    return true;
  Dest =
      MF.getMachineMemOperand(Ptr, Flags, Size, BaseAlignment, AAInfo, Range);
  return false;
}

void MIParser::initNames2InstrOpCodes() {
  if (!Names2InstrOpCodes.empty())
    return;
  const auto *TII = MF.getSubtarget().getInstrInfo();
  assert(TII && "Expected target instruction info");
  for (unsigned I = 0, E = TII->getNumOpcodes(); I < E; ++I)
    Names2InstrOpCodes.insert(std::make_pair(StringRef(TII->getName(I)), I));
}

bool MIParser::parseInstrName(StringRef InstrName, unsigned &OpCode) {
  initNames2InstrOpCodes();
  auto InstrInfo = Names2InstrOpCodes.find(InstrName);
  if (InstrInfo == Names2InstrOpCodes.end())
    return true;
  OpCode = InstrInfo->getValue();
  return false;
}

void MIParser::initNames2Regs() {
  if (!Names2Regs.empty())
    return;
  // The '%noreg' register is the register 0.
  Names2Regs.insert(std::make_pair("noreg", 0));
  const auto *TRI = MF.getSubtarget().getRegisterInfo();
  assert(TRI && "Expected target register info");
  for (unsigned I = 0, E = TRI->getNumRegs(); I < E; ++I) {
    bool WasInserted =
        Names2Regs.insert(std::make_pair(StringRef(TRI->getName(I)).lower(), I))
            .second;
    (void)WasInserted;
    assert(WasInserted && "Expected registers to be unique case-insensitively");
  }
}

bool MIParser::getRegisterByName(StringRef RegName, unsigned &Reg) {
  initNames2Regs();
  auto RegInfo = Names2Regs.find(RegName);
  if (RegInfo == Names2Regs.end())
    return true;
  Reg = RegInfo->getValue();
  return false;
}

void MIParser::initNames2RegMasks() {
  if (!Names2RegMasks.empty())
    return;
  const auto *TRI = MF.getSubtarget().getRegisterInfo();
  assert(TRI && "Expected target register info");
  ArrayRef<const uint32_t *> RegMasks = TRI->getRegMasks();
  ArrayRef<const char *> RegMaskNames = TRI->getRegMaskNames();
  assert(RegMasks.size() == RegMaskNames.size());
  for (size_t I = 0, E = RegMasks.size(); I < E; ++I)
    Names2RegMasks.insert(
        std::make_pair(StringRef(RegMaskNames[I]).lower(), RegMasks[I]));
}

const uint32_t *MIParser::getRegMask(StringRef Identifier) {
  initNames2RegMasks();
  auto RegMaskInfo = Names2RegMasks.find(Identifier);
  if (RegMaskInfo == Names2RegMasks.end())
    return nullptr;
  return RegMaskInfo->getValue();
}

void MIParser::initNames2SubRegIndices() {
  if (!Names2SubRegIndices.empty())
    return;
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
  for (unsigned I = 1, E = TRI->getNumSubRegIndices(); I < E; ++I)
    Names2SubRegIndices.insert(
        std::make_pair(StringRef(TRI->getSubRegIndexName(I)).lower(), I));
}

unsigned MIParser::getSubRegIndex(StringRef Name) {
  initNames2SubRegIndices();
  auto SubRegInfo = Names2SubRegIndices.find(Name);
  if (SubRegInfo == Names2SubRegIndices.end())
    return 0;
  return SubRegInfo->getValue();
}

static void initSlots2BasicBlocks(
    const Function &F,
    DenseMap<unsigned, const BasicBlock *> &Slots2BasicBlocks) {
  ModuleSlotTracker MST(F.getParent(), /*ShouldInitializeAllMetadata=*/false);
  MST.incorporateFunction(F);
  for (auto &BB : F) {
    if (BB.hasName())
      continue;
    int Slot = MST.getLocalSlot(&BB);
    if (Slot == -1)
      continue;
    Slots2BasicBlocks.insert(std::make_pair(unsigned(Slot), &BB));
  }
}

static const BasicBlock *getIRBlockFromSlot(
    unsigned Slot,
    const DenseMap<unsigned, const BasicBlock *> &Slots2BasicBlocks) {
  auto BlockInfo = Slots2BasicBlocks.find(Slot);
  if (BlockInfo == Slots2BasicBlocks.end())
    return nullptr;
  return BlockInfo->second;
}

const BasicBlock *MIParser::getIRBlock(unsigned Slot) {
  if (Slots2BasicBlocks.empty())
    initSlots2BasicBlocks(*MF.getFunction(), Slots2BasicBlocks);
  return getIRBlockFromSlot(Slot, Slots2BasicBlocks);
}

const BasicBlock *MIParser::getIRBlock(unsigned Slot, const Function &F) {
  if (&F == MF.getFunction())
    return getIRBlock(Slot);
  DenseMap<unsigned, const BasicBlock *> CustomSlots2BasicBlocks;
  initSlots2BasicBlocks(F, CustomSlots2BasicBlocks);
  return getIRBlockFromSlot(Slot, CustomSlots2BasicBlocks);
}

static void mapValueToSlot(const Value *V, ModuleSlotTracker &MST,
                           DenseMap<unsigned, const Value *> &Slots2Values) {
  int Slot = MST.getLocalSlot(V);
  if (Slot == -1)
    return;
  Slots2Values.insert(std::make_pair(unsigned(Slot), V));
}

/// Creates the mapping from slot numbers to function's unnamed IR values.
static void initSlots2Values(const Function &F,
                             DenseMap<unsigned, const Value *> &Slots2Values) {
  ModuleSlotTracker MST(F.getParent(), /*ShouldInitializeAllMetadata=*/false);
  MST.incorporateFunction(F);
  for (const auto &Arg : F.args())
    mapValueToSlot(&Arg, MST, Slots2Values);
  for (const auto &BB : F) {
    mapValueToSlot(&BB, MST, Slots2Values);
    for (const auto &I : BB)
      mapValueToSlot(&I, MST, Slots2Values);
  }
}

const Value *MIParser::getIRValue(unsigned Slot) {
  if (Slots2Values.empty())
    initSlots2Values(*MF.getFunction(), Slots2Values);
  auto ValueInfo = Slots2Values.find(Slot);
  if (ValueInfo == Slots2Values.end())
    return nullptr;
  return ValueInfo->second;
}

void MIParser::initNames2TargetIndices() {
  if (!Names2TargetIndices.empty())
    return;
  const auto *TII = MF.getSubtarget().getInstrInfo();
  assert(TII && "Expected target instruction info");
  auto Indices = TII->getSerializableTargetIndices();
  for (const auto &I : Indices)
    Names2TargetIndices.insert(std::make_pair(StringRef(I.second), I.first));
}

bool MIParser::getTargetIndex(StringRef Name, int &Index) {
  initNames2TargetIndices();
  auto IndexInfo = Names2TargetIndices.find(Name);
  if (IndexInfo == Names2TargetIndices.end())
    return true;
  Index = IndexInfo->second;
  return false;
}

void MIParser::initNames2DirectTargetFlags() {
  if (!Names2DirectTargetFlags.empty())
    return;
  const auto *TII = MF.getSubtarget().getInstrInfo();
  assert(TII && "Expected target instruction info");
  auto Flags = TII->getSerializableDirectMachineOperandTargetFlags();
  for (const auto &I : Flags)
    Names2DirectTargetFlags.insert(
        std::make_pair(StringRef(I.second), I.first));
}

bool MIParser::getDirectTargetFlag(StringRef Name, unsigned &Flag) {
  initNames2DirectTargetFlags();
  auto FlagInfo = Names2DirectTargetFlags.find(Name);
  if (FlagInfo == Names2DirectTargetFlags.end())
    return true;
  Flag = FlagInfo->second;
  return false;
}

void MIParser::initNames2BitmaskTargetFlags() {
  if (!Names2BitmaskTargetFlags.empty())
    return;
  const auto *TII = MF.getSubtarget().getInstrInfo();
  assert(TII && "Expected target instruction info");
  auto Flags = TII->getSerializableBitmaskMachineOperandTargetFlags();
  for (const auto &I : Flags)
    Names2BitmaskTargetFlags.insert(
        std::make_pair(StringRef(I.second), I.first));
}

bool MIParser::getBitmaskTargetFlag(StringRef Name, unsigned &Flag) {
  initNames2BitmaskTargetFlags();
  auto FlagInfo = Names2BitmaskTargetFlags.find(Name);
  if (FlagInfo == Names2BitmaskTargetFlags.end())
    return true;
  Flag = FlagInfo->second;
  return false;
}

bool llvm::parseMachineBasicBlockDefinitions(MachineFunction &MF, StringRef Src,
                                             PerFunctionMIParsingState &PFS,
                                             const SlotMapping &IRSlots,
                                             SMDiagnostic &Error) {
  SourceMgr SM;
  SM.AddNewSourceBuffer(
      MemoryBuffer::getMemBuffer(Src, "", /*RequiresNullTerminator=*/false),
      SMLoc());
  return MIParser(SM, MF, Error, Src, PFS, IRSlots)
      .parseBasicBlockDefinitions(PFS.MBBSlots);
}

bool llvm::parseMachineInstructions(MachineFunction &MF, StringRef Src,
                                    const PerFunctionMIParsingState &PFS,
                                    const SlotMapping &IRSlots,
                                    SMDiagnostic &Error) {
  SourceMgr SM;
  SM.AddNewSourceBuffer(
      MemoryBuffer::getMemBuffer(Src, "", /*RequiresNullTerminator=*/false),
      SMLoc());
  return MIParser(SM, MF, Error, Src, PFS, IRSlots).parseBasicBlocks();
}

bool llvm::parseMBBReference(MachineBasicBlock *&MBB, SourceMgr &SM,
                             MachineFunction &MF, StringRef Src,
                             const PerFunctionMIParsingState &PFS,
                             const SlotMapping &IRSlots, SMDiagnostic &Error) {
  return MIParser(SM, MF, Error, Src, PFS, IRSlots).parseStandaloneMBB(MBB);
}

bool llvm::parseNamedRegisterReference(unsigned &Reg, SourceMgr &SM,
                                       MachineFunction &MF, StringRef Src,
                                       const PerFunctionMIParsingState &PFS,
                                       const SlotMapping &IRSlots,
                                       SMDiagnostic &Error) {
  return MIParser(SM, MF, Error, Src, PFS, IRSlots)
      .parseStandaloneNamedRegister(Reg);
}

bool llvm::parseVirtualRegisterReference(unsigned &Reg, SourceMgr &SM,
                                         MachineFunction &MF, StringRef Src,
                                         const PerFunctionMIParsingState &PFS,
                                         const SlotMapping &IRSlots,
                                         SMDiagnostic &Error) {
  return MIParser(SM, MF, Error, Src, PFS, IRSlots)
      .parseStandaloneVirtualRegister(Reg);
}

bool llvm::parseStackObjectReference(int &FI, SourceMgr &SM,
                                     MachineFunction &MF, StringRef Src,
                                     const PerFunctionMIParsingState &PFS,
                                     const SlotMapping &IRSlots,
                                     SMDiagnostic &Error) {
  return MIParser(SM, MF, Error, Src, PFS, IRSlots)
      .parseStandaloneStackObject(FI);
}

bool llvm::parseMDNode(MDNode *&Node, SourceMgr &SM, MachineFunction &MF,
                       StringRef Src, const PerFunctionMIParsingState &PFS,
                       const SlotMapping &IRSlots, SMDiagnostic &Error) {
  return MIParser(SM, MF, Error, Src, PFS, IRSlots).parseStandaloneMDNode(Node);
}
