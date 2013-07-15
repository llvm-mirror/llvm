//===- BitstreamReader.h - Low-level bitstream reader interface -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This header defines the BitstreamReader class.  This class can be used to
// read an arbitrary bitstream, regardless of its contents.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_BITCODE_BITSTREAMREADER_H
#define LLVM_BITCODE_BITSTREAMREADER_H

#include "llvm/ADT/OwningPtr.h"
#include "llvm/Bitcode/BitCodes.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/StreamableMemoryObject.h"
#include <climits>
#include <string>
#include <vector>

namespace llvm {

  class Deserializer;

/// BitstreamReader - This class is used to read from an LLVM bitcode stream,
/// maintaining information that is global to decoding the entire file.  While
/// a file is being read, multiple cursors can be independently advanced or
/// skipped around within the file.  These are represented by the
/// BitstreamCursor class.
class BitstreamReader {
public:
  /// BlockInfo - This contains information emitted to BLOCKINFO_BLOCK blocks.
  /// These describe abbreviations that all blocks of the specified ID inherit.
  struct BlockInfo {
    unsigned BlockID;
    std::vector<BitCodeAbbrev*> Abbrevs;
    std::string Name;

    std::vector<std::pair<unsigned, std::string> > RecordNames;
  };
private:
  OwningPtr<StreamableMemoryObject> BitcodeBytes;

  std::vector<BlockInfo> BlockInfoRecords;

  /// IgnoreBlockInfoNames - This is set to true if we don't care about the
  /// block/record name information in the BlockInfo block. Only llvm-bcanalyzer
  /// uses this.
  bool IgnoreBlockInfoNames;

  BitstreamReader(const BitstreamReader&) LLVM_DELETED_FUNCTION;
  void operator=(const BitstreamReader&) LLVM_DELETED_FUNCTION;
public:
  BitstreamReader() : IgnoreBlockInfoNames(true) {
  }

  BitstreamReader(const unsigned char *Start, const unsigned char *End) {
    IgnoreBlockInfoNames = true;
    init(Start, End);
  }

  BitstreamReader(StreamableMemoryObject *bytes) {
    BitcodeBytes.reset(bytes);
  }

  void init(const unsigned char *Start, const unsigned char *End) {
    assert(((End-Start) & 3) == 0 &&"Bitcode stream not a multiple of 4 bytes");
    BitcodeBytes.reset(getNonStreamedMemoryObject(Start, End));
  }

  StreamableMemoryObject &getBitcodeBytes() { return *BitcodeBytes; }

  ~BitstreamReader() {
    // Free the BlockInfoRecords.
    while (!BlockInfoRecords.empty()) {
      BlockInfo &Info = BlockInfoRecords.back();
      // Free blockinfo abbrev info.
      for (unsigned i = 0, e = static_cast<unsigned>(Info.Abbrevs.size());
           i != e; ++i)
        Info.Abbrevs[i]->dropRef();
      BlockInfoRecords.pop_back();
    }
  }

  /// CollectBlockInfoNames - This is called by clients that want block/record
  /// name information.
  void CollectBlockInfoNames() { IgnoreBlockInfoNames = false; }
  bool isIgnoringBlockInfoNames() { return IgnoreBlockInfoNames; }

  //===--------------------------------------------------------------------===//
  // Block Manipulation
  //===--------------------------------------------------------------------===//

  /// hasBlockInfoRecords - Return true if we've already read and processed the
  /// block info block for this Bitstream.  We only process it for the first
  /// cursor that walks over it.
  bool hasBlockInfoRecords() const { return !BlockInfoRecords.empty(); }

  /// getBlockInfo - If there is block info for the specified ID, return it,
  /// otherwise return null.
  const BlockInfo *getBlockInfo(unsigned BlockID) const {
    // Common case, the most recent entry matches BlockID.
    if (!BlockInfoRecords.empty() && BlockInfoRecords.back().BlockID == BlockID)
      return &BlockInfoRecords.back();

    for (unsigned i = 0, e = static_cast<unsigned>(BlockInfoRecords.size());
         i != e; ++i)
      if (BlockInfoRecords[i].BlockID == BlockID)
        return &BlockInfoRecords[i];
    return 0;
  }

  BlockInfo &getOrCreateBlockInfo(unsigned BlockID) {
    if (const BlockInfo *BI = getBlockInfo(BlockID))
      return *const_cast<BlockInfo*>(BI);

    // Otherwise, add a new record.
    BlockInfoRecords.push_back(BlockInfo());
    BlockInfoRecords.back().BlockID = BlockID;
    return BlockInfoRecords.back();
  }
};


/// BitstreamEntry - When advancing through a bitstream cursor, each advance can
/// discover a few different kinds of entries:
///   Error    - Malformed bitcode was found.
///   EndBlock - We've reached the end of the current block, (or the end of the
///              file, which is treated like a series of EndBlock records.
///   SubBlock - This is the start of a new subblock of a specific ID.
///   Record   - This is a record with a specific AbbrevID.
///
struct BitstreamEntry {
  enum {
    Error,
    EndBlock,
    SubBlock,
    Record
  } Kind;

  unsigned ID;

  static BitstreamEntry getError() {
    BitstreamEntry E; E.Kind = Error; return E;
  }
  static BitstreamEntry getEndBlock() {
    BitstreamEntry E; E.Kind = EndBlock; return E;
  }
  static BitstreamEntry getSubBlock(unsigned ID) {
    BitstreamEntry E; E.Kind = SubBlock; E.ID = ID; return E;
  }
  static BitstreamEntry getRecord(unsigned AbbrevID) {
    BitstreamEntry E; E.Kind = Record; E.ID = AbbrevID; return E;
  }
};

/// BitstreamCursor - This represents a position within a bitcode file.  There
/// may be multiple independent cursors reading within one bitstream, each
/// maintaining their own local state.
///
/// Unlike iterators, BitstreamCursors are heavy-weight objects that should not
/// be passed by value.
class BitstreamCursor {
  friend class Deserializer;
  BitstreamReader *BitStream;
  size_t NextChar;


  /// CurWord/word_t - This is the current data we have pulled from the stream
  /// but have not returned to the client.  This is specifically and
  /// intentionally defined to follow the word size of the host machine for
  /// efficiency.  We use word_t in places that are aware of this to make it
  /// perfectly explicit what is going on.
  typedef uint32_t word_t;
  word_t CurWord;

  /// BitsInCurWord - This is the number of bits in CurWord that are valid. This
  /// is always from [0...31/63] inclusive (depending on word size).
  unsigned BitsInCurWord;

  // CurCodeSize - This is the declared size of code values used for the current
  // block, in bits.
  unsigned CurCodeSize;

  /// CurAbbrevs - Abbrevs installed at in this block.
  std::vector<BitCodeAbbrev*> CurAbbrevs;

  struct Block {
    unsigned PrevCodeSize;
    std::vector<BitCodeAbbrev*> PrevAbbrevs;
    explicit Block(unsigned PCS) : PrevCodeSize(PCS) {}
  };

  /// BlockScope - This tracks the codesize of parent blocks.
  SmallVector<Block, 8> BlockScope;


public:
  BitstreamCursor() : BitStream(0), NextChar(0) {
  }
  BitstreamCursor(const BitstreamCursor &RHS) : BitStream(0), NextChar(0) {
    operator=(RHS);
  }

  explicit BitstreamCursor(BitstreamReader &R) : BitStream(&R) {
    NextChar = 0;
    CurWord = 0;
    BitsInCurWord = 0;
    CurCodeSize = 2;
  }

  void init(BitstreamReader &R) {
    freeState();

    BitStream = &R;
    NextChar = 0;
    CurWord = 0;
    BitsInCurWord = 0;
    CurCodeSize = 2;
  }

  ~BitstreamCursor() {
    freeState();
  }

  void operator=(const BitstreamCursor &RHS);

  void freeState();

  bool isEndPos(size_t pos) {
    return BitStream->getBitcodeBytes().isObjectEnd(static_cast<uint64_t>(pos));
  }

  bool canSkipToPos(size_t pos) const {
    // pos can be skipped to if it is a valid address or one byte past the end.
    return pos == 0 || BitStream->getBitcodeBytes().isValidAddress(
        static_cast<uint64_t>(pos - 1));
  }

  uint32_t getWord(size_t pos) {
    uint8_t buf[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
    BitStream->getBitcodeBytes().readBytes(pos, sizeof(buf), buf);
    return *reinterpret_cast<support::ulittle32_t *>(buf);
  }

  bool AtEndOfStream() {
    return BitsInCurWord == 0 && isEndPos(NextChar);
  }

  /// getAbbrevIDWidth - Return the number of bits used to encode an abbrev #.
  unsigned getAbbrevIDWidth() const { return CurCodeSize; }

  /// GetCurrentBitNo - Return the bit # of the bit we are reading.
  uint64_t GetCurrentBitNo() const {
    return NextChar*CHAR_BIT - BitsInCurWord;
  }

  BitstreamReader *getBitStreamReader() {
    return BitStream;
  }
  const BitstreamReader *getBitStreamReader() const {
    return BitStream;
  }

  /// Flags that modify the behavior of advance().
  enum {
    /// AF_DontPopBlockAtEnd - If this flag is used, the advance() method does
    /// not automatically pop the block scope when the end of a block is
    /// reached.
    AF_DontPopBlockAtEnd = 1,

    /// AF_DontAutoprocessAbbrevs - If this flag is used, abbrev entries are
    /// returned just like normal records.
    AF_DontAutoprocessAbbrevs = 2
  };

  /// advance - Advance the current bitstream, returning the next entry in the
  /// stream.
  BitstreamEntry advance(unsigned Flags = 0) {
    while (1) {
      unsigned Code = ReadCode();
      if (Code == bitc::END_BLOCK) {
        // Pop the end of the block unless Flags tells us not to.
        if (!(Flags & AF_DontPopBlockAtEnd) && ReadBlockEnd())
          return BitstreamEntry::getError();
        return BitstreamEntry::getEndBlock();
      }

      if (Code == bitc::ENTER_SUBBLOCK)
        return BitstreamEntry::getSubBlock(ReadSubBlockID());

      if (Code == bitc::DEFINE_ABBREV &&
          !(Flags & AF_DontAutoprocessAbbrevs)) {
        // We read and accumulate abbrev's, the client can't do anything with
        // them anyway.
        ReadAbbrevRecord();
        continue;
      }

      return BitstreamEntry::getRecord(Code);
    }
  }

  /// advanceSkippingSubblocks - This is a convenience function for clients that
  /// don't expect any subblocks.  This just skips over them automatically.
  BitstreamEntry advanceSkippingSubblocks(unsigned Flags = 0) {
    while (1) {
      // If we found a normal entry, return it.
      BitstreamEntry Entry = advance(Flags);
      if (Entry.Kind != BitstreamEntry::SubBlock)
        return Entry;

      // If we found a sub-block, just skip over it and check the next entry.
      if (SkipBlock())
        return BitstreamEntry::getError();
    }
  }

  /// JumpToBit - Reset the stream to the specified bit number.
  void JumpToBit(uint64_t BitNo) {
    uintptr_t ByteNo = uintptr_t(BitNo/8) & ~(sizeof(word_t)-1);
    unsigned WordBitNo = unsigned(BitNo & (sizeof(word_t)*8-1));
    assert(canSkipToPos(ByteNo) && "Invalid location");

    // Move the cursor to the right word.
    NextChar = ByteNo;
    BitsInCurWord = 0;
    CurWord = 0;

    // Skip over any bits that are already consumed.
    if (WordBitNo) {
      if (sizeof(word_t) > 4)
        Read64(WordBitNo);
      else
        Read(WordBitNo);
    }
  }


  uint32_t Read(unsigned NumBits) {
    assert(NumBits && NumBits <= 32 &&
           "Cannot return zero or more than 32 bits!");

    // If the field is fully contained by CurWord, return it quickly.
    if (BitsInCurWord >= NumBits) {
      uint32_t R = uint32_t(CurWord) & (~0U >> (32-NumBits));
      CurWord >>= NumBits;
      BitsInCurWord -= NumBits;
      return R;
    }

    // If we run out of data, stop at the end of the stream.
    if (isEndPos(NextChar)) {
      CurWord = 0;
      BitsInCurWord = 0;
      return 0;
    }

    uint32_t R = uint32_t(CurWord);

    // Read the next word from the stream.
    uint8_t Array[sizeof(word_t)] = {0};

    BitStream->getBitcodeBytes().readBytes(NextChar, sizeof(Array), Array);

    // Handle big-endian byte-swapping if necessary.
    support::detail::packed_endian_specific_integral
      <word_t, support::little, support::unaligned> EndianValue;
    memcpy(&EndianValue, Array, sizeof(Array));

    CurWord = EndianValue;

    NextChar += sizeof(word_t);

    // Extract NumBits-BitsInCurWord from what we just read.
    unsigned BitsLeft = NumBits-BitsInCurWord;

    // Be careful here, BitsLeft is in the range [1..32]/[1..64] inclusive.
    R |= uint32_t((CurWord & (word_t(~0ULL) >> (sizeof(word_t)*8-BitsLeft)))
                    << BitsInCurWord);

    // BitsLeft bits have just been used up from CurWord.  BitsLeft is in the
    // range [1..32]/[1..64] so be careful how we shift.
    if (BitsLeft != sizeof(word_t)*8)
      CurWord >>= BitsLeft;
    else
      CurWord = 0;
    BitsInCurWord = sizeof(word_t)*8-BitsLeft;
    return R;
  }

  uint64_t Read64(unsigned NumBits) {
    if (NumBits <= 32) return Read(NumBits);

    uint64_t V = Read(32);
    return V | (uint64_t)Read(NumBits-32) << 32;
  }

  uint32_t ReadVBR(unsigned NumBits) {
    uint32_t Piece = Read(NumBits);
    if ((Piece & (1U << (NumBits-1))) == 0)
      return Piece;

    uint32_t Result = 0;
    unsigned NextBit = 0;
    while (1) {
      Result |= (Piece & ((1U << (NumBits-1))-1)) << NextBit;

      if ((Piece & (1U << (NumBits-1))) == 0)
        return Result;

      NextBit += NumBits-1;
      Piece = Read(NumBits);
    }
  }

  // ReadVBR64 - Read a VBR that may have a value up to 64-bits in size.  The
  // chunk size of the VBR must still be <= 32 bits though.
  uint64_t ReadVBR64(unsigned NumBits) {
    uint32_t Piece = Read(NumBits);
    if ((Piece & (1U << (NumBits-1))) == 0)
      return uint64_t(Piece);

    uint64_t Result = 0;
    unsigned NextBit = 0;
    while (1) {
      Result |= uint64_t(Piece & ((1U << (NumBits-1))-1)) << NextBit;

      if ((Piece & (1U << (NumBits-1))) == 0)
        return Result;

      NextBit += NumBits-1;
      Piece = Read(NumBits);
    }
  }

private:
  void SkipToFourByteBoundary() {
    // If word_t is 64-bits and if we've read less than 32 bits, just dump
    // the bits we have up to the next 32-bit boundary.
    if (sizeof(word_t) > 4 &&
        BitsInCurWord >= 32) {
      CurWord >>= BitsInCurWord-32;
      BitsInCurWord = 32;
      return;
    }

    BitsInCurWord = 0;
    CurWord = 0;
  }
public:

  unsigned ReadCode() {
    return Read(CurCodeSize);
  }


  // Block header:
  //    [ENTER_SUBBLOCK, blockid, newcodelen, <align4bytes>, blocklen]

  /// ReadSubBlockID - Having read the ENTER_SUBBLOCK code, read the BlockID for
  /// the block.
  unsigned ReadSubBlockID() {
    return ReadVBR(bitc::BlockIDWidth);
  }

  /// SkipBlock - Having read the ENTER_SUBBLOCK abbrevid and a BlockID, skip
  /// over the body of this block.  If the block record is malformed, return
  /// true.
  bool SkipBlock() {
    // Read and ignore the codelen value.  Since we are skipping this block, we
    // don't care what code widths are used inside of it.
    ReadVBR(bitc::CodeLenWidth);
    SkipToFourByteBoundary();
    unsigned NumFourBytes = Read(bitc::BlockSizeWidth);

    // Check that the block wasn't partially defined, and that the offset isn't
    // bogus.
    size_t SkipTo = GetCurrentBitNo() + NumFourBytes*4*8;
    if (AtEndOfStream() || !canSkipToPos(SkipTo/8))
      return true;

    JumpToBit(SkipTo);
    return false;
  }

  /// EnterSubBlock - Having read the ENTER_SUBBLOCK abbrevid, enter
  /// the block, and return true if the block has an error.
  bool EnterSubBlock(unsigned BlockID, unsigned *NumWordsP = 0);

  bool ReadBlockEnd() {
    if (BlockScope.empty()) return true;

    // Block tail:
    //    [END_BLOCK, <align4bytes>]
    SkipToFourByteBoundary();

    popBlockScope();
    return false;
  }

private:

  void popBlockScope() {
    CurCodeSize = BlockScope.back().PrevCodeSize;

    // Delete abbrevs from popped scope.
    for (unsigned i = 0, e = static_cast<unsigned>(CurAbbrevs.size());
         i != e; ++i)
      CurAbbrevs[i]->dropRef();

    BlockScope.back().PrevAbbrevs.swap(CurAbbrevs);
    BlockScope.pop_back();
  }

  //===--------------------------------------------------------------------===//
  // Record Processing
  //===--------------------------------------------------------------------===//

private:
  void readAbbreviatedLiteral(const BitCodeAbbrevOp &Op,
                              SmallVectorImpl<uint64_t> &Vals);
  void readAbbreviatedField(const BitCodeAbbrevOp &Op,
                            SmallVectorImpl<uint64_t> &Vals);
  void skipAbbreviatedField(const BitCodeAbbrevOp &Op);

public:

  /// getAbbrev - Return the abbreviation for the specified AbbrevId.
  const BitCodeAbbrev *getAbbrev(unsigned AbbrevID) {
    unsigned AbbrevNo = AbbrevID-bitc::FIRST_APPLICATION_ABBREV;
    assert(AbbrevNo < CurAbbrevs.size() && "Invalid abbrev #!");
    return CurAbbrevs[AbbrevNo];
  }

  /// skipRecord - Read the current record and discard it.
  void skipRecord(unsigned AbbrevID);

  unsigned readRecord(unsigned AbbrevID, SmallVectorImpl<uint64_t> &Vals,
                      StringRef *Blob = 0);

  //===--------------------------------------------------------------------===//
  // Abbrev Processing
  //===--------------------------------------------------------------------===//
  void ReadAbbrevRecord();

  bool ReadBlockInfoBlock();
};

} // End llvm namespace

#endif
