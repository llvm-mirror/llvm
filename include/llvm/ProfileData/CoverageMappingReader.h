//=-- CoverageMappingReader.h - Code coverage mapping reader ------*- C++ -*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains support for reading coverage mapping data for
// instrumentation based coverage.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_PROFILEDATA_COVERAGEMAPPINGREADER_H
#define LLVM_PROFILEDATA_COVERAGEMAPPINGREADER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/ProfileData/CoverageMapping.h"
#include "llvm/ProfileData/InstrProf.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include <iterator>

namespace llvm {
namespace coverage {

class ObjectFileCoverageMappingReader;

/// \brief Coverage mapping information for a single function.
struct CoverageMappingRecord {
  StringRef FunctionName;
  uint64_t FunctionHash;
  ArrayRef<StringRef> Filenames;
  ArrayRef<CounterExpression> Expressions;
  ArrayRef<CounterMappingRegion> MappingRegions;
};

/// \brief A file format agnostic iterator over coverage mapping data.
class CoverageMappingIterator
    : public std::iterator<std::input_iterator_tag, CoverageMappingRecord> {
  ObjectFileCoverageMappingReader *Reader;
  CoverageMappingRecord Record;

  void increment();

public:
  CoverageMappingIterator() : Reader(nullptr) {}
  CoverageMappingIterator(ObjectFileCoverageMappingReader *Reader)
      : Reader(Reader) {
    increment();
  }

  CoverageMappingIterator &operator++() {
    increment();
    return *this;
  }
  bool operator==(const CoverageMappingIterator &RHS) {
    return Reader == RHS.Reader;
  }
  bool operator!=(const CoverageMappingIterator &RHS) {
    return Reader != RHS.Reader;
  }
  CoverageMappingRecord &operator*() { return Record; }
  CoverageMappingRecord *operator->() { return &Record; }
};

/// \brief Base class for the raw coverage mapping and filenames data readers.
class RawCoverageReader {
protected:
  StringRef Data;

  /// \brief Return the error code.
  std::error_code error(std::error_code EC) { return EC; }

  /// \brief Clear the current error code and return a successful one.
  std::error_code success() { return error(instrprof_error::success); }

  RawCoverageReader(StringRef Data) : Data(Data) {}

  std::error_code readULEB128(uint64_t &Result);
  std::error_code readIntMax(uint64_t &Result, uint64_t MaxPlus1);
  std::error_code readSize(uint64_t &Result);
  std::error_code readString(StringRef &Result);
};

/// \brief Reader for the raw coverage filenames.
class RawCoverageFilenamesReader : public RawCoverageReader {
  std::vector<StringRef> &Filenames;

  RawCoverageFilenamesReader(const RawCoverageFilenamesReader &)
      LLVM_DELETED_FUNCTION;
  RawCoverageFilenamesReader &
  operator=(const RawCoverageFilenamesReader &) LLVM_DELETED_FUNCTION;

public:
  RawCoverageFilenamesReader(StringRef Data, std::vector<StringRef> &Filenames)
      : RawCoverageReader(Data), Filenames(Filenames) {}

  std::error_code read();
};

/// \brief Reader for the raw coverage mapping data.
class RawCoverageMappingReader : public RawCoverageReader {
  ArrayRef<StringRef> TranslationUnitFilenames;
  std::vector<StringRef> &Filenames;
  std::vector<CounterExpression> &Expressions;
  std::vector<CounterMappingRegion> &MappingRegions;

  RawCoverageMappingReader(const RawCoverageMappingReader &)
      LLVM_DELETED_FUNCTION;
  RawCoverageMappingReader &
  operator=(const RawCoverageMappingReader &) LLVM_DELETED_FUNCTION;

public:
  RawCoverageMappingReader(StringRef MappingData,
                           ArrayRef<StringRef> TranslationUnitFilenames,
                           std::vector<StringRef> &Filenames,
                           std::vector<CounterExpression> &Expressions,
                           std::vector<CounterMappingRegion> &MappingRegions)
      : RawCoverageReader(MappingData),
        TranslationUnitFilenames(TranslationUnitFilenames),
        Filenames(Filenames), Expressions(Expressions),
        MappingRegions(MappingRegions) {}

  std::error_code read();

private:
  std::error_code decodeCounter(unsigned Value, Counter &C);
  std::error_code readCounter(Counter &C);
  std::error_code
  readMappingRegionsSubArray(std::vector<CounterMappingRegion> &MappingRegions,
                             unsigned InferredFileID, size_t NumFileIDs);
};

/// \brief Reader for the coverage mapping data that is emitted by the
/// frontend and stored in an object file.
class ObjectFileCoverageMappingReader {
public:
  struct ProfileMappingRecord {
    CoverageMappingVersion Version;
    StringRef FunctionName;
    uint64_t FunctionHash;
    StringRef CoverageMapping;
    size_t FilenamesBegin;
    size_t FilenamesSize;

    ProfileMappingRecord(CoverageMappingVersion Version, StringRef FunctionName,
                         uint64_t FunctionHash, StringRef CoverageMapping,
                         size_t FilenamesBegin, size_t FilenamesSize)
        : Version(Version), FunctionName(FunctionName),
          FunctionHash(FunctionHash), CoverageMapping(CoverageMapping),
          FilenamesBegin(FilenamesBegin), FilenamesSize(FilenamesSize) {}
  };

private:
  std::error_code LastError;
  object::OwningBinary<object::ObjectFile> Object;
  std::vector<StringRef> Filenames;
  std::vector<ProfileMappingRecord> MappingRecords;
  size_t CurrentRecord;
  std::vector<StringRef> FunctionsFilenames;
  std::vector<CounterExpression> Expressions;
  std::vector<CounterMappingRegion> MappingRegions;

  ObjectFileCoverageMappingReader(const ObjectFileCoverageMappingReader &)
      LLVM_DELETED_FUNCTION;
  ObjectFileCoverageMappingReader &
  operator=(const ObjectFileCoverageMappingReader &) LLVM_DELETED_FUNCTION;

  /// \brief Set the current error_code and return same.
  std::error_code error(std::error_code EC) {
    LastError = EC;
    return EC;
  }

  /// \brief Clear the current error code and return a successful one.
  std::error_code success() { return error(instrprof_error::success); }

public:
  ObjectFileCoverageMappingReader(StringRef FileName);
  ObjectFileCoverageMappingReader(
      std::unique_ptr<MemoryBuffer> &ObjectBuffer,
      sys::fs::file_magic Type = sys::fs::file_magic::unknown);

  std::error_code readHeader();
  std::error_code readNextRecord(CoverageMappingRecord &Record);

  /// Iterator over profile data.
  CoverageMappingIterator begin() { return CoverageMappingIterator(this); }
  CoverageMappingIterator end() { return CoverageMappingIterator(); }

  /// \brief Return true if the reader has finished reading the profile data.
  bool isEOF() { return LastError == instrprof_error::eof; }
  /// \brief Return true if the reader encountered an error reading profiling
  /// data.
  bool hasError() { return LastError && !isEOF(); }
  /// \brief Get the current error code.
  std::error_code getError() { return LastError; }
};

} // end namespace coverage
} // end namespace llvm

#endif
