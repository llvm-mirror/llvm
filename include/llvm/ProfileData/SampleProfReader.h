//===- SampleProfReader.h - Read LLVM sample profile data -----------------===//
//
//                      The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains definitions needed for reading sample profiles.
//
// NOTE: If you are making changes to this file format, please remember
//       to document them in the Clang documentation at
//       tools/clang/docs/UsersManual.rst.
//
// Text format
// -----------
//
// Sample profiles are written as ASCII text. The file is divided into
// sections, which correspond to each of the functions executed at runtime.
// Each section has the following format
//
//     function1:total_samples:total_head_samples
//      offset1[.discriminator]: number_of_samples [fn1:num fn2:num ... ]
//      offset2[.discriminator]: number_of_samples [fn3:num fn4:num ... ]
//      ...
//      offsetN[.discriminator]: number_of_samples [fn5:num fn6:num ... ]
//      offsetA[.discriminator]: fnA:num_of_total_samples
//       offsetA1[.discriminator]: number_of_samples [fn7:num fn8:num ... ]
//       ...
//
// This is a nested tree in which the identation represents the nesting level
// of the inline stack. There are no blank lines in the file. And the spacing
// within a single line is fixed. Additional spaces will result in an error
// while reading the file.
//
// Any line starting with the '#' character is completely ignored.
//
// Inlined calls are represented with indentation. The Inline stack is a
// stack of source locations in which the top of the stack represents the
// leaf function, and the bottom of the stack represents the actual
// symbol to which the instruction belongs.
//
// Function names must be mangled in order for the profile loader to
// match them in the current translation unit. The two numbers in the
// function header specify how many total samples were accumulated in the
// function (first number), and the total number of samples accumulated
// in the prologue of the function (second number). This head sample
// count provides an indicator of how frequently the function is invoked.
//
// There are two types of lines in the function body.
//
// * Sampled line represents the profile information of a source location.
// * Callsite line represents the profile information of a callsite.
//
// Each sampled line may contain several items. Some are optional (marked
// below):
//
// a. Source line offset. This number represents the line number
//    in the function where the sample was collected. The line number is
//    always relative to the line where symbol of the function is
//    defined. So, if the function has its header at line 280, the offset
//    13 is at line 293 in the file.
//
//    Note that this offset should never be a negative number. This could
//    happen in cases like macros. The debug machinery will register the
//    line number at the point of macro expansion. So, if the macro was
//    expanded in a line before the start of the function, the profile
//    converter should emit a 0 as the offset (this means that the optimizers
//    will not be able to associate a meaningful weight to the instructions
//    in the macro).
//
// b. [OPTIONAL] Discriminator. This is used if the sampled program
//    was compiled with DWARF discriminator support
//    (http://wiki.dwarfstd.org/index.php?title=Path_Discriminators).
//    DWARF discriminators are unsigned integer values that allow the
//    compiler to distinguish between multiple execution paths on the
//    same source line location.
//
//    For example, consider the line of code ``if (cond) foo(); else bar();``.
//    If the predicate ``cond`` is true 80% of the time, then the edge
//    into function ``foo`` should be considered to be taken most of the
//    time. But both calls to ``foo`` and ``bar`` are at the same source
//    line, so a sample count at that line is not sufficient. The
//    compiler needs to know which part of that line is taken more
//    frequently.
//
//    This is what discriminators provide. In this case, the calls to
//    ``foo`` and ``bar`` will be at the same line, but will have
//    different discriminator values. This allows the compiler to correctly
//    set edge weights into ``foo`` and ``bar``.
//
// c. Number of samples. This is an integer quantity representing the
//    number of samples collected by the profiler at this source
//    location.
//
// d. [OPTIONAL] Potential call targets and samples. If present, this
//    line contains a call instruction. This models both direct and
//    number of samples. For example,
//
//      130: 7  foo:3  bar:2  baz:7
//
//    The above means that at relative line offset 130 there is a call
//    instruction that calls one of ``foo()``, ``bar()`` and ``baz()``,
//    with ``baz()`` being the relatively more frequently called target.
//
// Each callsite line may contain several items. Some are optional.
//
// a. Source line offset. This number represents the line number of the
//    callsite that is inlined in the profiled binary.
//
// b. [OPTIONAL] Discriminator. Same as the discriminator for sampled line.
//
// c. Number of samples. This is an integer quantity representing the
//    total number of samples collected for the inlined instance at this
//    callsite
//
//
// Binary format
// -------------
//
// This is a more compact encoding. Numbers are encoded as ULEB128 values
// and all strings are encoded in a name table. The file is organized in
// the following sections:
//
// MAGIC (uint64_t)
//    File identifier computed by function SPMagic() (0x5350524f463432ff)
//
// VERSION (uint32_t)
//    File format version number computed by SPVersion()
//
// NAME TABLE
//    SIZE (uint32_t)
//        Number of entries in the name table.
//    NAMES
//        A NUL-separated list of SIZE strings.
//
// FUNCTION BODY (one for each uninlined function body present in the profile)
//    HEAD_SAMPLES (uint64_t) [only for top-level functions]
//        Total number of samples collected at the head (prologue) of the
//        function.
//        NOTE: This field should only be present for top-level functions
//              (i.e., not inlined into any caller). Inlined function calls
//              have no prologue, so they don't need this.
//    NAME_IDX (uint32_t)
//        Index into the name table indicating the function name.
//    SAMPLES (uint64_t)
//        Total number of samples collected in this function.
//    NRECS (uint32_t)
//        Total number of sampling records this function's profile.
//    BODY RECORDS
//        A list of NRECS entries. Each entry contains:
//          OFFSET (uint32_t)
//            Line offset from the start of the function.
//          DISCRIMINATOR (uint32_t)
//            Discriminator value (see description of discriminators
//            in the text format documentation above).
//          SAMPLES (uint64_t)
//            Number of samples collected at this location.
//          NUM_CALLS (uint32_t)
//            Number of non-inlined function calls made at this location. In the
//            case of direct calls, this number will always be 1. For indirect
//            calls (virtual functions and function pointers) this will
//            represent all the actual functions called at runtime.
//          CALL_TARGETS
//            A list of NUM_CALLS entries for each called function:
//               NAME_IDX (uint32_t)
//                  Index into the name table with the callee name.
//               SAMPLES (uint64_t)
//                  Number of samples collected at the call site.
//    NUM_INLINED_FUNCTIONS (uint32_t)
//      Number of callees inlined into this function.
//    INLINED FUNCTION RECORDS
//      A list of NUM_INLINED_FUNCTIONS entries describing each of the inlined
//      callees.
//        OFFSET (uint32_t)
//          Line offset from the start of the function.
//        DISCRIMINATOR (uint32_t)
//          Discriminator value (see description of discriminators
//          in the text format documentation above).
//        FUNCTION BODY
//          A FUNCTION BODY entry describing the inlined function.
//===----------------------------------------------------------------------===//
#ifndef LLVM_PROFILEDATA_SAMPLEPROFREADER_H
#define LLVM_PROFILEDATA_SAMPLEPROFREADER_H

#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/ProfileData/SampleProf.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/GCOV.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {

namespace sampleprof {

/// \brief Sample-based profile reader.
///
/// Each profile contains sample counts for all the functions
/// executed. Inside each function, statements are annotated with the
/// collected samples on all the instructions associated with that
/// statement.
///
/// For this to produce meaningful data, the program needs to be
/// compiled with some debug information (at minimum, line numbers:
/// -gline-tables-only). Otherwise, it will be impossible to match IR
/// instructions to the line numbers collected by the profiler.
///
/// From the profile file, we are interested in collecting the
/// following information:
///
/// * A list of functions included in the profile (mangled names).
///
/// * For each function F:
///   1. The total number of samples collected in F.
///
///   2. The samples collected at each line in F. To provide some
///      protection against source code shuffling, line numbers should
///      be relative to the start of the function.
///
/// The reader supports two file formats: text and binary. The text format
/// is useful for debugging and testing, while the binary format is more
/// compact and I/O efficient. They can both be used interchangeably.
class SampleProfileReader {
public:
  SampleProfileReader(std::unique_ptr<MemoryBuffer> B, LLVMContext &C)
      : Profiles(0), Ctx(C), Buffer(std::move(B)) {}

  virtual ~SampleProfileReader() {}

  /// \brief Read and validate the file header.
  virtual std::error_code readHeader() = 0;

  /// \brief Read sample profiles from the associated file.
  virtual std::error_code read() = 0;

  /// \brief Print the profile for \p FName on stream \p OS.
  void dumpFunctionProfile(StringRef FName, raw_ostream &OS = dbgs());

  /// \brief Print all the profiles on stream \p OS.
  void dump(raw_ostream &OS = dbgs());

  /// \brief Return the samples collected for function \p F.
  FunctionSamples *getSamplesFor(const Function &F) {
    return &Profiles[F.getName()];
  }

  /// \brief Return all the profiles.
  StringMap<FunctionSamples> &getProfiles() { return Profiles; }

  /// \brief Report a parse error message.
  void reportError(int64_t LineNumber, Twine Msg) const {
    Ctx.diagnose(DiagnosticInfoSampleProfile(Buffer->getBufferIdentifier(),
                                             LineNumber, Msg));
  }

  /// \brief Create a sample profile reader appropriate to the file format.
  static ErrorOr<std::unique_ptr<SampleProfileReader>>
  create(StringRef Filename, LLVMContext &C);

  /// \brief Create a sample profile reader from the supplied memory buffer.
  static ErrorOr<std::unique_ptr<SampleProfileReader>>
  create(std::unique_ptr<MemoryBuffer> &B, LLVMContext &C);

protected:
  /// \brief Map every function to its associated profile.
  ///
  /// The profile of every function executed at runtime is collected
  /// in the structure FunctionSamples. This maps function objects
  /// to their corresponding profiles.
  StringMap<FunctionSamples> Profiles;

  /// \brief LLVM context used to emit diagnostics.
  LLVMContext &Ctx;

  /// \brief Memory buffer holding the profile file.
  std::unique_ptr<MemoryBuffer> Buffer;
};

class SampleProfileReaderText : public SampleProfileReader {
public:
  SampleProfileReaderText(std::unique_ptr<MemoryBuffer> B, LLVMContext &C)
      : SampleProfileReader(std::move(B), C) {}

  /// \brief Read and validate the file header.
  std::error_code readHeader() override { return sampleprof_error::success; }

  /// \brief Read sample profiles from the associated file.
  std::error_code read() override;

  /// \brief Return true if \p Buffer is in the format supported by this class.
  static bool hasFormat(const MemoryBuffer &Buffer);
};

class SampleProfileReaderBinary : public SampleProfileReader {
public:
  SampleProfileReaderBinary(std::unique_ptr<MemoryBuffer> B, LLVMContext &C)
      : SampleProfileReader(std::move(B), C), Data(nullptr), End(nullptr) {}

  /// \brief Read and validate the file header.
  std::error_code readHeader() override;

  /// \brief Read sample profiles from the associated file.
  std::error_code read() override;

  /// \brief Return true if \p Buffer is in the format supported by this class.
  static bool hasFormat(const MemoryBuffer &Buffer);

protected:
  /// \brief Read a numeric value of type T from the profile.
  ///
  /// If an error occurs during decoding, a diagnostic message is emitted and
  /// EC is set.
  ///
  /// \returns the read value.
  template <typename T> ErrorOr<T> readNumber();

  /// \brief Read a string from the profile.
  ///
  /// If an error occurs during decoding, a diagnostic message is emitted and
  /// EC is set.
  ///
  /// \returns the read value.
  ErrorOr<StringRef> readString();

  /// Read a string indirectly via the name table.
  ErrorOr<StringRef> readStringFromTable();

  /// \brief Return true if we've reached the end of file.
  bool at_eof() const { return Data >= End; }

  /// Read the contents of the given profile instance.
  std::error_code readProfile(FunctionSamples &FProfile);

  /// \brief Points to the current location in the buffer.
  const uint8_t *Data;

  /// \brief Points to the end of the buffer.
  const uint8_t *End;

  /// Function name table.
  std::vector<StringRef> NameTable;
};

typedef SmallVector<FunctionSamples *, 10> InlineCallStack;

// Supported histogram types in GCC.  Currently, we only need support for
// call target histograms.
enum HistType {
  HIST_TYPE_INTERVAL,
  HIST_TYPE_POW2,
  HIST_TYPE_SINGLE_VALUE,
  HIST_TYPE_CONST_DELTA,
  HIST_TYPE_INDIR_CALL,
  HIST_TYPE_AVERAGE,
  HIST_TYPE_IOR,
  HIST_TYPE_INDIR_CALL_TOPN
};

class SampleProfileReaderGCC : public SampleProfileReader {
public:
  SampleProfileReaderGCC(std::unique_ptr<MemoryBuffer> B, LLVMContext &C)
      : SampleProfileReader(std::move(B), C), GcovBuffer(Buffer.get()) {}

  /// \brief Read and validate the file header.
  std::error_code readHeader() override;

  /// \brief Read sample profiles from the associated file.
  std::error_code read() override;

  /// \brief Return true if \p Buffer is in the format supported by this class.
  static bool hasFormat(const MemoryBuffer &Buffer);

protected:
  std::error_code readNameTable();
  std::error_code readOneFunctionProfile(const InlineCallStack &InlineStack,
                                         bool Update, uint32_t Offset);
  std::error_code readFunctionProfiles();
  std::error_code skipNextWord();
  template <typename T> ErrorOr<T> readNumber();
  ErrorOr<StringRef> readString();

  /// \brief Read the section tag and check that it's the same as \p Expected.
  std::error_code readSectionTag(uint32_t Expected);

  /// GCOV buffer containing the profile.
  GCOVBuffer GcovBuffer;

  /// Function names in this profile.
  std::vector<std::string> Names;

  /// GCOV tags used to separate sections in the profile file.
  static const uint32_t GCOVTagAFDOFileNames = 0xaa000000;
  static const uint32_t GCOVTagAFDOFunction = 0xac000000;
};

} // End namespace sampleprof

} // End namespace llvm

#endif // LLVM_PROFILEDATA_SAMPLEPROFREADER_H
