//===-- SpecialCaseList.h - special case list for sanitizers ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//===----------------------------------------------------------------------===//
//
// This is a utility class for instrumentation passes (like AddressSanitizer
// or ThreadSanitizer) to avoid instrumenting some functions or global
// variables based on a user-supplied list.
//
// The list can also specify categories for specific globals, which can be used
// to instruct an instrumentation pass to treat certain functions or global
// variables in a specific way, such as by omitting certain aspects of
// instrumentation while keeping others, or informing the instrumentation pass
// that a specific uninstrumentable function has certain semantics, thus
// allowing the pass to instrument callers according to those semantics.
//
// For example, AddressSanitizer uses the "init" category for globals whose
// initializers should not be instrumented, but which in all other respects
// should be instrumented.
//
// Each line contains a prefix, followed by a colon and a wild card expression,
// followed optionally by an equals sign and an instrumentation-specific
// category.  Empty lines and lines starting with "#" are ignored.
// ---
// # Blacklisted items:
// fun:*_ZN4base6subtle*
// global:*global_with_bad_access_or_initialization*
// global:*global_with_initialization_issues*=init
// type:*Namespace::ClassName*=init
// src:file_with_tricky_code.cc
// src:ignore-global-initializers-issues.cc=init
//
// # Functions with pure functional semantics:
// fun:cos=functional
// fun:sin=functional
// ---
// Note that the wild card is in fact an llvm::Regex, but * is automatically
// replaced with .*
// This is similar to the "ignore" feature of ThreadSanitizer.
// http://code.google.com/p/data-race-test/wiki/ThreadSanitizerIgnores
//
//===----------------------------------------------------------------------===//
//

#include "llvm/ADT/StringMap.h"

namespace llvm {
class Function;
class GlobalVariable;
class MemoryBuffer;
class Module;
class Regex;
class StringRef;

class SpecialCaseList {
 public:
  SpecialCaseList(const StringRef Path);
  SpecialCaseList(const MemoryBuffer *MB);
  ~SpecialCaseList();

  /// Returns whether either this function or its source file are listed in the
  /// given category, which may be omitted to search the empty category.
  bool isIn(const Function &F, const StringRef Category = StringRef()) const;

  /// Returns whether this global, its type or its source file are listed in the
  /// given category, which may be omitted to search the empty category.
  bool isIn(const GlobalVariable &G,
            const StringRef Category = StringRef()) const;

  /// Returns whether this module is listed in the given category, which may be
  /// omitted to search the empty category.
  bool isIn(const Module &M, const StringRef Category = StringRef()) const;

  /// Returns whether either this function or its source file are listed in any
  /// category.  Category will contain the name of an arbitrary category in
  /// which this function is listed.
  bool findCategory(const Function &F, StringRef &Category) const;

  /// Returns whether this global, its type or its source file are listed in any
  /// category.  Category will contain the name of an arbitrary category in
  /// which this global is listed.
  bool findCategory(const GlobalVariable &G, StringRef &Category) const;

  /// Returns whether this module is listed in any category.  Category will
  /// contain the name of an arbitrary category in which this module is listed.
  bool findCategory(const Module &M, StringRef &Category) const;

 private:
  StringMap<StringMap<Regex*> > Entries;

  void init(const MemoryBuffer *MB);
  bool findCategory(const StringRef Section, const StringRef Query,
                    StringRef &Category) const;
  bool inSectionCategory(const StringRef Section, const StringRef Query,
                         const StringRef Category) const;
};

}  // namespace llvm
