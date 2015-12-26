//===-- llvm/CodeGen/MachineBasicBlock.h ------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Collect the sequence of machine instructions for a basic block.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINEBASICBLOCK_H
#define LLVM_CODEGEN_MACHINEBASICBLOCK_H

#include "llvm/ADT/GraphTraits.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/Support/BranchProbability.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Support/DataTypes.h"
#include <functional>

namespace llvm {

class Pass;
class BasicBlock;
class MachineFunction;
class MCSymbol;
class MIPrinter;
class SlotIndexes;
class StringRef;
class raw_ostream;
class MachineBranchProbabilityInfo;

// Forward declaration to avoid circular include problem with TargetRegisterInfo
typedef unsigned LaneBitmask;

template <>
struct ilist_traits<MachineInstr> : public ilist_default_traits<MachineInstr> {
private:
  mutable ilist_half_node<MachineInstr> Sentinel;

  // this is only set by the MachineBasicBlock owning the LiveList
  friend class MachineBasicBlock;
  MachineBasicBlock* Parent;

public:
  MachineInstr *createSentinel() const {
    return static_cast<MachineInstr*>(&Sentinel);
  }
  void destroySentinel(MachineInstr *) const {}

  MachineInstr *provideInitialHead() const { return createSentinel(); }
  MachineInstr *ensureHead(MachineInstr*) const { return createSentinel(); }
  static void noteHead(MachineInstr*, MachineInstr*) {}

  void addNodeToList(MachineInstr* N);
  void removeNodeFromList(MachineInstr* N);
  void transferNodesFromList(ilist_traits &SrcTraits,
                             ilist_iterator<MachineInstr> First,
                             ilist_iterator<MachineInstr> Last);
  void deleteNode(MachineInstr *N);
private:
  void createNode(const MachineInstr &);
};

class MachineBasicBlock
    : public ilist_node_with_parent<MachineBasicBlock, MachineFunction> {
public:
  /// Pair of physical register and lane mask.
  /// This is not simply a std::pair typedef because the members should be named
  /// clearly as they both have an integer type.
  struct RegisterMaskPair {
  public:
    MCPhysReg PhysReg;
    LaneBitmask LaneMask;

    RegisterMaskPair(MCPhysReg PhysReg, LaneBitmask LaneMask)
        : PhysReg(PhysReg), LaneMask(LaneMask) {}
  };

private:
  typedef ilist<MachineInstr> Instructions;
  Instructions Insts;
  const BasicBlock *BB;
  int Number;
  MachineFunction *xParent;

  /// Keep track of the predecessor / successor basic blocks.
  std::vector<MachineBasicBlock *> Predecessors;
  std::vector<MachineBasicBlock *> Successors;

  /// Keep track of the probabilities to the successors. This vector has the
  /// same order as Successors, or it is empty if we don't use it (disable
  /// optimization).
  std::vector<BranchProbability> Probs;
  typedef std::vector<BranchProbability>::iterator probability_iterator;
  typedef std::vector<BranchProbability>::const_iterator
      const_probability_iterator;

  /// Keep track of the physical registers that are livein of the basicblock.
  typedef std::vector<RegisterMaskPair> LiveInVector;
  LiveInVector LiveIns;

  /// Alignment of the basic block. Zero if the basic block does not need to be
  /// aligned. The alignment is specified as log2(bytes).
  unsigned Alignment = 0;

  /// Indicate that this basic block is entered via an exception handler.
  bool IsEHPad = false;

  /// Indicate that this basic block is potentially the target of an indirect
  /// branch.
  bool AddressTaken = false;

  /// Indicate that this basic block is the entry block of an EH funclet.
  bool IsEHFuncletEntry = false;

  /// Indicate that this basic block is the entry block of a cleanup funclet.
  bool IsCleanupFuncletEntry = false;

  /// \brief since getSymbol is a relatively heavy-weight operation, the symbol
  /// is only computed once and is cached.
  mutable MCSymbol *CachedMCSymbol = nullptr;

  // Intrusive list support
  MachineBasicBlock() {}

  explicit MachineBasicBlock(MachineFunction &MF, const BasicBlock *BB);

  ~MachineBasicBlock();

  // MachineBasicBlocks are allocated and owned by MachineFunction.
  friend class MachineFunction;

public:
  /// Return the LLVM basic block that this instance corresponded to originally.
  /// Note that this may be NULL if this instance does not correspond directly
  /// to an LLVM basic block.
  const BasicBlock *getBasicBlock() const { return BB; }

  /// Return the name of the corresponding LLVM basic block, or "(null)".
  StringRef getName() const;

  /// Return a formatted string to identify this block and its parent function.
  std::string getFullName() const;

  /// Test whether this block is potentially the target of an indirect branch.
  bool hasAddressTaken() const { return AddressTaken; }

  /// Set this block to reflect that it potentially is the target of an indirect
  /// branch.
  void setHasAddressTaken() { AddressTaken = true; }

  /// Return the MachineFunction containing this basic block.
  const MachineFunction *getParent() const { return xParent; }
  MachineFunction *getParent() { return xParent; }

  /// MachineBasicBlock iterator that automatically skips over MIs that are
  /// inside bundles (i.e. walk top level MIs only).
  template<typename Ty, typename IterTy>
  class bundle_iterator
    : public std::iterator<std::bidirectional_iterator_tag, Ty, ptrdiff_t> {
    IterTy MII;

  public:
    bundle_iterator(IterTy MI) : MII(MI) {}

    bundle_iterator(Ty &MI) : MII(MI) {
      assert(!MI.isBundledWithPred() &&
             "It's not legal to initialize bundle_iterator with a bundled MI");
    }
    bundle_iterator(Ty *MI) : MII(MI) {
      assert((!MI || !MI->isBundledWithPred()) &&
             "It's not legal to initialize bundle_iterator with a bundled MI");
    }
    // Template allows conversion from const to nonconst.
    template<class OtherTy, class OtherIterTy>
    bundle_iterator(const bundle_iterator<OtherTy, OtherIterTy> &I)
      : MII(I.getInstrIterator()) {}
    bundle_iterator() : MII(nullptr) {}

    Ty &operator*() const { return *MII; }
    Ty *operator->() const { return &operator*(); }

    operator Ty *() const { return MII.getNodePtrUnchecked(); }

    bool operator==(const bundle_iterator &X) const {
      return MII == X.MII;
    }
    bool operator!=(const bundle_iterator &X) const {
      return !operator==(X);
    }

    // Increment and decrement operators...
    bundle_iterator &operator--() {      // predecrement - Back up
      do --MII;
      while (MII->isBundledWithPred());
      return *this;
    }
    bundle_iterator &operator++() {      // preincrement - Advance
      while (MII->isBundledWithSucc())
        ++MII;
      ++MII;
      return *this;
    }
    bundle_iterator operator--(int) {    // postdecrement operators...
      bundle_iterator tmp = *this;
      --*this;
      return tmp;
    }
    bundle_iterator operator++(int) {    // postincrement operators...
      bundle_iterator tmp = *this;
      ++*this;
      return tmp;
    }

    IterTy getInstrIterator() const {
      return MII;
    }
  };

  typedef Instructions::iterator                                 instr_iterator;
  typedef Instructions::const_iterator                     const_instr_iterator;
  typedef std::reverse_iterator<instr_iterator>          reverse_instr_iterator;
  typedef
  std::reverse_iterator<const_instr_iterator>      const_reverse_instr_iterator;

  typedef
  bundle_iterator<MachineInstr,instr_iterator>                         iterator;
  typedef
  bundle_iterator<const MachineInstr,const_instr_iterator>       const_iterator;
  typedef std::reverse_iterator<const_iterator>          const_reverse_iterator;
  typedef std::reverse_iterator<iterator>                      reverse_iterator;


  unsigned size() const { return (unsigned)Insts.size(); }
  bool empty() const { return Insts.empty(); }

  MachineInstr       &instr_front()       { return Insts.front(); }
  MachineInstr       &instr_back()        { return Insts.back();  }
  const MachineInstr &instr_front() const { return Insts.front(); }
  const MachineInstr &instr_back()  const { return Insts.back();  }

  MachineInstr       &front()             { return Insts.front(); }
  MachineInstr       &back()              { return *--end();      }
  const MachineInstr &front()       const { return Insts.front(); }
  const MachineInstr &back()        const { return *--end();      }

  instr_iterator                instr_begin()       { return Insts.begin();  }
  const_instr_iterator          instr_begin() const { return Insts.begin();  }
  instr_iterator                  instr_end()       { return Insts.end();    }
  const_instr_iterator            instr_end() const { return Insts.end();    }
  reverse_instr_iterator       instr_rbegin()       { return Insts.rbegin(); }
  const_reverse_instr_iterator instr_rbegin() const { return Insts.rbegin(); }
  reverse_instr_iterator       instr_rend  ()       { return Insts.rend();   }
  const_reverse_instr_iterator instr_rend  () const { return Insts.rend();   }

  iterator                begin()       { return instr_begin();  }
  const_iterator          begin() const { return instr_begin();  }
  iterator                end  ()       { return instr_end();    }
  const_iterator          end  () const { return instr_end();    }
  reverse_iterator       rbegin()       { return instr_rbegin(); }
  const_reverse_iterator rbegin() const { return instr_rbegin(); }
  reverse_iterator       rend  ()       { return instr_rend();   }
  const_reverse_iterator rend  () const { return instr_rend();   }

  /// Support for MachineInstr::getNextNode().
  static Instructions MachineBasicBlock::*getSublistAccess(MachineInstr *) {
    return &MachineBasicBlock::Insts;
  }

  inline iterator_range<iterator> terminators() {
    return make_range(getFirstTerminator(), end());
  }
  inline iterator_range<const_iterator> terminators() const {
    return make_range(getFirstTerminator(), end());
  }

  // Machine-CFG iterators
  typedef std::vector<MachineBasicBlock *>::iterator       pred_iterator;
  typedef std::vector<MachineBasicBlock *>::const_iterator const_pred_iterator;
  typedef std::vector<MachineBasicBlock *>::iterator       succ_iterator;
  typedef std::vector<MachineBasicBlock *>::const_iterator const_succ_iterator;
  typedef std::vector<MachineBasicBlock *>::reverse_iterator
                                                         pred_reverse_iterator;
  typedef std::vector<MachineBasicBlock *>::const_reverse_iterator
                                                   const_pred_reverse_iterator;
  typedef std::vector<MachineBasicBlock *>::reverse_iterator
                                                         succ_reverse_iterator;
  typedef std::vector<MachineBasicBlock *>::const_reverse_iterator
                                                   const_succ_reverse_iterator;
  pred_iterator        pred_begin()       { return Predecessors.begin(); }
  const_pred_iterator  pred_begin() const { return Predecessors.begin(); }
  pred_iterator        pred_end()         { return Predecessors.end();   }
  const_pred_iterator  pred_end()   const { return Predecessors.end();   }
  pred_reverse_iterator        pred_rbegin()
                                          { return Predecessors.rbegin();}
  const_pred_reverse_iterator  pred_rbegin() const
                                          { return Predecessors.rbegin();}
  pred_reverse_iterator        pred_rend()
                                          { return Predecessors.rend();  }
  const_pred_reverse_iterator  pred_rend()   const
                                          { return Predecessors.rend();  }
  unsigned             pred_size()  const {
    return (unsigned)Predecessors.size();
  }
  bool                 pred_empty() const { return Predecessors.empty(); }
  succ_iterator        succ_begin()       { return Successors.begin();   }
  const_succ_iterator  succ_begin() const { return Successors.begin();   }
  succ_iterator        succ_end()         { return Successors.end();     }
  const_succ_iterator  succ_end()   const { return Successors.end();     }
  succ_reverse_iterator        succ_rbegin()
                                          { return Successors.rbegin();  }
  const_succ_reverse_iterator  succ_rbegin() const
                                          { return Successors.rbegin();  }
  succ_reverse_iterator        succ_rend()
                                          { return Successors.rend();    }
  const_succ_reverse_iterator  succ_rend()   const
                                          { return Successors.rend();    }
  unsigned             succ_size()  const {
    return (unsigned)Successors.size();
  }
  bool                 succ_empty() const { return Successors.empty();   }

  inline iterator_range<pred_iterator> predecessors() {
    return make_range(pred_begin(), pred_end());
  }
  inline iterator_range<const_pred_iterator> predecessors() const {
    return make_range(pred_begin(), pred_end());
  }
  inline iterator_range<succ_iterator> successors() {
    return make_range(succ_begin(), succ_end());
  }
  inline iterator_range<const_succ_iterator> successors() const {
    return make_range(succ_begin(), succ_end());
  }

  // LiveIn management methods.

  /// Adds the specified register as a live in. Note that it is an error to add
  /// the same register to the same set more than once unless the intention is
  /// to call sortUniqueLiveIns after all registers are added.
  void addLiveIn(MCPhysReg PhysReg, LaneBitmask LaneMask = ~0u) {
    LiveIns.push_back(RegisterMaskPair(PhysReg, LaneMask));
  }
  void addLiveIn(const RegisterMaskPair &RegMaskPair) {
    LiveIns.push_back(RegMaskPair);
  }

  /// Sorts and uniques the LiveIns vector. It can be significantly faster to do
  /// this than repeatedly calling isLiveIn before calling addLiveIn for every
  /// LiveIn insertion.
  void sortUniqueLiveIns();

  /// Add PhysReg as live in to this block, and ensure that there is a copy of
  /// PhysReg to a virtual register of class RC. Return the virtual register
  /// that is a copy of the live in PhysReg.
  unsigned addLiveIn(MCPhysReg PhysReg, const TargetRegisterClass *RC);

  /// Remove the specified register from the live in set.
  void removeLiveIn(MCPhysReg Reg, LaneBitmask LaneMask = ~0u);

  /// Return true if the specified register is in the live in set.
  bool isLiveIn(MCPhysReg Reg, LaneBitmask LaneMask = ~0u) const;

  // Iteration support for live in sets.  These sets are kept in sorted
  // order by their register number.
  typedef LiveInVector::const_iterator livein_iterator;
  livein_iterator livein_begin() const { return LiveIns.begin(); }
  livein_iterator livein_end()   const { return LiveIns.end(); }
  bool            livein_empty() const { return LiveIns.empty(); }
  iterator_range<livein_iterator> liveins() const {
    return make_range(livein_begin(), livein_end());
  }

  /// Get the clobber mask for the start of this basic block. Funclets use this
  /// to prevent register allocation across funclet transitions.
  const uint32_t *getBeginClobberMask(const TargetRegisterInfo *TRI) const;

  /// Get the clobber mask for the end of the basic block.
  /// \see getBeginClobberMask()
  const uint32_t *getEndClobberMask(const TargetRegisterInfo *TRI) const;

  /// Return alignment of the basic block. The alignment is specified as
  /// log2(bytes).
  unsigned getAlignment() const { return Alignment; }

  /// Set alignment of the basic block. The alignment is specified as
  /// log2(bytes).
  void setAlignment(unsigned Align) { Alignment = Align; }

  /// Returns true if the block is a landing pad. That is this basic block is
  /// entered via an exception handler.
  bool isEHPad() const { return IsEHPad; }

  /// Indicates the block is a landing pad.  That is this basic block is entered
  /// via an exception handler.
  void setIsEHPad(bool V = true) { IsEHPad = V; }

  /// If this block has a successor that is a landing pad, return it. Otherwise
  /// return NULL.
  const MachineBasicBlock *getLandingPadSuccessor() const;

  bool hasEHPadSuccessor() const;

  /// Returns true if this is the entry block of an EH funclet.
  bool isEHFuncletEntry() const { return IsEHFuncletEntry; }

  /// Indicates if this is the entry block of an EH funclet.
  void setIsEHFuncletEntry(bool V = true) { IsEHFuncletEntry = V; }

  /// Returns true if this is the entry block of a cleanup funclet.
  bool isCleanupFuncletEntry() const { return IsCleanupFuncletEntry; }

  /// Indicates if this is the entry block of a cleanup funclet.
  void setIsCleanupFuncletEntry(bool V = true) { IsCleanupFuncletEntry = V; }

  // Code Layout methods.

  /// Move 'this' block before or after the specified block.  This only moves
  /// the block, it does not modify the CFG or adjust potential fall-throughs at
  /// the end of the block.
  void moveBefore(MachineBasicBlock *NewAfter);
  void moveAfter(MachineBasicBlock *NewBefore);

  /// Update the terminator instructions in block to account for changes to the
  /// layout. If the block previously used a fallthrough, it may now need a
  /// branch, and if it previously used branching it may now be able to use a
  /// fallthrough.
  void updateTerminator();

  // Machine-CFG mutators

  /// Add Succ as a successor of this MachineBasicBlock.  The Predecessors list
  /// of Succ is automatically updated. PROB parameter is stored in
  /// Probabilities list. The default probability is set as unknown. Mixing
  /// known and unknown probabilities in successor list is not allowed. When all
  /// successors have unknown probabilities, 1 / N is returned as the
  /// probability for each successor, where N is the number of successors.
  ///
  /// Note that duplicate Machine CFG edges are not allowed.
  void addSuccessor(MachineBasicBlock *Succ,
                    BranchProbability Prob = BranchProbability::getUnknown());

  /// Add Succ as a successor of this MachineBasicBlock.  The Predecessors list
  /// of Succ is automatically updated. The probability is not provided because
  /// BPI is not available (e.g. -O0 is used), in which case edge probabilities
  /// won't be used. Using this interface can save some space.
  void addSuccessorWithoutProb(MachineBasicBlock *Succ);

  /// Set successor probability of a given iterator.
  void setSuccProbability(succ_iterator I, BranchProbability Prob);

  /// Normalize probabilities of all successors so that the sum of them becomes
  /// one. This is usually done when the current update on this MBB is done, and
  /// the sum of its successors' probabilities is not guaranteed to be one. The
  /// user is responsible for the correct use of this function.
  /// MBB::removeSuccessor() has an option to do this automatically.
  void normalizeSuccProbs() {
    BranchProbability::normalizeProbabilities(Probs.begin(), Probs.end());
  }

  /// Validate successors' probabilities and check if the sum of them is
  /// approximate one. This only works in DEBUG mode.
  void validateSuccProbs() const;

  /// Remove successor from the successors list of this MachineBasicBlock. The
  /// Predecessors list of Succ is automatically updated.
  /// If NormalizeSuccProbs is true, then normalize successors' probabilities
  /// after the successor is removed.
  void removeSuccessor(MachineBasicBlock *Succ,
                       bool NormalizeSuccProbs = false);

  /// Remove specified successor from the successors list of this
  /// MachineBasicBlock. The Predecessors list of Succ is automatically updated.
  /// If NormalizeSuccProbs is true, then normalize successors' probabilities
  /// after the successor is removed.
  /// Return the iterator to the element after the one removed.
  succ_iterator removeSuccessor(succ_iterator I,
                                bool NormalizeSuccProbs = false);

  /// Replace successor OLD with NEW and update probability info.
  void replaceSuccessor(MachineBasicBlock *Old, MachineBasicBlock *New);

  /// Transfers all the successors from MBB to this machine basic block (i.e.,
  /// copies all the successors FromMBB and remove all the successors from
  /// FromMBB).
  void transferSuccessors(MachineBasicBlock *FromMBB);

  /// Transfers all the successors, as in transferSuccessors, and update PHI
  /// operands in the successor blocks which refer to FromMBB to refer to this.
  void transferSuccessorsAndUpdatePHIs(MachineBasicBlock *FromMBB);

  /// Return true if any of the successors have probabilities attached to them.
  bool hasSuccessorProbabilities() const { return !Probs.empty(); }

  /// Return true if the specified MBB is a predecessor of this block.
  bool isPredecessor(const MachineBasicBlock *MBB) const;

  /// Return true if the specified MBB is a successor of this block.
  bool isSuccessor(const MachineBasicBlock *MBB) const;

  /// Return true if the specified MBB will be emitted immediately after this
  /// block, such that if this block exits by falling through, control will
  /// transfer to the specified MBB. Note that MBB need not be a successor at
  /// all, for example if this block ends with an unconditional branch to some
  /// other block.
  bool isLayoutSuccessor(const MachineBasicBlock *MBB) const;

  /// Return true if the block can implicitly transfer control to the block
  /// after it by falling off the end of it.  This should return false if it can
  /// reach the block after it, but it uses an explicit branch to do so (e.g., a
  /// table jump).  True is a conservative answer.
  bool canFallThrough();

  /// Returns a pointer to the first instruction in this block that is not a
  /// PHINode instruction. When adding instructions to the beginning of the
  /// basic block, they should be added before the returned value, not before
  /// the first instruction, which might be PHI.
  /// Returns end() is there's no non-PHI instruction.
  iterator getFirstNonPHI();

  /// Return the first instruction in MBB after I that is not a PHI or a label.
  /// This is the correct point to insert copies at the beginning of a basic
  /// block.
  iterator SkipPHIsAndLabels(iterator I);

  /// Returns an iterator to the first terminator instruction of this basic
  /// block. If a terminator does not exist, it returns end().
  iterator getFirstTerminator();
  const_iterator getFirstTerminator() const {
    return const_cast<MachineBasicBlock *>(this)->getFirstTerminator();
  }

  /// Same getFirstTerminator but it ignores bundles and return an
  /// instr_iterator instead.
  instr_iterator getFirstInstrTerminator();

  /// Returns an iterator to the first non-debug instruction in the basic block,
  /// or end().
  iterator getFirstNonDebugInstr();
  const_iterator getFirstNonDebugInstr() const {
    return const_cast<MachineBasicBlock *>(this)->getFirstNonDebugInstr();
  }

  /// Returns an iterator to the last non-debug instruction in the basic block,
  /// or end().
  iterator getLastNonDebugInstr();
  const_iterator getLastNonDebugInstr() const {
    return const_cast<MachineBasicBlock *>(this)->getLastNonDebugInstr();
  }

  /// Convenience function that returns true if the block ends in a return
  /// instruction.
  bool isReturnBlock() const {
    return !empty() && back().isReturn();
  }

  /// Split the critical edge from this block to the given successor block, and
  /// return the newly created block, or null if splitting is not possible.
  ///
  /// This function updates LiveVariables, MachineDominatorTree, and
  /// MachineLoopInfo, as applicable.
  MachineBasicBlock *SplitCriticalEdge(MachineBasicBlock *Succ, Pass *P);

  void pop_front() { Insts.pop_front(); }
  void pop_back() { Insts.pop_back(); }
  void push_back(MachineInstr *MI) { Insts.push_back(MI); }

  /// Insert MI into the instruction list before I, possibly inside a bundle.
  ///
  /// If the insertion point is inside a bundle, MI will be added to the bundle,
  /// otherwise MI will not be added to any bundle. That means this function
  /// alone can't be used to prepend or append instructions to bundles. See
  /// MIBundleBuilder::insert() for a more reliable way of doing that.
  instr_iterator insert(instr_iterator I, MachineInstr *M);

  /// Insert a range of instructions into the instruction list before I.
  template<typename IT>
  void insert(iterator I, IT S, IT E) {
    assert((I == end() || I->getParent() == this) &&
           "iterator points outside of basic block");
    Insts.insert(I.getInstrIterator(), S, E);
  }

  /// Insert MI into the instruction list before I.
  iterator insert(iterator I, MachineInstr *MI) {
    assert((I == end() || I->getParent() == this) &&
           "iterator points outside of basic block");
    assert(!MI->isBundledWithPred() && !MI->isBundledWithSucc() &&
           "Cannot insert instruction with bundle flags");
    return Insts.insert(I.getInstrIterator(), MI);
  }

  /// Insert MI into the instruction list after I.
  iterator insertAfter(iterator I, MachineInstr *MI) {
    assert((I == end() || I->getParent() == this) &&
           "iterator points outside of basic block");
    assert(!MI->isBundledWithPred() && !MI->isBundledWithSucc() &&
           "Cannot insert instruction with bundle flags");
    return Insts.insertAfter(I.getInstrIterator(), MI);
  }

  /// Remove an instruction from the instruction list and delete it.
  ///
  /// If the instruction is part of a bundle, the other instructions in the
  /// bundle will still be bundled after removing the single instruction.
  instr_iterator erase(instr_iterator I);

  /// Remove an instruction from the instruction list and delete it.
  ///
  /// If the instruction is part of a bundle, the other instructions in the
  /// bundle will still be bundled after removing the single instruction.
  instr_iterator erase_instr(MachineInstr *I) {
    return erase(instr_iterator(I));
  }

  /// Remove a range of instructions from the instruction list and delete them.
  iterator erase(iterator I, iterator E) {
    return Insts.erase(I.getInstrIterator(), E.getInstrIterator());
  }

  /// Remove an instruction or bundle from the instruction list and delete it.
  ///
  /// If I points to a bundle of instructions, they are all erased.
  iterator erase(iterator I) {
    return erase(I, std::next(I));
  }

  /// Remove an instruction from the instruction list and delete it.
  ///
  /// If I is the head of a bundle of instructions, the whole bundle will be
  /// erased.
  iterator erase(MachineInstr *I) {
    return erase(iterator(I));
  }

  /// Remove the unbundled instruction from the instruction list without
  /// deleting it.
  ///
  /// This function can not be used to remove bundled instructions, use
  /// remove_instr to remove individual instructions from a bundle.
  MachineInstr *remove(MachineInstr *I) {
    assert(!I->isBundled() && "Cannot remove bundled instructions");
    return Insts.remove(instr_iterator(I));
  }

  /// Remove the possibly bundled instruction from the instruction list
  /// without deleting it.
  ///
  /// If the instruction is part of a bundle, the other instructions in the
  /// bundle will still be bundled after removing the single instruction.
  MachineInstr *remove_instr(MachineInstr *I);

  void clear() {
    Insts.clear();
  }

  /// Take an instruction from MBB 'Other' at the position From, and insert it
  /// into this MBB right before 'Where'.
  ///
  /// If From points to a bundle of instructions, the whole bundle is moved.
  void splice(iterator Where, MachineBasicBlock *Other, iterator From) {
    // The range splice() doesn't allow noop moves, but this one does.
    if (Where != From)
      splice(Where, Other, From, std::next(From));
  }

  /// Take a block of instructions from MBB 'Other' in the range [From, To),
  /// and insert them into this MBB right before 'Where'.
  ///
  /// The instruction at 'Where' must not be included in the range of
  /// instructions to move.
  void splice(iterator Where, MachineBasicBlock *Other,
              iterator From, iterator To) {
    Insts.splice(Where.getInstrIterator(), Other->Insts,
                 From.getInstrIterator(), To.getInstrIterator());
  }

  /// This method unlinks 'this' from the containing function, and returns it,
  /// but does not delete it.
  MachineBasicBlock *removeFromParent();

  /// This method unlinks 'this' from the containing function and deletes it.
  void eraseFromParent();

  /// Given a machine basic block that branched to 'Old', change the code and
  /// CFG so that it branches to 'New' instead.
  void ReplaceUsesOfBlockWith(MachineBasicBlock *Old, MachineBasicBlock *New);

  /// Various pieces of code can cause excess edges in the CFG to be inserted.
  /// If we have proven that MBB can only branch to DestA and DestB, remove any
  /// other MBB successors from the CFG. DestA and DestB can be null. Besides
  /// DestA and DestB, retain other edges leading to LandingPads (currently
  /// there can be only one; we don't check or require that here). Note it is
  /// possible that DestA and/or DestB are LandingPads.
  bool CorrectExtraCFGEdges(MachineBasicBlock *DestA,
                            MachineBasicBlock *DestB,
                            bool IsCond);

  /// Find the next valid DebugLoc starting at MBBI, skipping any DBG_VALUE
  /// instructions.  Return UnknownLoc if there is none.
  DebugLoc findDebugLoc(instr_iterator MBBI);
  DebugLoc findDebugLoc(iterator MBBI) {
    return findDebugLoc(MBBI.getInstrIterator());
  }

  /// Possible outcome of a register liveness query to computeRegisterLiveness()
  enum LivenessQueryResult {
    LQR_Live,   ///< Register is known to be (at least partially) live.
    LQR_Dead,   ///< Register is known to be fully dead.
    LQR_Unknown ///< Register liveness not decidable from local neighborhood.
  };

  /// Return whether (physical) register \p Reg has been <def>ined and not
  /// <kill>ed as of just before \p Before.
  ///
  /// Search is localised to a neighborhood of \p Neighborhood instructions
  /// before (searching for defs or kills) and \p Neighborhood instructions
  /// after (searching just for defs) \p Before.
  ///
  /// \p Reg must be a physical register.
  LivenessQueryResult computeRegisterLiveness(const TargetRegisterInfo *TRI,
                                              unsigned Reg,
                                              const_iterator Before,
                                              unsigned Neighborhood=10) const;

  // Debugging methods.
  void dump() const;
  void print(raw_ostream &OS, SlotIndexes* = nullptr) const;
  void print(raw_ostream &OS, ModuleSlotTracker &MST,
             SlotIndexes * = nullptr) const;

  // Printing method used by LoopInfo.
  void printAsOperand(raw_ostream &OS, bool PrintType = true) const;

  /// MachineBasicBlocks are uniquely numbered at the function level, unless
  /// they're not in a MachineFunction yet, in which case this will return -1.
  int getNumber() const { return Number; }
  void setNumber(int N) { Number = N; }

  /// Return the MCSymbol for this basic block.
  MCSymbol *getSymbol() const;


private:
  /// Return probability iterator corresponding to the I successor iterator.
  probability_iterator getProbabilityIterator(succ_iterator I);
  const_probability_iterator
  getProbabilityIterator(const_succ_iterator I) const;

  friend class MachineBranchProbabilityInfo;
  friend class MIPrinter;

  /// Return probability of the edge from this block to MBB. This method should
  /// NOT be called directly, but by using getEdgeProbability method from
  /// MachineBranchProbabilityInfo class.
  BranchProbability getSuccProbability(const_succ_iterator Succ) const;

  // Methods used to maintain doubly linked list of blocks...
  friend struct ilist_traits<MachineBasicBlock>;

  // Machine-CFG mutators

  /// Remove Pred as a predecessor of this MachineBasicBlock. Don't do this
  /// unless you know what you're doing, because it doesn't update Pred's
  /// successors list. Use Pred->addSuccessor instead.
  void addPredecessor(MachineBasicBlock *Pred);

  /// Remove Pred as a predecessor of this MachineBasicBlock. Don't do this
  /// unless you know what you're doing, because it doesn't update Pred's
  /// successors list. Use Pred->removeSuccessor instead.
  void removePredecessor(MachineBasicBlock *Pred);
};

raw_ostream& operator<<(raw_ostream &OS, const MachineBasicBlock &MBB);

// This is useful when building IndexedMaps keyed on basic block pointers.
struct MBB2NumberFunctor :
  public std::unary_function<const MachineBasicBlock*, unsigned> {
  unsigned operator()(const MachineBasicBlock *MBB) const {
    return MBB->getNumber();
  }
};

//===--------------------------------------------------------------------===//
// GraphTraits specializations for machine basic block graphs (machine-CFGs)
//===--------------------------------------------------------------------===//

// Provide specializations of GraphTraits to be able to treat a
// MachineFunction as a graph of MachineBasicBlocks.
//

template <> struct GraphTraits<MachineBasicBlock *> {
  typedef MachineBasicBlock NodeType;
  typedef MachineBasicBlock::succ_iterator ChildIteratorType;

  static NodeType *getEntryNode(MachineBasicBlock *BB) { return BB; }
  static inline ChildIteratorType child_begin(NodeType *N) {
    return N->succ_begin();
  }
  static inline ChildIteratorType child_end(NodeType *N) {
    return N->succ_end();
  }
};

template <> struct GraphTraits<const MachineBasicBlock *> {
  typedef const MachineBasicBlock NodeType;
  typedef MachineBasicBlock::const_succ_iterator ChildIteratorType;

  static NodeType *getEntryNode(const MachineBasicBlock *BB) { return BB; }
  static inline ChildIteratorType child_begin(NodeType *N) {
    return N->succ_begin();
  }
  static inline ChildIteratorType child_end(NodeType *N) {
    return N->succ_end();
  }
};

// Provide specializations of GraphTraits to be able to treat a
// MachineFunction as a graph of MachineBasicBlocks and to walk it
// in inverse order.  Inverse order for a function is considered
// to be when traversing the predecessor edges of a MBB
// instead of the successor edges.
//
template <> struct GraphTraits<Inverse<MachineBasicBlock*> > {
  typedef MachineBasicBlock NodeType;
  typedef MachineBasicBlock::pred_iterator ChildIteratorType;
  static NodeType *getEntryNode(Inverse<MachineBasicBlock *> G) {
    return G.Graph;
  }
  static inline ChildIteratorType child_begin(NodeType *N) {
    return N->pred_begin();
  }
  static inline ChildIteratorType child_end(NodeType *N) {
    return N->pred_end();
  }
};

template <> struct GraphTraits<Inverse<const MachineBasicBlock*> > {
  typedef const MachineBasicBlock NodeType;
  typedef MachineBasicBlock::const_pred_iterator ChildIteratorType;
  static NodeType *getEntryNode(Inverse<const MachineBasicBlock*> G) {
    return G.Graph;
  }
  static inline ChildIteratorType child_begin(NodeType *N) {
    return N->pred_begin();
  }
  static inline ChildIteratorType child_end(NodeType *N) {
    return N->pred_end();
  }
};



/// MachineInstrSpan provides an interface to get an iteration range
/// containing the instruction it was initialized with, along with all
/// those instructions inserted prior to or following that instruction
/// at some point after the MachineInstrSpan is constructed.
class MachineInstrSpan {
  MachineBasicBlock &MBB;
  MachineBasicBlock::iterator I, B, E;
public:
  MachineInstrSpan(MachineBasicBlock::iterator I)
    : MBB(*I->getParent()),
      I(I),
      B(I == MBB.begin() ? MBB.end() : std::prev(I)),
      E(std::next(I)) {}

  MachineBasicBlock::iterator begin() {
    return B == MBB.end() ? MBB.begin() : std::next(B);
  }
  MachineBasicBlock::iterator end() { return E; }
  bool empty() { return begin() == end(); }

  MachineBasicBlock::iterator getInitial() { return I; }
};

} // End llvm namespace

#endif
