//===--- TargetRegistry.cpp - Target registration -------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/TargetRegistry.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <vector>
using namespace llvm;

// Clients are responsible for avoid race conditions in registration.
static Target *FirstTarget = nullptr;

TargetRegistry::iterator TargetRegistry::begin() {
  return iterator(FirstTarget);
}

const Target *TargetRegistry::lookupTarget(const std::string &ArchName,
                                           Triple &TheTriple,
                                           std::string &Error) {
  // Allocate target machine.  First, check whether the user has explicitly
  // specified an architecture to compile for. If so we have to look it up by
  // name, because it might be a backend that has no mapping to a target triple.
  const Target *TheTarget = nullptr;
  if (!ArchName.empty()) {
    for (TargetRegistry::iterator it = TargetRegistry::begin(),
           ie = TargetRegistry::end(); it != ie; ++it) {
      if (ArchName == it->getName()) {
        TheTarget = &*it;
        break;
      }
    }

    if (!TheTarget) {
      Error = "error: invalid target '" + ArchName + "'.\n";
      return nullptr;
    }

    // Adjust the triple to match (if known), otherwise stick with the
    // given triple.
    Triple::ArchType Type = Triple::getArchTypeForLLVMName(ArchName);
    if (Type != Triple::UnknownArch)
      TheTriple.setArch(Type);
  } else {
    // Get the target specific parser.
    std::string TempError;
    TheTarget = TargetRegistry::lookupTarget(TheTriple.getTriple(), TempError);
    if (!TheTarget) {
      Error = ": error: unable to get target for '"
            + TheTriple.getTriple()
            + "', see --version and --triple.\n";
      return nullptr;
    }
  }

  return TheTarget;
}

const Target *TargetRegistry::lookupTarget(const std::string &TT,
                                           std::string &Error) {
  // Provide special warning when no targets are initialized.
  if (begin() == end()) {
    Error = "Unable to find target for this triple (no targets are registered)";
    return nullptr;
  }
  const Target *Matching = nullptr;
  Triple::ArchType Arch =  Triple(TT).getArch();
  for (iterator it = begin(), ie = end(); it != ie; ++it) {
    if (it->ArchMatchFn(Arch)) {
      if (Matching) {
        Error = std::string("Cannot choose between targets \"") +
          Matching->Name  + "\" and \"" + it->Name + "\"";
        return nullptr;
      }
      Matching = &*it;
    }
  }

  if (!Matching) {
    Error = "No available targets are compatible with this triple, "
      "see -version for the available targets.";
    return nullptr;
  }

  return Matching;
}

void TargetRegistry::RegisterTarget(Target &T,
                                    const char *Name,
                                    const char *ShortDesc,
                                    Target::ArchMatchFnTy ArchMatchFn,
                                    bool HasJIT) {
  assert(Name && ShortDesc && ArchMatchFn &&
         "Missing required target information!");

  // Check if this target has already been initialized, we allow this as a
  // convenience to some clients.
  if (T.Name)
    return;
         
  // Add to the list of targets.
  T.Next = FirstTarget;
  FirstTarget = &T;

  T.Name = Name;
  T.ShortDesc = ShortDesc;
  T.ArchMatchFn = ArchMatchFn;
  T.HasJIT = HasJIT;
}

static int TargetArraySortFn(const std::pair<StringRef, const Target *> *LHS,
                             const std::pair<StringRef, const Target *> *RHS) {
  return LHS->first.compare(RHS->first);
}

void TargetRegistry::printRegisteredTargetsForVersion() {
  std::vector<std::pair<StringRef, const Target*> > Targets;
  size_t Width = 0;
  for (TargetRegistry::iterator I = TargetRegistry::begin(),
       E = TargetRegistry::end();
       I != E; ++I) {
    Targets.push_back(std::make_pair(I->getName(), &*I));
    Width = std::max(Width, Targets.back().first.size());
  }
  array_pod_sort(Targets.begin(), Targets.end(), TargetArraySortFn);

  raw_ostream &OS = outs();
  OS << "  Registered Targets:\n";
  for (unsigned i = 0, e = Targets.size(); i != e; ++i) {
    OS << "    " << Targets[i].first;
    OS.indent(Width - Targets[i].first.size()) << " - "
      << Targets[i].second->getShortDescription() << '\n';
  }
  if (Targets.empty())
    OS << "    (none)\n";
}
