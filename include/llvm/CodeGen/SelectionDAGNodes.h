//===-- llvm/CodeGen/SelectionDAGNodes.h - SelectionDAG Nodes ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares the SDNode class and derived classes, which are used to
// represent the nodes and operations present in a SelectionDAG.  These nodes
// and operations are machine code level operations, with some similarities to
// the GCC RTL representation.
//
// Clients should include the SelectionDAG.h file instead of this file directly.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_SELECTIONDAGNODES_H
#define LLVM_CODEGEN_SELECTIONDAGNODES_H

#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/GraphTraits.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/ilist_node.h"
#include "llvm/CodeGen/ISDOpcodes.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/DataTypes.h"
#include "llvm/Support/DebugLoc.h"
#include "llvm/Support/MathExtras.h"
#include <cassert>

namespace llvm {

class SelectionDAG;
class GlobalValue;
class MachineBasicBlock;
class MachineConstantPoolValue;
class SDNode;
class Value;
class MCSymbol;
template <typename T> struct DenseMapInfo;
template <typename T> struct simplify_type;
template <typename T> struct ilist_traits;

void checkForCycles(const SDNode *N);

/// SDVTList - This represents a list of ValueType's that has been intern'd by
/// a SelectionDAG.  Instances of this simple value class are returned by
/// SelectionDAG::getVTList(...).
///
struct SDVTList {
  const EVT *VTs;
  unsigned int NumVTs;
};

namespace ISD {
  /// Node predicates

  /// isBuildVectorAllOnes - Return true if the specified node is a
  /// BUILD_VECTOR where all of the elements are ~0 or undef.
  bool isBuildVectorAllOnes(const SDNode *N);

  /// isBuildVectorAllZeros - Return true if the specified node is a
  /// BUILD_VECTOR where all of the elements are 0 or undef.
  bool isBuildVectorAllZeros(const SDNode *N);

  /// isScalarToVector - Return true if the specified node is a
  /// ISD::SCALAR_TO_VECTOR node or a BUILD_VECTOR node where only the low
  /// element is not an undef.
  bool isScalarToVector(const SDNode *N);

  /// allOperandsUndef - Return true if the node has at least one operand
  /// and all operands of the specified node are ISD::UNDEF.
  bool allOperandsUndef(const SDNode *N);
}  // end llvm:ISD namespace

//===----------------------------------------------------------------------===//
/// SDValue - Unlike LLVM values, Selection DAG nodes may return multiple
/// values as the result of a computation.  Many nodes return multiple values,
/// from loads (which define a token and a return value) to ADDC (which returns
/// a result and a carry value), to calls (which may return an arbitrary number
/// of values).
///
/// As such, each use of a SelectionDAG computation must indicate the node that
/// computes it as well as which return value to use from that node.  This pair
/// of information is represented with the SDValue value type.
///
class SDValue {
  SDNode *Node;       // The node defining the value we are using.
  unsigned ResNo;     // Which return value of the node we are using.
public:
  SDValue() : Node(0), ResNo(0) {}
  SDValue(SDNode *node, unsigned resno) : Node(node), ResNo(resno) {}

  /// get the index which selects a specific result in the SDNode
  unsigned getResNo() const { return ResNo; }

  /// get the SDNode which holds the desired result
  SDNode *getNode() const { return Node; }

  /// set the SDNode
  void setNode(SDNode *N) { Node = N; }

  inline SDNode *operator->() const { return Node; }

  bool operator==(const SDValue &O) const {
    return Node == O.Node && ResNo == O.ResNo;
  }
  bool operator!=(const SDValue &O) const {
    return !operator==(O);
  }
  bool operator<(const SDValue &O) const {
    return Node < O.Node || (Node == O.Node && ResNo < O.ResNo);
  }

  SDValue getValue(unsigned R) const {
    return SDValue(Node, R);
  }

  // isOperandOf - Return true if this node is an operand of N.
  bool isOperandOf(SDNode *N) const;

  /// getValueType - Return the ValueType of the referenced return value.
  ///
  inline EVT getValueType() const;

  /// Return the simple ValueType of the referenced return value.
  MVT getSimpleValueType() const {
    return getValueType().getSimpleVT();
  }

  /// getValueSizeInBits - Returns the size of the value in bits.
  ///
  unsigned getValueSizeInBits() const {
    return getValueType().getSizeInBits();
  }

  // Forwarding methods - These forward to the corresponding methods in SDNode.
  inline unsigned getOpcode() const;
  inline unsigned getNumOperands() const;
  inline const SDValue &getOperand(unsigned i) const;
  inline uint64_t getConstantOperandVal(unsigned i) const;
  inline bool isTargetMemoryOpcode() const;
  inline bool isTargetOpcode() const;
  inline bool isMachineOpcode() const;
  inline unsigned getMachineOpcode() const;
  inline const DebugLoc getDebugLoc() const;
  inline void dump() const;
  inline void dumpr() const;

  /// reachesChainWithoutSideEffects - Return true if this operand (which must
  /// be a chain) reaches the specified operand without crossing any
  /// side-effecting instructions.  In practice, this looks through token
  /// factors and non-volatile loads.  In order to remain efficient, this only
  /// looks a couple of nodes in, it does not do an exhaustive search.
  bool reachesChainWithoutSideEffects(SDValue Dest,
                                      unsigned Depth = 2) const;

  /// use_empty - Return true if there are no nodes using value ResNo
  /// of Node.
  ///
  inline bool use_empty() const;

  /// hasOneUse - Return true if there is exactly one node using value
  /// ResNo of Node.
  ///
  inline bool hasOneUse() const;
};


template<> struct DenseMapInfo<SDValue> {
  static inline SDValue getEmptyKey() {
    return SDValue((SDNode*)-1, -1U);
  }
  static inline SDValue getTombstoneKey() {
    return SDValue((SDNode*)-1, 0);
  }
  static unsigned getHashValue(const SDValue &Val) {
    return ((unsigned)((uintptr_t)Val.getNode() >> 4) ^
            (unsigned)((uintptr_t)Val.getNode() >> 9)) + Val.getResNo();
  }
  static bool isEqual(const SDValue &LHS, const SDValue &RHS) {
    return LHS == RHS;
  }
};
template <> struct isPodLike<SDValue> { static const bool value = true; };


/// simplify_type specializations - Allow casting operators to work directly on
/// SDValues as if they were SDNode*'s.
template<> struct simplify_type<SDValue> {
  typedef SDNode* SimpleType;
  static SimpleType getSimplifiedValue(SDValue &Val) {
    return Val.getNode();
  }
};
template<> struct simplify_type<const SDValue> {
  typedef /*const*/ SDNode* SimpleType;
  static SimpleType getSimplifiedValue(const SDValue &Val) {
    return Val.getNode();
  }
};

/// SDUse - Represents a use of a SDNode. This class holds an SDValue,
/// which records the SDNode being used and the result number, a
/// pointer to the SDNode using the value, and Next and Prev pointers,
/// which link together all the uses of an SDNode.
///
class SDUse {
  /// Val - The value being used.
  SDValue Val;
  /// User - The user of this value.
  SDNode *User;
  /// Prev, Next - Pointers to the uses list of the SDNode referred by
  /// this operand.
  SDUse **Prev, *Next;

  SDUse(const SDUse &U) LLVM_DELETED_FUNCTION;
  void operator=(const SDUse &U) LLVM_DELETED_FUNCTION;

public:
  SDUse() : Val(), User(NULL), Prev(NULL), Next(NULL) {}

  /// Normally SDUse will just implicitly convert to an SDValue that it holds.
  operator const SDValue&() const { return Val; }

  /// If implicit conversion to SDValue doesn't work, the get() method returns
  /// the SDValue.
  const SDValue &get() const { return Val; }

  /// getUser - This returns the SDNode that contains this Use.
  SDNode *getUser() { return User; }

  /// getNext - Get the next SDUse in the use list.
  SDUse *getNext() const { return Next; }

  /// getNode - Convenience function for get().getNode().
  SDNode *getNode() const { return Val.getNode(); }
  /// getResNo - Convenience function for get().getResNo().
  unsigned getResNo() const { return Val.getResNo(); }
  /// getValueType - Convenience function for get().getValueType().
  EVT getValueType() const { return Val.getValueType(); }

  /// operator== - Convenience function for get().operator==
  bool operator==(const SDValue &V) const {
    return Val == V;
  }

  /// operator!= - Convenience function for get().operator!=
  bool operator!=(const SDValue &V) const {
    return Val != V;
  }

  /// operator< - Convenience function for get().operator<
  bool operator<(const SDValue &V) const {
    return Val < V;
  }

private:
  friend class SelectionDAG;
  friend class SDNode;

  void setUser(SDNode *p) { User = p; }

  /// set - Remove this use from its existing use list, assign it the
  /// given value, and add it to the new value's node's use list.
  inline void set(const SDValue &V);
  /// setInitial - like set, but only supports initializing a newly-allocated
  /// SDUse with a non-null value.
  inline void setInitial(const SDValue &V);
  /// setNode - like set, but only sets the Node portion of the value,
  /// leaving the ResNo portion unmodified.
  inline void setNode(SDNode *N);

  void addToList(SDUse **List) {
    Next = *List;
    if (Next) Next->Prev = &Next;
    Prev = List;
    *List = this;
  }

  void removeFromList() {
    *Prev = Next;
    if (Next) Next->Prev = Prev;
  }
};

/// simplify_type specializations - Allow casting operators to work directly on
/// SDValues as if they were SDNode*'s.
template<> struct simplify_type<SDUse> {
  typedef SDNode* SimpleType;
  static SimpleType getSimplifiedValue(SDUse &Val) {
    return Val.getNode();
  }
};


/// SDNode - Represents one node in the SelectionDAG.
///
class SDNode : public FoldingSetNode, public ilist_node<SDNode> {
private:
  /// NodeType - The operation that this node performs.
  ///
  int16_t NodeType;

  /// OperandsNeedDelete - This is true if OperandList was new[]'d.  If true,
  /// then they will be delete[]'d when the node is destroyed.
  uint16_t OperandsNeedDelete : 1;

  /// HasDebugValue - This tracks whether this node has one or more dbg_value
  /// nodes corresponding to it.
  uint16_t HasDebugValue : 1;

protected:
  /// SubclassData - This member is defined by this class, but is not used for
  /// anything.  Subclasses can use it to hold whatever state they find useful.
  /// This field is initialized to zero by the ctor.
  uint16_t SubclassData : 14;

private:
  /// NodeId - Unique id per SDNode in the DAG.
  int NodeId;

  /// OperandList - The values that are used by this operation.
  ///
  SDUse *OperandList;

  /// ValueList - The types of the values this node defines.  SDNode's may
  /// define multiple values simultaneously.
  const EVT *ValueList;

  /// UseList - List of uses for this SDNode.
  SDUse *UseList;

  /// NumOperands/NumValues - The number of entries in the Operand/Value list.
  unsigned short NumOperands, NumValues;

  /// debugLoc - source line information.
  DebugLoc debugLoc;

  // The ordering of the SDNodes. It roughly corresponds to the ordering of the
  // original LLVM instructions.
  // This is used for turning off scheduling, because we'll forgo
  // the normal scheduling algorithms and output the instructions according to
  // this ordering.
  unsigned IROrder;

  /// getValueTypeList - Return a pointer to the specified value type.
  static const EVT *getValueTypeList(EVT VT);

  friend class SelectionDAG;
  friend struct ilist_traits<SDNode>;

public:
  //===--------------------------------------------------------------------===//
  //  Accessors
  //

  /// getOpcode - Return the SelectionDAG opcode value for this node. For
  /// pre-isel nodes (those for which isMachineOpcode returns false), these
  /// are the opcode values in the ISD and <target>ISD namespaces. For
  /// post-isel opcodes, see getMachineOpcode.
  unsigned getOpcode()  const { return (unsigned short)NodeType; }

  /// isTargetOpcode - Test if this node has a target-specific opcode (in the
  /// \<target\>ISD namespace).
  bool isTargetOpcode() const { return NodeType >= ISD::BUILTIN_OP_END; }

  /// isTargetMemoryOpcode - Test if this node has a target-specific 
  /// memory-referencing opcode (in the \<target\>ISD namespace and
  /// greater than FIRST_TARGET_MEMORY_OPCODE).
  bool isTargetMemoryOpcode() const {
    return NodeType >= ISD::FIRST_TARGET_MEMORY_OPCODE;
  }

  /// isMachineOpcode - Test if this node has a post-isel opcode, directly
  /// corresponding to a MachineInstr opcode.
  bool isMachineOpcode() const { return NodeType < 0; }

  /// getMachineOpcode - This may only be called if isMachineOpcode returns
  /// true. It returns the MachineInstr opcode value that the node's opcode
  /// corresponds to.
  unsigned getMachineOpcode() const {
    assert(isMachineOpcode() && "Not a MachineInstr opcode!");
    return ~NodeType;
  }

  /// getHasDebugValue - get this bit.
  bool getHasDebugValue() const { return HasDebugValue; }

  /// setHasDebugValue - set this bit.
  void setHasDebugValue(bool b) { HasDebugValue = b; }

  /// use_empty - Return true if there are no uses of this node.
  ///
  bool use_empty() const { return UseList == NULL; }

  /// hasOneUse - Return true if there is exactly one use of this node.
  ///
  bool hasOneUse() const {
    return !use_empty() && llvm::next(use_begin()) == use_end();
  }

  /// use_size - Return the number of uses of this node. This method takes
  /// time proportional to the number of uses.
  ///
  size_t use_size() const { return std::distance(use_begin(), use_end()); }

  /// getNodeId - Return the unique node id.
  ///
  int getNodeId() const { return NodeId; }

  /// setNodeId - Set unique node id.
  void setNodeId(int Id) { NodeId = Id; }

  /// getIROrder - Return the node ordering.
  ///
  unsigned getIROrder() const { return IROrder; }

  /// setIROrder - Set the node ordering.
  ///
  void setIROrder(unsigned Order) { IROrder = Order; }

  /// getDebugLoc - Return the source location info.
  const DebugLoc getDebugLoc() const { return debugLoc; }

  /// setDebugLoc - Set source location info.  Try to avoid this, putting
  /// it in the constructor is preferable.
  void setDebugLoc(const DebugLoc dl) { debugLoc = dl; }

  /// use_iterator - This class provides iterator support for SDUse
  /// operands that use a specific SDNode.
  class use_iterator
    : public std::iterator<std::forward_iterator_tag, SDUse, ptrdiff_t> {
    SDUse *Op;
    explicit use_iterator(SDUse *op) : Op(op) {
    }
    friend class SDNode;
  public:
    typedef std::iterator<std::forward_iterator_tag,
                          SDUse, ptrdiff_t>::reference reference;
    typedef std::iterator<std::forward_iterator_tag,
                          SDUse, ptrdiff_t>::pointer pointer;

    use_iterator(const use_iterator &I) : Op(I.Op) {}
    use_iterator() : Op(0) {}

    bool operator==(const use_iterator &x) const {
      return Op == x.Op;
    }
    bool operator!=(const use_iterator &x) const {
      return !operator==(x);
    }

    /// atEnd - return true if this iterator is at the end of uses list.
    bool atEnd() const { return Op == 0; }

    // Iterator traversal: forward iteration only.
    use_iterator &operator++() {          // Preincrement
      assert(Op && "Cannot increment end iterator!");
      Op = Op->getNext();
      return *this;
    }

    use_iterator operator++(int) {        // Postincrement
      use_iterator tmp = *this; ++*this; return tmp;
    }

    /// Retrieve a pointer to the current user node.
    SDNode *operator*() const {
      assert(Op && "Cannot dereference end iterator!");
      return Op->getUser();
    }

    SDNode *operator->() const { return operator*(); }

    SDUse &getUse() const { return *Op; }

    /// getOperandNo - Retrieve the operand # of this use in its user.
    ///
    unsigned getOperandNo() const {
      assert(Op && "Cannot dereference end iterator!");
      return (unsigned)(Op - Op->getUser()->OperandList);
    }
  };

  /// use_begin/use_end - Provide iteration support to walk over all uses
  /// of an SDNode.

  use_iterator use_begin() const {
    return use_iterator(UseList);
  }

  static use_iterator use_end() { return use_iterator(0); }


  /// hasNUsesOfValue - Return true if there are exactly NUSES uses of the
  /// indicated value.  This method ignores uses of other values defined by this
  /// operation.
  bool hasNUsesOfValue(unsigned NUses, unsigned Value) const;

  /// hasAnyUseOfValue - Return true if there are any use of the indicated
  /// value. This method ignores uses of other values defined by this operation.
  bool hasAnyUseOfValue(unsigned Value) const;

  /// isOnlyUserOf - Return true if this node is the only use of N.
  ///
  bool isOnlyUserOf(SDNode *N) const;

  /// isOperandOf - Return true if this node is an operand of N.
  ///
  bool isOperandOf(SDNode *N) const;

  /// isPredecessorOf - Return true if this node is a predecessor of N.
  /// NOTE: Implemented on top of hasPredecessor and every bit as
  /// expensive. Use carefully.
  bool isPredecessorOf(const SDNode *N) const { return N->hasPredecessor(this); }

  /// hasPredecessor - Return true if N is a predecessor of this node.
  /// N is either an operand of this node, or can be reached by recursively
  /// traversing up the operands.
  /// NOTE: This is an expensive method. Use it carefully.
  bool hasPredecessor(const SDNode *N) const;

  /// hasPredecesorHelper - Return true if N is a predecessor of this node.
  /// N is either an operand of this node, or can be reached by recursively
  /// traversing up the operands.
  /// In this helper the Visited and worklist sets are held externally to
  /// cache predecessors over multiple invocations. If you want to test for
  /// multiple predecessors this method is preferable to multiple calls to
  /// hasPredecessor. Be sure to clear Visited and Worklist if the DAG
  /// changes.
  /// NOTE: This is still very expensive. Use carefully.
  bool hasPredecessorHelper(const SDNode *N,
                            SmallPtrSet<const SDNode *, 32> &Visited,
                            SmallVectorImpl<const SDNode *> &Worklist) const;

  /// getNumOperands - Return the number of values used by this operation.
  ///
  unsigned getNumOperands() const { return NumOperands; }

  /// getConstantOperandVal - Helper method returns the integer value of a
  /// ConstantSDNode operand.
  uint64_t getConstantOperandVal(unsigned Num) const;

  const SDValue &getOperand(unsigned Num) const {
    assert(Num < NumOperands && "Invalid child # of SDNode!");
    return OperandList[Num];
  }

  typedef SDUse* op_iterator;
  op_iterator op_begin() const { return OperandList; }
  op_iterator op_end() const { return OperandList+NumOperands; }

  SDVTList getVTList() const {
    SDVTList X = { ValueList, NumValues };
    return X;
  }

  /// getGluedNode - If this node has a glue operand, return the node
  /// to which the glue operand points. Otherwise return NULL.
  SDNode *getGluedNode() const {
    if (getNumOperands() != 0 &&
      getOperand(getNumOperands()-1).getValueType() == MVT::Glue)
      return getOperand(getNumOperands()-1).getNode();
    return 0;
  }

  // If this is a pseudo op, like copyfromreg, look to see if there is a
  // real target node glued to it.  If so, return the target node.
  const SDNode *getGluedMachineNode() const {
    const SDNode *FoundNode = this;

    // Climb up glue edges until a machine-opcode node is found, or the
    // end of the chain is reached.
    while (!FoundNode->isMachineOpcode()) {
      const SDNode *N = FoundNode->getGluedNode();
      if (!N) break;
      FoundNode = N;
    }

    return FoundNode;
  }

  /// getGluedUser - If this node has a glue value with a user, return
  /// the user (there is at most one). Otherwise return NULL.
  SDNode *getGluedUser() const {
    for (use_iterator UI = use_begin(), UE = use_end(); UI != UE; ++UI)
      if (UI.getUse().get().getValueType() == MVT::Glue)
        return *UI;
    return 0;
  }

  /// getNumValues - Return the number of values defined/returned by this
  /// operator.
  ///
  unsigned getNumValues() const { return NumValues; }

  /// getValueType - Return the type of a specified result.
  ///
  EVT getValueType(unsigned ResNo) const {
    assert(ResNo < NumValues && "Illegal result number!");
    return ValueList[ResNo];
  }

  /// Return the type of a specified result as a simple type.
  ///
  MVT getSimpleValueType(unsigned ResNo) const {
    return getValueType(ResNo).getSimpleVT();
  }

  /// getValueSizeInBits - Returns MVT::getSizeInBits(getValueType(ResNo)).
  ///
  unsigned getValueSizeInBits(unsigned ResNo) const {
    return getValueType(ResNo).getSizeInBits();
  }

  typedef const EVT* value_iterator;
  value_iterator value_begin() const { return ValueList; }
  value_iterator value_end() const { return ValueList+NumValues; }

  /// getOperationName - Return the opcode of this operation for printing.
  ///
  std::string getOperationName(const SelectionDAG *G = 0) const;
  static const char* getIndexedModeName(ISD::MemIndexedMode AM);
  void print_types(raw_ostream &OS, const SelectionDAG *G) const;
  void print_details(raw_ostream &OS, const SelectionDAG *G) const;
  void print(raw_ostream &OS, const SelectionDAG *G = 0) const;
  void printr(raw_ostream &OS, const SelectionDAG *G = 0) const;

  /// printrFull - Print a SelectionDAG node and all children down to
  /// the leaves.  The given SelectionDAG allows target-specific nodes
  /// to be printed in human-readable form.  Unlike printr, this will
  /// print the whole DAG, including children that appear multiple
  /// times.
  ///
  void printrFull(raw_ostream &O, const SelectionDAG *G = 0) const;

  /// printrWithDepth - Print a SelectionDAG node and children up to
  /// depth "depth."  The given SelectionDAG allows target-specific
  /// nodes to be printed in human-readable form.  Unlike printr, this
  /// will print children that appear multiple times wherever they are
  /// used.
  ///
  void printrWithDepth(raw_ostream &O, const SelectionDAG *G = 0,
                       unsigned depth = 100) const;


  /// dump - Dump this node, for debugging.
  void dump() const;

  /// dumpr - Dump (recursively) this node and its use-def subgraph.
  void dumpr() const;

  /// dump - Dump this node, for debugging.
  /// The given SelectionDAG allows target-specific nodes to be printed
  /// in human-readable form.
  void dump(const SelectionDAG *G) const;

  /// dumpr - Dump (recursively) this node and its use-def subgraph.
  /// The given SelectionDAG allows target-specific nodes to be printed
  /// in human-readable form.
  void dumpr(const SelectionDAG *G) const;

  /// dumprFull - printrFull to dbgs().  The given SelectionDAG allows
  /// target-specific nodes to be printed in human-readable form.
  /// Unlike dumpr, this will print the whole DAG, including children
  /// that appear multiple times.
  ///
  void dumprFull(const SelectionDAG *G = 0) const;

  /// dumprWithDepth - printrWithDepth to dbgs().  The given
  /// SelectionDAG allows target-specific nodes to be printed in
  /// human-readable form.  Unlike dumpr, this will print children
  /// that appear multiple times wherever they are used.
  ///
  void dumprWithDepth(const SelectionDAG *G = 0, unsigned depth = 100) const;

  /// Profile - Gather unique data for the node.
  ///
  void Profile(FoldingSetNodeID &ID) const;

  /// addUse - This method should only be used by the SDUse class.
  ///
  void addUse(SDUse &U) { U.addToList(&UseList); }

protected:
  static SDVTList getSDVTList(EVT VT) {
    SDVTList Ret = { getValueTypeList(VT), 1 };
    return Ret;
  }

  SDNode(unsigned Opc, unsigned Order, const DebugLoc dl, SDVTList VTs,
         const SDValue *Ops, unsigned NumOps)
    : NodeType(Opc), OperandsNeedDelete(true), HasDebugValue(false),
      SubclassData(0), NodeId(-1),
      OperandList(NumOps ? new SDUse[NumOps] : 0),
      ValueList(VTs.VTs), UseList(NULL),
      NumOperands(NumOps), NumValues(VTs.NumVTs),
      debugLoc(dl), IROrder(Order) {
    for (unsigned i = 0; i != NumOps; ++i) {
      OperandList[i].setUser(this);
      OperandList[i].setInitial(Ops[i]);
    }
    checkForCycles(this);
  }

  /// This constructor adds no operands itself; operands can be
  /// set later with InitOperands.
  SDNode(unsigned Opc, unsigned Order, const DebugLoc dl, SDVTList VTs)
    : NodeType(Opc), OperandsNeedDelete(false), HasDebugValue(false),
      SubclassData(0), NodeId(-1), OperandList(0),
      ValueList(VTs.VTs), UseList(NULL), NumOperands(0), NumValues(VTs.NumVTs),
      debugLoc(dl), IROrder(Order) {}

  /// InitOperands - Initialize the operands list of this with 1 operand.
  void InitOperands(SDUse *Ops, const SDValue &Op0) {
    Ops[0].setUser(this);
    Ops[0].setInitial(Op0);
    NumOperands = 1;
    OperandList = Ops;
    checkForCycles(this);
  }

  /// InitOperands - Initialize the operands list of this with 2 operands.
  void InitOperands(SDUse *Ops, const SDValue &Op0, const SDValue &Op1) {
    Ops[0].setUser(this);
    Ops[0].setInitial(Op0);
    Ops[1].setUser(this);
    Ops[1].setInitial(Op1);
    NumOperands = 2;
    OperandList = Ops;
    checkForCycles(this);
  }

  /// InitOperands - Initialize the operands list of this with 3 operands.
  void InitOperands(SDUse *Ops, const SDValue &Op0, const SDValue &Op1,
                    const SDValue &Op2) {
    Ops[0].setUser(this);
    Ops[0].setInitial(Op0);
    Ops[1].setUser(this);
    Ops[1].setInitial(Op1);
    Ops[2].setUser(this);
    Ops[2].setInitial(Op2);
    NumOperands = 3;
    OperandList = Ops;
    checkForCycles(this);
  }

  /// InitOperands - Initialize the operands list of this with 4 operands.
  void InitOperands(SDUse *Ops, const SDValue &Op0, const SDValue &Op1,
                    const SDValue &Op2, const SDValue &Op3) {
    Ops[0].setUser(this);
    Ops[0].setInitial(Op0);
    Ops[1].setUser(this);
    Ops[1].setInitial(Op1);
    Ops[2].setUser(this);
    Ops[2].setInitial(Op2);
    Ops[3].setUser(this);
    Ops[3].setInitial(Op3);
    NumOperands = 4;
    OperandList = Ops;
    checkForCycles(this);
  }

  /// InitOperands - Initialize the operands list of this with N operands.
  void InitOperands(SDUse *Ops, const SDValue *Vals, unsigned N) {
    for (unsigned i = 0; i != N; ++i) {
      Ops[i].setUser(this);
      Ops[i].setInitial(Vals[i]);
    }
    NumOperands = N;
    OperandList = Ops;
    checkForCycles(this);
  }

  /// DropOperands - Release the operands and set this node to have
  /// zero operands.
  void DropOperands();
};

/// Wrapper class for IR location info (IR ordering and DebugLoc) to be passed
/// into SDNode creation functions.
/// When an SDNode is created from the DAGBuilder, the DebugLoc is extracted
/// from the original Instruction, and IROrder is the ordinal position of
/// the instruction.
/// When an SDNode is created after the DAG is being built, both DebugLoc and
/// the IROrder are propagated from the original SDNode.
/// So SDLoc class provides two constructors besides the default one, one to
/// be used by the DAGBuilder, the other to be used by others.
class SDLoc {
private:
  // Ptr could be used for either Instruction* or SDNode*. It is used for
  // Instruction* if IROrder is not -1.
  const void *Ptr;
  int IROrder;

public:
  SDLoc() : Ptr(NULL), IROrder(0) {}
  SDLoc(const SDNode *N) : Ptr(N), IROrder(-1) {
    assert(N && "null SDNode");
  }
  SDLoc(const SDValue V) : Ptr(V.getNode()), IROrder(-1) {
    assert(Ptr && "null SDNode");
  }
  SDLoc(const Instruction *I, int Order) : Ptr(I), IROrder(Order) {
    assert(Order >= 0 && "bad IROrder");
  }
  unsigned getIROrder() {
    if (IROrder >= 0 || Ptr == NULL) {
      return (unsigned)IROrder;
    }
    const SDNode *N = (const SDNode*)(Ptr);
    return N->getIROrder();
  }
  DebugLoc getDebugLoc() {
    if (Ptr == NULL) {
      return DebugLoc();
    }
    if (IROrder >= 0) {
      const Instruction *I = (const Instruction*)(Ptr);
      return I->getDebugLoc();
    }
    const SDNode *N = (const SDNode*)(Ptr);
    return N->getDebugLoc();
  }
};


// Define inline functions from the SDValue class.

inline unsigned SDValue::getOpcode() const {
  return Node->getOpcode();
}
inline EVT SDValue::getValueType() const {
  return Node->getValueType(ResNo);
}
inline unsigned SDValue::getNumOperands() const {
  return Node->getNumOperands();
}
inline const SDValue &SDValue::getOperand(unsigned i) const {
  return Node->getOperand(i);
}
inline uint64_t SDValue::getConstantOperandVal(unsigned i) const {
  return Node->getConstantOperandVal(i);
}
inline bool SDValue::isTargetOpcode() const {
  return Node->isTargetOpcode();
}
inline bool SDValue::isTargetMemoryOpcode() const {
  return Node->isTargetMemoryOpcode();
}
inline bool SDValue::isMachineOpcode() const {
  return Node->isMachineOpcode();
}
inline unsigned SDValue::getMachineOpcode() const {
  return Node->getMachineOpcode();
}
inline bool SDValue::use_empty() const {
  return !Node->hasAnyUseOfValue(ResNo);
}
inline bool SDValue::hasOneUse() const {
  return Node->hasNUsesOfValue(1, ResNo);
}
inline const DebugLoc SDValue::getDebugLoc() const {
  return Node->getDebugLoc();
}
inline void SDValue::dump() const {
  return Node->dump();
}
inline void SDValue::dumpr() const {
  return Node->dumpr();
}
// Define inline functions from the SDUse class.

inline void SDUse::set(const SDValue &V) {
  if (Val.getNode()) removeFromList();
  Val = V;
  if (V.getNode()) V.getNode()->addUse(*this);
}

inline void SDUse::setInitial(const SDValue &V) {
  Val = V;
  V.getNode()->addUse(*this);
}

inline void SDUse::setNode(SDNode *N) {
  if (Val.getNode()) removeFromList();
  Val.setNode(N);
  if (N) N->addUse(*this);
}

/// UnarySDNode - This class is used for single-operand SDNodes.  This is solely
/// to allow co-allocation of node operands with the node itself.
class UnarySDNode : public SDNode {
  SDUse Op;
public:
  UnarySDNode(unsigned Opc, unsigned Order, DebugLoc dl, SDVTList VTs, SDValue X)
    : SDNode(Opc, Order, dl, VTs) {
    InitOperands(&Op, X);
  }
};

/// BinarySDNode - This class is used for two-operand SDNodes.  This is solely
/// to allow co-allocation of node operands with the node itself.
class BinarySDNode : public SDNode {
  SDUse Ops[2];
public:
  BinarySDNode(unsigned Opc, unsigned Order, DebugLoc dl, SDVTList VTs, SDValue X, SDValue Y)
    : SDNode(Opc, Order, dl, VTs) {
    InitOperands(Ops, X, Y);
  }
};

/// TernarySDNode - This class is used for three-operand SDNodes. This is solely
/// to allow co-allocation of node operands with the node itself.
class TernarySDNode : public SDNode {
  SDUse Ops[3];
public:
  TernarySDNode(unsigned Opc, unsigned Order, DebugLoc dl, SDVTList VTs,
                SDValue X, SDValue Y, SDValue Z)
    : SDNode(Opc, Order, dl, VTs) {
    InitOperands(Ops, X, Y, Z);
  }
};


/// HandleSDNode - This class is used to form a handle around another node that
/// is persistent and is updated across invocations of replaceAllUsesWith on its
/// operand.  This node should be directly created by end-users and not added to
/// the AllNodes list.
class HandleSDNode : public SDNode {
  SDUse Op;
public:
  // FIXME: Remove the "noinline" attribute once <rdar://problem/5852746> is
  // fixed.
#if __GNUC__==4 && __GNUC_MINOR__==2 && defined(__APPLE__) && !defined(__llvm__)
  explicit __attribute__((__noinline__)) HandleSDNode(SDValue X)
#else
  explicit HandleSDNode(SDValue X)
#endif
    : SDNode(ISD::HANDLENODE, 0, DebugLoc(), getSDVTList(MVT::Other)) {
    InitOperands(&Op, X);
  }
  ~HandleSDNode();
  const SDValue &getValue() const { return Op; }
};

/// Abstact virtual class for operations for memory operations
class MemSDNode : public SDNode {
private:
  // MemoryVT - VT of in-memory value.
  EVT MemoryVT;

protected:
  /// MMO - Memory reference information.
  MachineMemOperand *MMO;

public:
  MemSDNode(unsigned Opc, unsigned Order, DebugLoc dl, SDVTList VTs,
            EVT MemoryVT, MachineMemOperand *MMO);

  MemSDNode(unsigned Opc, unsigned Order, DebugLoc dl, SDVTList VTs, const SDValue *Ops,
            unsigned NumOps, EVT MemoryVT, MachineMemOperand *MMO);

  bool readMem() const { return MMO->isLoad(); }
  bool writeMem() const { return MMO->isStore(); }

  /// Returns alignment and volatility of the memory access
  unsigned getOriginalAlignment() const { 
    return MMO->getBaseAlignment();
  }
  unsigned getAlignment() const {
    return MMO->getAlignment();
  }

  /// getRawSubclassData - Return the SubclassData value, which contains an
  /// encoding of the volatile flag, as well as bits used by subclasses. This
  /// function should only be used to compute a FoldingSetNodeID value.
  unsigned getRawSubclassData() const {
    return SubclassData;
  }

  // We access subclass data here so that we can check consistency
  // with MachineMemOperand information.
  bool isVolatile() const { return (SubclassData >> 5) & 1; }
  bool isNonTemporal() const { return (SubclassData >> 6) & 1; }
  bool isInvariant() const { return (SubclassData >> 7) & 1; }

  AtomicOrdering getOrdering() const {
    return AtomicOrdering((SubclassData >> 8) & 15);
  }
  SynchronizationScope getSynchScope() const {
    return SynchronizationScope((SubclassData >> 12) & 1);
  }

  /// Returns the SrcValue and offset that describes the location of the access
  const Value *getSrcValue() const { return MMO->getValue(); }
  int64_t getSrcValueOffset() const { return MMO->getOffset(); }

  /// Returns the TBAAInfo that describes the dereference.
  const MDNode *getTBAAInfo() const { return MMO->getTBAAInfo(); }

  /// Returns the Ranges that describes the dereference.
  const MDNode *getRanges() const { return MMO->getRanges(); }

  /// getMemoryVT - Return the type of the in-memory value.
  EVT getMemoryVT() const { return MemoryVT; }

  /// getMemOperand - Return a MachineMemOperand object describing the memory
  /// reference performed by operation.
  MachineMemOperand *getMemOperand() const { return MMO; }

  const MachinePointerInfo &getPointerInfo() const {
    return MMO->getPointerInfo();
  }

  /// getAddressSpace - Return the address space for the associated pointer
  unsigned getAddressSpace() const {
    return getPointerInfo().getAddrSpace();
  }

  /// refineAlignment - Update this MemSDNode's MachineMemOperand information
  /// to reflect the alignment of NewMMO, if it has a greater alignment.
  /// This must only be used when the new alignment applies to all users of
  /// this MachineMemOperand.
  void refineAlignment(const MachineMemOperand *NewMMO) {
    MMO->refineAlignment(NewMMO);
  }

  const SDValue &getChain() const { return getOperand(0); }
  const SDValue &getBasePtr() const {
    return getOperand(getOpcode() == ISD::STORE ? 2 : 1);
  }

  // Methods to support isa and dyn_cast
  static bool classof(const SDNode *N) {
    // For some targets, we lower some target intrinsics to a MemIntrinsicNode
    // with either an intrinsic or a target opcode.
    return N->getOpcode() == ISD::LOAD                ||
           N->getOpcode() == ISD::STORE               ||
           N->getOpcode() == ISD::PREFETCH            ||
           N->getOpcode() == ISD::ATOMIC_CMP_SWAP     ||
           N->getOpcode() == ISD::ATOMIC_SWAP         ||
           N->getOpcode() == ISD::ATOMIC_LOAD_ADD     ||
           N->getOpcode() == ISD::ATOMIC_LOAD_SUB     ||
           N->getOpcode() == ISD::ATOMIC_LOAD_AND     ||
           N->getOpcode() == ISD::ATOMIC_LOAD_OR      ||
           N->getOpcode() == ISD::ATOMIC_LOAD_XOR     ||
           N->getOpcode() == ISD::ATOMIC_LOAD_NAND    ||
           N->getOpcode() == ISD::ATOMIC_LOAD_MIN     ||
           N->getOpcode() == ISD::ATOMIC_LOAD_MAX     ||
           N->getOpcode() == ISD::ATOMIC_LOAD_UMIN    ||
           N->getOpcode() == ISD::ATOMIC_LOAD_UMAX    ||
           N->getOpcode() == ISD::ATOMIC_LOAD         ||
           N->getOpcode() == ISD::ATOMIC_STORE        ||
           N->isTargetMemoryOpcode();
  }
};

/// AtomicSDNode - A SDNode reprenting atomic operations.
///
class AtomicSDNode : public MemSDNode {
  SDUse Ops[4];

  void InitAtomic(AtomicOrdering Ordering, SynchronizationScope SynchScope) {
    // This must match encodeMemSDNodeFlags() in SelectionDAG.cpp.
    assert((Ordering & 15) == Ordering &&
           "Ordering may not require more than 4 bits!");
    assert((SynchScope & 1) == SynchScope &&
           "SynchScope may not require more than 1 bit!");
    SubclassData |= Ordering << 8;
    SubclassData |= SynchScope << 12;
    assert(getOrdering() == Ordering && "Ordering encoding error!");
    assert(getSynchScope() == SynchScope && "Synch-scope encoding error!");
  }

public:
  // Opc:   opcode for atomic
  // VTL:    value type list
  // Chain:  memory chain for operaand
  // Ptr:    address to update as a SDValue
  // Cmp:    compare value
  // Swp:    swap value
  // SrcVal: address to update as a Value (used for MemOperand)
  // Align:  alignment of memory
  AtomicSDNode(unsigned Opc, unsigned Order, DebugLoc dl, SDVTList VTL, EVT MemVT,
               SDValue Chain, SDValue Ptr,
               SDValue Cmp, SDValue Swp, MachineMemOperand *MMO,
               AtomicOrdering Ordering, SynchronizationScope SynchScope)
    : MemSDNode(Opc, Order, dl, VTL, MemVT, MMO) {
    InitAtomic(Ordering, SynchScope);
    InitOperands(Ops, Chain, Ptr, Cmp, Swp);
  }
  AtomicSDNode(unsigned Opc, unsigned Order, DebugLoc dl, SDVTList VTL, EVT MemVT,
               SDValue Chain, SDValue Ptr,
               SDValue Val, MachineMemOperand *MMO,
               AtomicOrdering Ordering, SynchronizationScope SynchScope)
    : MemSDNode(Opc, Order, dl, VTL, MemVT, MMO) {
    InitAtomic(Ordering, SynchScope);
    InitOperands(Ops, Chain, Ptr, Val);
  }
  AtomicSDNode(unsigned Opc, unsigned Order, DebugLoc dl, SDVTList VTL, EVT MemVT,
               SDValue Chain, SDValue Ptr,
               MachineMemOperand *MMO,
               AtomicOrdering Ordering, SynchronizationScope SynchScope)
    : MemSDNode(Opc, Order, dl, VTL, MemVT, MMO) {
    InitAtomic(Ordering, SynchScope);
    InitOperands(Ops, Chain, Ptr);
  }

  const SDValue &getBasePtr() const { return getOperand(1); }
  const SDValue &getVal() const { return getOperand(2); }

  bool isCompareAndSwap() const {
    unsigned Op = getOpcode();
    return Op == ISD::ATOMIC_CMP_SWAP;
  }

  // Methods to support isa and dyn_cast
  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::ATOMIC_CMP_SWAP     ||
           N->getOpcode() == ISD::ATOMIC_SWAP         ||
           N->getOpcode() == ISD::ATOMIC_LOAD_ADD     ||
           N->getOpcode() == ISD::ATOMIC_LOAD_SUB     ||
           N->getOpcode() == ISD::ATOMIC_LOAD_AND     ||
           N->getOpcode() == ISD::ATOMIC_LOAD_OR      ||
           N->getOpcode() == ISD::ATOMIC_LOAD_XOR     ||
           N->getOpcode() == ISD::ATOMIC_LOAD_NAND    ||
           N->getOpcode() == ISD::ATOMIC_LOAD_MIN     ||
           N->getOpcode() == ISD::ATOMIC_LOAD_MAX     ||
           N->getOpcode() == ISD::ATOMIC_LOAD_UMIN    ||
           N->getOpcode() == ISD::ATOMIC_LOAD_UMAX    ||
           N->getOpcode() == ISD::ATOMIC_LOAD         ||
           N->getOpcode() == ISD::ATOMIC_STORE;
  }
};

/// MemIntrinsicSDNode - This SDNode is used for target intrinsics that touch
/// memory and need an associated MachineMemOperand. Its opcode may be
/// INTRINSIC_VOID, INTRINSIC_W_CHAIN, PREFETCH, or a target-specific opcode
/// with a value not less than FIRST_TARGET_MEMORY_OPCODE.
class MemIntrinsicSDNode : public MemSDNode {
public:
  MemIntrinsicSDNode(unsigned Opc, unsigned Order, DebugLoc dl, SDVTList VTs,
                     const SDValue *Ops, unsigned NumOps,
                     EVT MemoryVT, MachineMemOperand *MMO)
    : MemSDNode(Opc, Order, dl, VTs, Ops, NumOps, MemoryVT, MMO) {
  }

  // Methods to support isa and dyn_cast
  static bool classof(const SDNode *N) {
    // We lower some target intrinsics to their target opcode
    // early a node with a target opcode can be of this class
    return N->getOpcode() == ISD::INTRINSIC_W_CHAIN ||
           N->getOpcode() == ISD::INTRINSIC_VOID ||
           N->getOpcode() == ISD::PREFETCH ||
           N->isTargetMemoryOpcode();
  }
};

/// ShuffleVectorSDNode - This SDNode is used to implement the code generator
/// support for the llvm IR shufflevector instruction.  It combines elements
/// from two input vectors into a new input vector, with the selection and
/// ordering of elements determined by an array of integers, referred to as
/// the shuffle mask.  For input vectors of width N, mask indices of 0..N-1
/// refer to elements from the LHS input, and indices from N to 2N-1 the RHS.
/// An index of -1 is treated as undef, such that the code generator may put
/// any value in the corresponding element of the result.
class ShuffleVectorSDNode : public SDNode {
  SDUse Ops[2];

  // The memory for Mask is owned by the SelectionDAG's OperandAllocator, and
  // is freed when the SelectionDAG object is destroyed.
  const int *Mask;
protected:
  friend class SelectionDAG;
  ShuffleVectorSDNode(EVT VT, unsigned Order, DebugLoc dl, SDValue N1, SDValue N2, const int *M)
    : SDNode(ISD::VECTOR_SHUFFLE, Order, dl, getSDVTList(VT)), Mask(M) {
    InitOperands(Ops, N1, N2);
  }
public:

  ArrayRef<int> getMask() const {
    EVT VT = getValueType(0);
    return makeArrayRef(Mask, VT.getVectorNumElements());
  }
  int getMaskElt(unsigned Idx) const {
    assert(Idx < getValueType(0).getVectorNumElements() && "Idx out of range!");
    return Mask[Idx];
  }
  
  bool isSplat() const { return isSplatMask(Mask, getValueType(0)); }
  int  getSplatIndex() const { 
    assert(isSplat() && "Cannot get splat index for non-splat!");
    EVT VT = getValueType(0);
    for (unsigned i = 0, e = VT.getVectorNumElements(); i != e; ++i) {
      if (Mask[i] != -1)
        return Mask[i];
    }
    return -1;
  }
  static bool isSplatMask(const int *Mask, EVT VT);

  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::VECTOR_SHUFFLE;
  }
};
  
class ConstantSDNode : public SDNode {
  const ConstantInt *Value;
  friend class SelectionDAG;
  ConstantSDNode(bool isTarget, const ConstantInt *val, EVT VT)
    : SDNode(isTarget ? ISD::TargetConstant : ISD::Constant,
             0, DebugLoc(), getSDVTList(VT)), Value(val) {
  }
public:

  const ConstantInt *getConstantIntValue() const { return Value; }
  const APInt &getAPIntValue() const { return Value->getValue(); }
  uint64_t getZExtValue() const { return Value->getZExtValue(); }
  int64_t getSExtValue() const { return Value->getSExtValue(); }

  bool isOne() const { return Value->isOne(); }
  bool isNullValue() const { return Value->isNullValue(); }
  bool isAllOnesValue() const { return Value->isAllOnesValue(); }

  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::Constant ||
           N->getOpcode() == ISD::TargetConstant;
  }
};

class ConstantFPSDNode : public SDNode {
  const ConstantFP *Value;
  friend class SelectionDAG;
  ConstantFPSDNode(bool isTarget, const ConstantFP *val, EVT VT)
    : SDNode(isTarget ? ISD::TargetConstantFP : ISD::ConstantFP,
             0, DebugLoc(), getSDVTList(VT)), Value(val) {
  }
public:

  const APFloat& getValueAPF() const { return Value->getValueAPF(); }
  const ConstantFP *getConstantFPValue() const { return Value; }

  /// isZero - Return true if the value is positive or negative zero.
  bool isZero() const { return Value->isZero(); }

  /// isNaN - Return true if the value is a NaN.
  bool isNaN() const { return Value->isNaN(); }

  /// isExactlyValue - We don't rely on operator== working on double values, as
  /// it returns true for things that are clearly not equal, like -0.0 and 0.0.
  /// As such, this method can be used to do an exact bit-for-bit comparison of
  /// two floating point values.

  /// We leave the version with the double argument here because it's just so
  /// convenient to write "2.0" and the like.  Without this function we'd
  /// have to duplicate its logic everywhere it's called.
  bool isExactlyValue(double V) const {
    bool ignored;
    APFloat Tmp(V);
    Tmp.convert(Value->getValueAPF().getSemantics(),
                APFloat::rmNearestTiesToEven, &ignored);
    return isExactlyValue(Tmp);
  }
  bool isExactlyValue(const APFloat& V) const;

  static bool isValueValidForType(EVT VT, const APFloat& Val);

  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::ConstantFP ||
           N->getOpcode() == ISD::TargetConstantFP;
  }
};

class GlobalAddressSDNode : public SDNode {
  const GlobalValue *TheGlobal;
  int64_t Offset;
  unsigned char TargetFlags;
  friend class SelectionDAG;
  GlobalAddressSDNode(unsigned Opc, unsigned Order, DebugLoc DL, const GlobalValue *GA, EVT VT,
                      int64_t o, unsigned char TargetFlags);
public:

  const GlobalValue *getGlobal() const { return TheGlobal; }
  int64_t getOffset() const { return Offset; }
  unsigned char getTargetFlags() const { return TargetFlags; }
  // Return the address space this GlobalAddress belongs to.
  unsigned getAddressSpace() const;

  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::GlobalAddress ||
           N->getOpcode() == ISD::TargetGlobalAddress ||
           N->getOpcode() == ISD::GlobalTLSAddress ||
           N->getOpcode() == ISD::TargetGlobalTLSAddress;
  }
};

class FrameIndexSDNode : public SDNode {
  int FI;
  friend class SelectionDAG;
  FrameIndexSDNode(int fi, EVT VT, bool isTarg)
    : SDNode(isTarg ? ISD::TargetFrameIndex : ISD::FrameIndex,
      0, DebugLoc(), getSDVTList(VT)), FI(fi) {
  }
public:

  int getIndex() const { return FI; }

  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::FrameIndex ||
           N->getOpcode() == ISD::TargetFrameIndex;
  }
};

class JumpTableSDNode : public SDNode {
  int JTI;
  unsigned char TargetFlags;
  friend class SelectionDAG;
  JumpTableSDNode(int jti, EVT VT, bool isTarg, unsigned char TF)
    : SDNode(isTarg ? ISD::TargetJumpTable : ISD::JumpTable,
      0, DebugLoc(), getSDVTList(VT)), JTI(jti), TargetFlags(TF) {
  }
public:

  int getIndex() const { return JTI; }
  unsigned char getTargetFlags() const { return TargetFlags; }

  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::JumpTable ||
           N->getOpcode() == ISD::TargetJumpTable;
  }
};

class ConstantPoolSDNode : public SDNode {
  union {
    const Constant *ConstVal;
    MachineConstantPoolValue *MachineCPVal;
  } Val;
  int Offset;  // It's a MachineConstantPoolValue if top bit is set.
  unsigned Alignment;  // Minimum alignment requirement of CP (not log2 value).
  unsigned char TargetFlags;
  friend class SelectionDAG;
  ConstantPoolSDNode(bool isTarget, const Constant *c, EVT VT, int o,
                     unsigned Align, unsigned char TF)
    : SDNode(isTarget ? ISD::TargetConstantPool : ISD::ConstantPool, 0, DebugLoc(),
             getSDVTList(VT)), Offset(o), Alignment(Align), TargetFlags(TF) {
    assert(Offset >= 0 && "Offset is too large");
    Val.ConstVal = c;
  }
  ConstantPoolSDNode(bool isTarget, MachineConstantPoolValue *v,
                     EVT VT, int o, unsigned Align, unsigned char TF)
    : SDNode(isTarget ? ISD::TargetConstantPool : ISD::ConstantPool, 0, DebugLoc(),
             getSDVTList(VT)), Offset(o), Alignment(Align), TargetFlags(TF) {
    assert(Offset >= 0 && "Offset is too large");
    Val.MachineCPVal = v;
    Offset |= 1 << (sizeof(unsigned)*CHAR_BIT-1);
  }
public:
  

  bool isMachineConstantPoolEntry() const {
    return Offset < 0;
  }

  const Constant *getConstVal() const {
    assert(!isMachineConstantPoolEntry() && "Wrong constantpool type");
    return Val.ConstVal;
  }

  MachineConstantPoolValue *getMachineCPVal() const {
    assert(isMachineConstantPoolEntry() && "Wrong constantpool type");
    return Val.MachineCPVal;
  }

  int getOffset() const {
    return Offset & ~(1 << (sizeof(unsigned)*CHAR_BIT-1));
  }

  // Return the alignment of this constant pool object, which is either 0 (for
  // default alignment) or the desired value.
  unsigned getAlignment() const { return Alignment; }
  unsigned char getTargetFlags() const { return TargetFlags; }

  Type *getType() const;

  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::ConstantPool ||
           N->getOpcode() == ISD::TargetConstantPool;
  }
};

/// Completely target-dependent object reference.
class TargetIndexSDNode : public SDNode {
  unsigned char TargetFlags;
  int Index;
  int64_t Offset;
  friend class SelectionDAG;
public:

  TargetIndexSDNode(int Idx, EVT VT, int64_t Ofs, unsigned char TF)
    : SDNode(ISD::TargetIndex, 0, DebugLoc(), getSDVTList(VT)),
      TargetFlags(TF), Index(Idx), Offset(Ofs) {}
public:

  unsigned char getTargetFlags() const { return TargetFlags; }
  int getIndex() const { return Index; }
  int64_t getOffset() const { return Offset; }

  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::TargetIndex;
  }
};

class BasicBlockSDNode : public SDNode {
  MachineBasicBlock *MBB;
  friend class SelectionDAG;
  /// Debug info is meaningful and potentially useful here, but we create
  /// blocks out of order when they're jumped to, which makes it a bit
  /// harder.  Let's see if we need it first.
  explicit BasicBlockSDNode(MachineBasicBlock *mbb)
    : SDNode(ISD::BasicBlock, 0, DebugLoc(), getSDVTList(MVT::Other)), MBB(mbb) {
  }
public:

  MachineBasicBlock *getBasicBlock() const { return MBB; }

  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::BasicBlock;
  }
};

/// BuildVectorSDNode - A "pseudo-class" with methods for operating on
/// BUILD_VECTORs.
class BuildVectorSDNode : public SDNode {
  // These are constructed as SDNodes and then cast to BuildVectorSDNodes.
  explicit BuildVectorSDNode() LLVM_DELETED_FUNCTION;
public:
  /// isConstantSplat - Check if this is a constant splat, and if so, find the
  /// smallest element size that splats the vector.  If MinSplatBits is
  /// nonzero, the element size must be at least that large.  Note that the
  /// splat element may be the entire vector (i.e., a one element vector).
  /// Returns the splat element value in SplatValue.  Any undefined bits in
  /// that value are zero, and the corresponding bits in the SplatUndef mask
  /// are set.  The SplatBitSize value is set to the splat element size in
  /// bits.  HasAnyUndefs is set to true if any bits in the vector are
  /// undefined.  isBigEndian describes the endianness of the target.
  bool isConstantSplat(APInt &SplatValue, APInt &SplatUndef,
                       unsigned &SplatBitSize, bool &HasAnyUndefs,
                       unsigned MinSplatBits = 0, bool isBigEndian = false);

  static inline bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::BUILD_VECTOR;
  }
};

/// SrcValueSDNode - An SDNode that holds an arbitrary LLVM IR Value. This is
/// used when the SelectionDAG needs to make a simple reference to something
/// in the LLVM IR representation.
///
class SrcValueSDNode : public SDNode {
  const Value *V;
  friend class SelectionDAG;
  /// Create a SrcValue for a general value.
  explicit SrcValueSDNode(const Value *v)
    : SDNode(ISD::SRCVALUE, 0, DebugLoc(), getSDVTList(MVT::Other)), V(v) {}

public:
  /// getValue - return the contained Value.
  const Value *getValue() const { return V; }

  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::SRCVALUE;
  }
};
  
class MDNodeSDNode : public SDNode {
  const MDNode *MD;
  friend class SelectionDAG;
  explicit MDNodeSDNode(const MDNode *md)
  : SDNode(ISD::MDNODE_SDNODE, 0, DebugLoc(), getSDVTList(MVT::Other)), MD(md) {}
public:
  
  const MDNode *getMD() const { return MD; }
  
  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::MDNODE_SDNODE;
  }
};


class RegisterSDNode : public SDNode {
  unsigned Reg;
  friend class SelectionDAG;
  RegisterSDNode(unsigned reg, EVT VT)
    : SDNode(ISD::Register, 0, DebugLoc(), getSDVTList(VT)), Reg(reg) {
  }
public:

  unsigned getReg() const { return Reg; }

  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::Register;
  }
};

class RegisterMaskSDNode : public SDNode {
  // The memory for RegMask is not owned by the node.
  const uint32_t *RegMask;
  friend class SelectionDAG;
  RegisterMaskSDNode(const uint32_t *mask)
    : SDNode(ISD::RegisterMask, 0, DebugLoc(), getSDVTList(MVT::Untyped)),
      RegMask(mask) {}
public:

  const uint32_t *getRegMask() const { return RegMask; }

  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::RegisterMask;
  }
};

class BlockAddressSDNode : public SDNode {
  const BlockAddress *BA;
  int64_t Offset;
  unsigned char TargetFlags;
  friend class SelectionDAG;
  BlockAddressSDNode(unsigned NodeTy, EVT VT, const BlockAddress *ba,
                     int64_t o, unsigned char Flags)
    : SDNode(NodeTy, 0, DebugLoc(), getSDVTList(VT)),
             BA(ba), Offset(o), TargetFlags(Flags) {
  }
public:
  const BlockAddress *getBlockAddress() const { return BA; }
  int64_t getOffset() const { return Offset; }
  unsigned char getTargetFlags() const { return TargetFlags; }

  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::BlockAddress ||
           N->getOpcode() == ISD::TargetBlockAddress;
  }
};

class EHLabelSDNode : public SDNode {
  SDUse Chain;
  MCSymbol *Label;
  friend class SelectionDAG;
  EHLabelSDNode(unsigned Order, DebugLoc dl, SDValue ch, MCSymbol *L)
    : SDNode(ISD::EH_LABEL, Order, dl, getSDVTList(MVT::Other)), Label(L) {
    InitOperands(&Chain, ch);
  }
public:
  MCSymbol *getLabel() const { return Label; }

  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::EH_LABEL;
  }
};

class ExternalSymbolSDNode : public SDNode {
  const char *Symbol;
  unsigned char TargetFlags;
  
  friend class SelectionDAG;
  ExternalSymbolSDNode(bool isTarget, const char *Sym, unsigned char TF, EVT VT)
    : SDNode(isTarget ? ISD::TargetExternalSymbol : ISD::ExternalSymbol,
             0, DebugLoc(), getSDVTList(VT)), Symbol(Sym), TargetFlags(TF) {
  }
public:

  const char *getSymbol() const { return Symbol; }
  unsigned char getTargetFlags() const { return TargetFlags; }

  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::ExternalSymbol ||
           N->getOpcode() == ISD::TargetExternalSymbol;
  }
};

class CondCodeSDNode : public SDNode {
  ISD::CondCode Condition;
  friend class SelectionDAG;
  explicit CondCodeSDNode(ISD::CondCode Cond)
    : SDNode(ISD::CONDCODE, 0, DebugLoc(), getSDVTList(MVT::Other)),
      Condition(Cond) {
  }
public:

  ISD::CondCode get() const { return Condition; }

  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::CONDCODE;
  }
};
  
/// CvtRndSatSDNode - NOTE: avoid using this node as this may disappear in the
/// future and most targets don't support it.
class CvtRndSatSDNode : public SDNode {
  ISD::CvtCode CvtCode;
  friend class SelectionDAG;
  explicit CvtRndSatSDNode(EVT VT, unsigned Order, DebugLoc dl, const SDValue *Ops,
                           unsigned NumOps, ISD::CvtCode Code)
    : SDNode(ISD::CONVERT_RNDSAT, Order, dl, getSDVTList(VT), Ops, NumOps),
      CvtCode(Code) {
    assert(NumOps == 5 && "wrong number of operations");
  }
public:
  ISD::CvtCode getCvtCode() const { return CvtCode; }

  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::CONVERT_RNDSAT;
  }
};

/// VTSDNode - This class is used to represent EVT's, which are used
/// to parameterize some operations.
class VTSDNode : public SDNode {
  EVT ValueType;
  friend class SelectionDAG;
  explicit VTSDNode(EVT VT)
    : SDNode(ISD::VALUETYPE, 0, DebugLoc(), getSDVTList(MVT::Other)),
      ValueType(VT) {
  }
public:

  EVT getVT() const { return ValueType; }

  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::VALUETYPE;
  }
};

/// LSBaseSDNode - Base class for LoadSDNode and StoreSDNode
///
class LSBaseSDNode : public MemSDNode {
  //! Operand array for load and store
  /*!
    \note Moving this array to the base class captures more
    common functionality shared between LoadSDNode and
    StoreSDNode
   */
  SDUse Ops[4];
public:
  LSBaseSDNode(ISD::NodeType NodeTy, unsigned Order, DebugLoc dl, SDValue *Operands,
               unsigned numOperands, SDVTList VTs, ISD::MemIndexedMode AM,
               EVT MemVT, MachineMemOperand *MMO)
    : MemSDNode(NodeTy, Order, dl, VTs, MemVT, MMO) {
    SubclassData |= AM << 2;
    assert(getAddressingMode() == AM && "MemIndexedMode encoding error!");
    InitOperands(Ops, Operands, numOperands);
    assert((getOffset().getOpcode() == ISD::UNDEF || isIndexed()) &&
           "Only indexed loads and stores have a non-undef offset operand");
  }

  const SDValue &getOffset() const {
    return getOperand(getOpcode() == ISD::LOAD ? 2 : 3);
  }

  /// getAddressingMode - Return the addressing mode for this load or store:
  /// unindexed, pre-inc, pre-dec, post-inc, or post-dec.
  ISD::MemIndexedMode getAddressingMode() const {
    return ISD::MemIndexedMode((SubclassData >> 2) & 7);
  }

  /// isIndexed - Return true if this is a pre/post inc/dec load/store.
  bool isIndexed() const { return getAddressingMode() != ISD::UNINDEXED; }

  /// isUnindexed - Return true if this is NOT a pre/post inc/dec load/store.
  bool isUnindexed() const { return getAddressingMode() == ISD::UNINDEXED; }

  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::LOAD ||
           N->getOpcode() == ISD::STORE;
  }
};

/// LoadSDNode - This class is used to represent ISD::LOAD nodes.
///
class LoadSDNode : public LSBaseSDNode {
  friend class SelectionDAG;
  LoadSDNode(SDValue *ChainPtrOff, unsigned Order, DebugLoc dl, SDVTList VTs,
             ISD::MemIndexedMode AM, ISD::LoadExtType ETy, EVT MemVT,
             MachineMemOperand *MMO)
    : LSBaseSDNode(ISD::LOAD, Order, dl, ChainPtrOff, 3, VTs, AM, MemVT, MMO) {
    SubclassData |= (unsigned short)ETy;
    assert(getExtensionType() == ETy && "LoadExtType encoding error!");
    assert(readMem() && "Load MachineMemOperand is not a load!");
    assert(!writeMem() && "Load MachineMemOperand is a store!");
  }
public:

  /// getExtensionType - Return whether this is a plain node,
  /// or one of the varieties of value-extending loads.
  ISD::LoadExtType getExtensionType() const {
    return ISD::LoadExtType(SubclassData & 3);
  }

  const SDValue &getBasePtr() const { return getOperand(1); }
  const SDValue &getOffset() const { return getOperand(2); }

  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::LOAD;
  }
};

/// StoreSDNode - This class is used to represent ISD::STORE nodes.
///
class StoreSDNode : public LSBaseSDNode {
  friend class SelectionDAG;
  StoreSDNode(SDValue *ChainValuePtrOff, unsigned Order, DebugLoc dl,
              SDVTList VTs, ISD::MemIndexedMode AM, bool isTrunc, EVT MemVT,
              MachineMemOperand *MMO)
    : LSBaseSDNode(ISD::STORE, Order, dl, ChainValuePtrOff, 4,
                   VTs, AM, MemVT, MMO) {
    SubclassData |= (unsigned short)isTrunc;
    assert(isTruncatingStore() == isTrunc && "isTrunc encoding error!");
    assert(!readMem() && "Store MachineMemOperand is a load!");
    assert(writeMem() && "Store MachineMemOperand is not a store!");
  }
public:

  /// isTruncatingStore - Return true if the op does a truncation before store.
  /// For integers this is the same as doing a TRUNCATE and storing the result.
  /// For floats, it is the same as doing an FP_ROUND and storing the result.
  bool isTruncatingStore() const { return SubclassData & 1; }

  const SDValue &getValue() const { return getOperand(1); }
  const SDValue &getBasePtr() const { return getOperand(2); }
  const SDValue &getOffset() const { return getOperand(3); }

  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::STORE;
  }
};

/// MachineSDNode - An SDNode that represents everything that will be needed
/// to construct a MachineInstr. These nodes are created during the
/// instruction selection proper phase.
///
class MachineSDNode : public SDNode {
public:
  typedef MachineMemOperand **mmo_iterator;

private:
  friend class SelectionDAG;
  MachineSDNode(unsigned Opc, unsigned Order, const DebugLoc DL, SDVTList VTs)
    : SDNode(Opc, Order, DL, VTs), MemRefs(0), MemRefsEnd(0) {}

  /// LocalOperands - Operands for this instruction, if they fit here. If
  /// they don't, this field is unused.
  SDUse LocalOperands[4];

  /// MemRefs - Memory reference descriptions for this instruction.
  mmo_iterator MemRefs;
  mmo_iterator MemRefsEnd;

public:
  mmo_iterator memoperands_begin() const { return MemRefs; }
  mmo_iterator memoperands_end() const { return MemRefsEnd; }
  bool memoperands_empty() const { return MemRefsEnd == MemRefs; }

  /// setMemRefs - Assign this MachineSDNodes's memory reference descriptor
  /// list. This does not transfer ownership.
  void setMemRefs(mmo_iterator NewMemRefs, mmo_iterator NewMemRefsEnd) {
    for (mmo_iterator MMI = NewMemRefs, MME = NewMemRefsEnd; MMI != MME; ++MMI)
      assert(*MMI && "Null mem ref detected!");
    MemRefs = NewMemRefs;
    MemRefsEnd = NewMemRefsEnd;
  }

  static bool classof(const SDNode *N) {
    return N->isMachineOpcode();
  }
};

class SDNodeIterator : public std::iterator<std::forward_iterator_tag,
                                            SDNode, ptrdiff_t> {
  const SDNode *Node;
  unsigned Operand;

  SDNodeIterator(const SDNode *N, unsigned Op) : Node(N), Operand(Op) {}
public:
  bool operator==(const SDNodeIterator& x) const {
    return Operand == x.Operand;
  }
  bool operator!=(const SDNodeIterator& x) const { return !operator==(x); }

  const SDNodeIterator &operator=(const SDNodeIterator &I) {
    assert(I.Node == Node && "Cannot assign iterators to two different nodes!");
    Operand = I.Operand;
    return *this;
  }

  pointer operator*() const {
    return Node->getOperand(Operand).getNode();
  }
  pointer operator->() const { return operator*(); }

  SDNodeIterator& operator++() {                // Preincrement
    ++Operand;
    return *this;
  }
  SDNodeIterator operator++(int) { // Postincrement
    SDNodeIterator tmp = *this; ++*this; return tmp;
  }
  size_t operator-(SDNodeIterator Other) const {
    assert(Node == Other.Node &&
           "Cannot compare iterators of two different nodes!");
    return Operand - Other.Operand;
  }

  static SDNodeIterator begin(const SDNode *N) { return SDNodeIterator(N, 0); }
  static SDNodeIterator end  (const SDNode *N) {
    return SDNodeIterator(N, N->getNumOperands());
  }

  unsigned getOperand() const { return Operand; }
  const SDNode *getNode() const { return Node; }
};

template <> struct GraphTraits<SDNode*> {
  typedef SDNode NodeType;
  typedef SDNodeIterator ChildIteratorType;
  static inline NodeType *getEntryNode(SDNode *N) { return N; }
  static inline ChildIteratorType child_begin(NodeType *N) {
    return SDNodeIterator::begin(N);
  }
  static inline ChildIteratorType child_end(NodeType *N) {
    return SDNodeIterator::end(N);
  }
};

/// LargestSDNode - The largest SDNode class.
///
typedef LoadSDNode LargestSDNode;

/// MostAlignedSDNode - The SDNode class with the greatest alignment
/// requirement.
///
typedef GlobalAddressSDNode MostAlignedSDNode;

namespace ISD {
  /// isNormalLoad - Returns true if the specified node is a non-extending
  /// and unindexed load.
  inline bool isNormalLoad(const SDNode *N) {
    const LoadSDNode *Ld = dyn_cast<LoadSDNode>(N);
    return Ld && Ld->getExtensionType() == ISD::NON_EXTLOAD &&
      Ld->getAddressingMode() == ISD::UNINDEXED;
  }

  /// isNON_EXTLoad - Returns true if the specified node is a non-extending
  /// load.
  inline bool isNON_EXTLoad(const SDNode *N) {
    return isa<LoadSDNode>(N) &&
      cast<LoadSDNode>(N)->getExtensionType() == ISD::NON_EXTLOAD;
  }

  /// isEXTLoad - Returns true if the specified node is a EXTLOAD.
  ///
  inline bool isEXTLoad(const SDNode *N) {
    return isa<LoadSDNode>(N) &&
      cast<LoadSDNode>(N)->getExtensionType() == ISD::EXTLOAD;
  }

  /// isSEXTLoad - Returns true if the specified node is a SEXTLOAD.
  ///
  inline bool isSEXTLoad(const SDNode *N) {
    return isa<LoadSDNode>(N) &&
      cast<LoadSDNode>(N)->getExtensionType() == ISD::SEXTLOAD;
  }

  /// isZEXTLoad - Returns true if the specified node is a ZEXTLOAD.
  ///
  inline bool isZEXTLoad(const SDNode *N) {
    return isa<LoadSDNode>(N) &&
      cast<LoadSDNode>(N)->getExtensionType() == ISD::ZEXTLOAD;
  }

  /// isUNINDEXEDLoad - Returns true if the specified node is an unindexed load.
  ///
  inline bool isUNINDEXEDLoad(const SDNode *N) {
    return isa<LoadSDNode>(N) &&
      cast<LoadSDNode>(N)->getAddressingMode() == ISD::UNINDEXED;
  }

  /// isNormalStore - Returns true if the specified node is a non-truncating
  /// and unindexed store.
  inline bool isNormalStore(const SDNode *N) {
    const StoreSDNode *St = dyn_cast<StoreSDNode>(N);
    return St && !St->isTruncatingStore() &&
      St->getAddressingMode() == ISD::UNINDEXED;
  }

  /// isNON_TRUNCStore - Returns true if the specified node is a non-truncating
  /// store.
  inline bool isNON_TRUNCStore(const SDNode *N) {
    return isa<StoreSDNode>(N) && !cast<StoreSDNode>(N)->isTruncatingStore();
  }

  /// isTRUNCStore - Returns true if the specified node is a truncating
  /// store.
  inline bool isTRUNCStore(const SDNode *N) {
    return isa<StoreSDNode>(N) && cast<StoreSDNode>(N)->isTruncatingStore();
  }

  /// isUNINDEXEDStore - Returns true if the specified node is an
  /// unindexed store.
  inline bool isUNINDEXEDStore(const SDNode *N) {
    return isa<StoreSDNode>(N) &&
      cast<StoreSDNode>(N)->getAddressingMode() == ISD::UNINDEXED;
  }
}

} // end llvm namespace

#endif
