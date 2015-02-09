//===- ListReducer.h - Trim down list while retaining property --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This class is to be used as a base class for operations that want to zero in
// on a subset of the input which still causes the bug we are tracking.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_BUGPOINT_LISTREDUCER_H
#define LLVM_TOOLS_BUGPOINT_LISTREDUCER_H

#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cstdlib>
#include <vector>

namespace llvm {
  
  extern bool BugpointIsInterrupted;

template<typename ElTy>
struct ListReducer {
  enum TestResult {
    NoFailure,         // No failure of the predicate was detected
    KeepSuffix,        // The suffix alone satisfies the predicate
    KeepPrefix,        // The prefix alone satisfies the predicate
    InternalError      // Encountered an error trying to run the predicate
  };

  virtual ~ListReducer() {}

  // doTest - This virtual function should be overriden by subclasses to
  // implement the test desired.  The testcase is only required to test to see
  // if the Kept list still satisfies the property, but if it is going to check
  // the prefix anyway, it can.
  //
  virtual TestResult doTest(std::vector<ElTy> &Prefix,
                            std::vector<ElTy> &Kept,
                            std::string &Error) = 0;

  // reduceList - This function attempts to reduce the length of the specified
  // list while still maintaining the "test" property.  This is the core of the
  // "work" that bugpoint does.
  //
  bool reduceList(std::vector<ElTy> &TheList, std::string &Error) {
    std::vector<ElTy> empty;
    std::srand(0x6e5ea738); // Seed the random number generator
    switch (doTest(TheList, empty, Error)) {
    case KeepPrefix:
      if (TheList.size() == 1) // we are done, it's the base case and it fails
        return true;
      else
        break; // there's definitely an error, but we need to narrow it down

    case KeepSuffix:
      // cannot be reached!
      llvm_unreachable("bugpoint ListReducer internal error: "
                       "selected empty set.");

    case NoFailure:
      return false; // there is no failure with the full set of passes/funcs!

    case InternalError:
      assert(!Error.empty());
      return true;
    }

    // Maximal number of allowed splitting iterations,
    // before the elements are randomly shuffled.
    const unsigned MaxIterationsWithoutProgress = 3;
    bool ShufflingEnabled = true;

Backjump:
    unsigned MidTop = TheList.size();
    unsigned MaxIterations = MaxIterationsWithoutProgress;
    unsigned NumOfIterationsWithoutProgress = 0;
    while (MidTop > 1) { // Binary split reduction loop
      // Halt if the user presses ctrl-c.
      if (BugpointIsInterrupted) {
        errs() << "\n\n*** Reduction Interrupted, cleaning up...\n\n";
        return true;
      }

      // If the loop doesn't make satisfying progress, try shuffling.
      // The purpose of shuffling is to avoid the heavy tails of the
      // distribution (improving the speed of convergence).
      if (ShufflingEnabled && 
          NumOfIterationsWithoutProgress > MaxIterations) {
        std::vector<ElTy> ShuffledList(TheList);
        std::random_shuffle(ShuffledList.begin(), ShuffledList.end());
        errs() << "\n\n*** Testing shuffled set...\n\n";
        // Check that random shuffle doesn't loose the bug
        if (doTest(ShuffledList, empty, Error) == KeepPrefix) {
          // If the bug is still here, use the shuffled list.
          TheList.swap(ShuffledList);
          MidTop = TheList.size();
          // Must increase the shuffling treshold to avoid the small 
          // probability of inifinite looping without making progress.
          MaxIterations += 2;
          errs() << "\n\n*** Shuffling does not hide the bug...\n\n";
        } else {
          ShufflingEnabled = false; // Disable shuffling further on
          errs() << "\n\n*** Shuffling hides the bug...\n\n";
        }
        NumOfIterationsWithoutProgress = 0;
      }
      
      unsigned Mid = MidTop / 2;
      std::vector<ElTy> Prefix(TheList.begin(), TheList.begin()+Mid);
      std::vector<ElTy> Suffix(TheList.begin()+Mid, TheList.end());

      switch (doTest(Prefix, Suffix, Error)) {
      case KeepSuffix:
        // The property still holds.  We can just drop the prefix elements, and
        // shorten the list to the "kept" elements.
        TheList.swap(Suffix);
        MidTop = TheList.size();
        // Reset progress treshold and progress counter
        MaxIterations = MaxIterationsWithoutProgress;
        NumOfIterationsWithoutProgress = 0;
        break;
      case KeepPrefix:
        // The predicate still holds, shorten the list to the prefix elements.
        TheList.swap(Prefix);
        MidTop = TheList.size();
        // Reset progress treshold and progress counter
        MaxIterations = MaxIterationsWithoutProgress;
        NumOfIterationsWithoutProgress = 0;
        break;
      case NoFailure:
        // Otherwise the property doesn't hold.  Some of the elements we removed
        // must be necessary to maintain the property.
        MidTop = Mid;
        NumOfIterationsWithoutProgress++;
        break;
      case InternalError:
        return true;  // Error was set by doTest.
      }
      assert(Error.empty() && "doTest did not return InternalError for error");
    }

    // Probability of backjumping from the trimming loop back to the binary
    // split reduction loop.
    const int BackjumpProbability = 10;

    // Okay, we trimmed as much off the top and the bottom of the list as we
    // could.  If there is more than two elements in the list, try deleting 
    // interior elements and testing that.
    //
    if (TheList.size() > 2) {
      bool Changed = true;
      std::vector<ElTy> EmptyList;
      while (Changed) {  // Trimming loop.
        Changed = false;
        
        // If the binary split reduction loop made an unfortunate sequence of
        // splits, the trimming loop might be left off with a huge number of
        // remaining elements (large search space). Backjumping out of that
        // search space and attempting a different split can significantly 
        // improve the convergence speed.
        if (std::rand() % 100 < BackjumpProbability)
          goto Backjump;
        
        for (unsigned i = 1; i < TheList.size()-1; ++i) { // Check interior elts
          if (BugpointIsInterrupted) {
            errs() << "\n\n*** Reduction Interrupted, cleaning up...\n\n";
            return true;
          }
          
          std::vector<ElTy> TestList(TheList);
          TestList.erase(TestList.begin()+i);

          if (doTest(EmptyList, TestList, Error) == KeepSuffix) {
            // We can trim down the list!
            TheList.swap(TestList);
            --i;  // Don't skip an element of the list
            Changed = true;
          }
          if (!Error.empty())
            return true;
        }
        // This can take a long time if left uncontrolled.  For now, don't
        // iterate.
        break;
      }
    }

    return true; // there are some failure and we've narrowed them down
  }
};

} // End llvm namespace

#endif
