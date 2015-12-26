//=-- InstrProfReader.h - Instrumented profiling readers ----------*- C++ -*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains support for reading profiling data for instrumentation
// based PGO and coverage.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_PROFILEDATA_INSTRPROFREADER_H
#define LLVM_PROFILEDATA_INSTRPROFREADER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ProfileData/InstrProf.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/LineIterator.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/OnDiskHashTable.h"
#include "llvm/Support/raw_ostream.h"
#include <iterator>

namespace llvm {

class InstrProfReader;

/// A file format agnostic iterator over profiling data.
class InstrProfIterator : public std::iterator<std::input_iterator_tag,
                                               InstrProfRecord> {
  InstrProfReader *Reader;
  InstrProfRecord Record;

  void Increment();
public:
  InstrProfIterator() : Reader(nullptr) {}
  InstrProfIterator(InstrProfReader *Reader) : Reader(Reader) { Increment(); }

  InstrProfIterator &operator++() { Increment(); return *this; }
  bool operator==(const InstrProfIterator &RHS) { return Reader == RHS.Reader; }
  bool operator!=(const InstrProfIterator &RHS) { return Reader != RHS.Reader; }
  InstrProfRecord &operator*() { return Record; }
  InstrProfRecord *operator->() { return &Record; }
};

/// Base class and interface for reading profiling data of any known instrprof
/// format. Provides an iterator over InstrProfRecords.
class InstrProfReader {
  std::error_code LastError;

public:
  InstrProfReader() : LastError(instrprof_error::success), Symtab() {}
  virtual ~InstrProfReader() {}

  /// Read the header.  Required before reading first record.
  virtual std::error_code readHeader() = 0;
  /// Read a single record.
  virtual std::error_code readNextRecord(InstrProfRecord &Record) = 0;
  /// Iterator over profile data.
  InstrProfIterator begin() { return InstrProfIterator(this); }
  InstrProfIterator end() { return InstrProfIterator(); }

  /// Return the PGO symtab. There are three different readers:
  /// Raw, Text, and Indexed profile readers. The first two types
  /// of readers are used only by llvm-profdata tool, while the indexed
  /// profile reader is also used by llvm-cov tool and the compiler (
  /// backend or frontend). Since creating PGO symtab can create
  /// significant runtime and memory overhead (as it touches data
  /// for the whole program), InstrProfSymtab for the indexed profile
  /// reader should be created on demand and it is recommended to be
  /// only used for dumping purpose with llvm-proftool, not with the
  /// compiler.
  virtual InstrProfSymtab &getSymtab() = 0;

protected:
  std::unique_ptr<InstrProfSymtab> Symtab;
  /// Set the current std::error_code and return same.
  std::error_code error(std::error_code EC) {
    LastError = EC;
    return EC;
  }

  /// Clear the current error code and return a successful one.
  std::error_code success() { return error(instrprof_error::success); }

public:
  /// Return true if the reader has finished reading the profile data.
  bool isEOF() { return LastError == instrprof_error::eof; }
  /// Return true if the reader encountered an error reading profiling data.
  bool hasError() { return LastError && !isEOF(); }
  /// Get the current error code.
  std::error_code getError() { return LastError; }

  /// Factory method to create an appropriately typed reader for the given
  /// instrprof file.
  static ErrorOr<std::unique_ptr<InstrProfReader>> create(std::string Path);

  static ErrorOr<std::unique_ptr<InstrProfReader>>
  create(std::unique_ptr<MemoryBuffer> Buffer);
};

/// Reader for the simple text based instrprof format.
///
/// This format is a simple text format that's suitable for test data. Records
/// are separated by one or more blank lines, and record fields are separated by
/// new lines.
///
/// Each record consists of a function name, a function hash, a number of
/// counters, and then each counter value, in that order.
class TextInstrProfReader : public InstrProfReader {
private:
  /// The profile data file contents.
  std::unique_ptr<MemoryBuffer> DataBuffer;
  /// Iterator over the profile data.
  line_iterator Line;

  TextInstrProfReader(const TextInstrProfReader &) = delete;
  TextInstrProfReader &operator=(const TextInstrProfReader &) = delete;
  std::error_code readValueProfileData(InstrProfRecord &Record);

public:
  TextInstrProfReader(std::unique_ptr<MemoryBuffer> DataBuffer_)
      : DataBuffer(std::move(DataBuffer_)), Line(*DataBuffer, true, '#') {}

  /// Return true if the given buffer is in text instrprof format.
  static bool hasFormat(const MemoryBuffer &Buffer);

  /// Read the header.
  std::error_code readHeader() override;
  /// Read a single record.
  std::error_code readNextRecord(InstrProfRecord &Record) override;

  InstrProfSymtab &getSymtab() override {
    assert(Symtab.get());
    return *Symtab.get();
  }
};

/// Reader for the raw instrprof binary format from runtime.
///
/// This format is a raw memory dump of the instrumentation-baed profiling data
/// from the runtime.  It has no index.
///
/// Templated on the unsigned type whose size matches pointers on the platform
/// that wrote the profile.
template <class IntPtrT>
class RawInstrProfReader : public InstrProfReader {
private:
  /// The profile data file contents.
  std::unique_ptr<MemoryBuffer> DataBuffer;
  bool ShouldSwapBytes;
  uint64_t CountersDelta;
  uint64_t NamesDelta;
  const RawInstrProf::ProfileData<IntPtrT> *Data;
  const RawInstrProf::ProfileData<IntPtrT> *DataEnd;
  const uint64_t *CountersStart;
  const char *NamesStart;
  const uint8_t *ValueDataStart;
  const char *ProfileEnd;
  uint32_t ValueKindLast;
  uint32_t CurValueDataSize;

  InstrProfRecord::ValueMapType FunctionPtrToNameMap;

  RawInstrProfReader(const RawInstrProfReader &) = delete;
  RawInstrProfReader &operator=(const RawInstrProfReader &) = delete;
public:
  RawInstrProfReader(std::unique_ptr<MemoryBuffer> DataBuffer)
      : DataBuffer(std::move(DataBuffer)) { }

  static bool hasFormat(const MemoryBuffer &DataBuffer);
  std::error_code readHeader() override;
  std::error_code readNextRecord(InstrProfRecord &Record) override;

  InstrProfSymtab &getSymtab() override {
    assert(Symtab.get());
    return *Symtab.get();
  }

private:
  void createSymtab(InstrProfSymtab &Symtab);
  std::error_code readNextHeader(const char *CurrentPos);
  std::error_code readHeader(const RawInstrProf::Header &Header);
  template <class IntT> IntT swap(IntT Int) const {
    return ShouldSwapBytes ? sys::getSwappedBytes(Int) : Int;
  }
  support::endianness getDataEndianness() const {
    support::endianness HostEndian = getHostEndianness();
    if (!ShouldSwapBytes)
      return HostEndian;
    if (HostEndian == support::little)
      return support::big;
    else
      return support::little;
  }

  inline uint8_t getNumPaddingBytes(uint64_t SizeInBytes) {
    return 7 & (sizeof(uint64_t) - SizeInBytes % sizeof(uint64_t));
  }
  std::error_code readName(InstrProfRecord &Record);
  std::error_code readFuncHash(InstrProfRecord &Record);
  std::error_code readRawCounts(InstrProfRecord &Record);
  std::error_code readValueProfilingData(InstrProfRecord &Record);
  bool atEnd() const { return Data == DataEnd; }
  void advanceData() {
    Data++;
    ValueDataStart += CurValueDataSize;
  }

  const uint64_t *getCounter(IntPtrT CounterPtr) const {
    ptrdiff_t Offset = (swap(CounterPtr) - CountersDelta) / sizeof(uint64_t);
    return CountersStart + Offset;
  }
  const char *getName(IntPtrT NamePtr) const {
    ptrdiff_t Offset = (swap(NamePtr) - NamesDelta) / sizeof(char);
    return NamesStart + Offset;
  }
};

typedef RawInstrProfReader<uint32_t> RawInstrProfReader32;
typedef RawInstrProfReader<uint64_t> RawInstrProfReader64;

namespace IndexedInstrProf {
enum class HashT : uint32_t;
}

/// Trait for lookups into the on-disk hash table for the binary instrprof
/// format.
class InstrProfLookupTrait {
  std::vector<InstrProfRecord> DataBuffer;
  IndexedInstrProf::HashT HashType;
  unsigned FormatVersion;
  // Endianness of the input value profile data.
  // It should be LE by default, but can be changed
  // for testing purpose.
  support::endianness ValueProfDataEndianness;

public:
  InstrProfLookupTrait(IndexedInstrProf::HashT HashType, unsigned FormatVersion)
      : HashType(HashType), FormatVersion(FormatVersion),
        ValueProfDataEndianness(support::little) {}

  typedef ArrayRef<InstrProfRecord> data_type;

  typedef StringRef internal_key_type;
  typedef StringRef external_key_type;
  typedef uint64_t hash_value_type;
  typedef uint64_t offset_type;

  static bool EqualKey(StringRef A, StringRef B) { return A == B; }
  static StringRef GetInternalKey(StringRef K) { return K; }
  static StringRef GetExternalKey(StringRef K) { return K; }

  hash_value_type ComputeHash(StringRef K);

  static std::pair<offset_type, offset_type>
  ReadKeyDataLength(const unsigned char *&D) {
    using namespace support;
    offset_type KeyLen = endian::readNext<offset_type, little, unaligned>(D);
    offset_type DataLen = endian::readNext<offset_type, little, unaligned>(D);
    return std::make_pair(KeyLen, DataLen);
  }

  StringRef ReadKey(const unsigned char *D, offset_type N) {
    return StringRef((const char *)D, N);
  }

  bool readValueProfilingData(const unsigned char *&D,
                              const unsigned char *const End);
  data_type ReadData(StringRef K, const unsigned char *D, offset_type N);

  // Used for testing purpose only.
  void setValueProfDataEndianness(support::endianness Endianness) {
    ValueProfDataEndianness = Endianness;
  }
};

struct InstrProfReaderIndexBase {
  // Read all the profile records with the same key pointed to the current
  // iterator.
  virtual std::error_code getRecords(ArrayRef<InstrProfRecord> &Data) = 0;
  // Read all the profile records with the key equal to FuncName
  virtual std::error_code getRecords(StringRef FuncName,
                                     ArrayRef<InstrProfRecord> &Data) = 0;
  virtual void advanceToNextKey() = 0;
  virtual bool atEnd() const = 0;
  virtual void setValueProfDataEndianness(support::endianness Endianness) = 0;
  virtual ~InstrProfReaderIndexBase() {}
  virtual uint64_t getVersion() const = 0;
  virtual void populateSymtab(InstrProfSymtab &) = 0;
};

typedef OnDiskIterableChainedHashTable<InstrProfLookupTrait>
    OnDiskHashTableImplV3;

template <typename HashTableImpl>
class InstrProfReaderIndex : public InstrProfReaderIndexBase {

private:
  std::unique_ptr<HashTableImpl> HashTable;
  typename HashTableImpl::data_iterator RecordIterator;
  uint64_t FormatVersion;

public:
  InstrProfReaderIndex(const unsigned char *Buckets,
                       const unsigned char *const Payload,
                       const unsigned char *const Base,
                       IndexedInstrProf::HashT HashType, uint64_t Version);

  std::error_code getRecords(ArrayRef<InstrProfRecord> &Data) override;
  std::error_code getRecords(StringRef FuncName,
                             ArrayRef<InstrProfRecord> &Data) override;
  void advanceToNextKey() override { RecordIterator++; }
  bool atEnd() const override {
    return RecordIterator == HashTable->data_end();
  }
  void setValueProfDataEndianness(support::endianness Endianness) override {
    HashTable->getInfoObj().setValueProfDataEndianness(Endianness);
  }
  ~InstrProfReaderIndex() override {}
  uint64_t getVersion() const override { return FormatVersion; }
  void populateSymtab(InstrProfSymtab &Symtab) override {
    Symtab.create(HashTable->keys());
  }
};

/// Reader for the indexed binary instrprof format.
class IndexedInstrProfReader : public InstrProfReader {
private:
  /// The profile data file contents.
  std::unique_ptr<MemoryBuffer> DataBuffer;
  /// The index into the profile data.
  std::unique_ptr<InstrProfReaderIndexBase> Index;
  /// The maximal execution count among all functions.
  uint64_t MaxFunctionCount;

  IndexedInstrProfReader(const IndexedInstrProfReader &) = delete;
  IndexedInstrProfReader &operator=(const IndexedInstrProfReader &) = delete;

public:
  uint64_t getVersion() const { return Index->getVersion(); }
  IndexedInstrProfReader(std::unique_ptr<MemoryBuffer> DataBuffer)
      : DataBuffer(std::move(DataBuffer)), Index(nullptr) {}

  /// Return true if the given buffer is in an indexed instrprof format.
  static bool hasFormat(const MemoryBuffer &DataBuffer);

  /// Read the file header.
  std::error_code readHeader() override;
  /// Read a single record.
  std::error_code readNextRecord(InstrProfRecord &Record) override;

  /// Return the pointer to InstrProfRecord associated with FuncName
  /// and FuncHash
  ErrorOr<InstrProfRecord> getInstrProfRecord(StringRef FuncName,
                                              uint64_t FuncHash);

  /// Fill Counts with the profile data for the given function name.
  std::error_code getFunctionCounts(StringRef FuncName, uint64_t FuncHash,
                                    std::vector<uint64_t> &Counts);

  /// Return the maximum of all known function counts.
  uint64_t getMaximumFunctionCount() { return MaxFunctionCount; }

  /// Factory method to create an indexed reader.
  static ErrorOr<std::unique_ptr<IndexedInstrProfReader>>
  create(std::string Path);

  static ErrorOr<std::unique_ptr<IndexedInstrProfReader>>
  create(std::unique_ptr<MemoryBuffer> Buffer);

  // Used for testing purpose only.
  void setValueProfDataEndianness(support::endianness Endianness) {
    Index->setValueProfDataEndianness(Endianness);
  }

  // See description in the base class. This interface is designed
  // to be used by llvm-profdata (for dumping). Avoid using this when
  // the client is the compiler.
  InstrProfSymtab &getSymtab() override;
};

} // end namespace llvm

#endif
