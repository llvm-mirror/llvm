//===- llvm/ADT/PostOrderIterator.h - PostOrder iterator --------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file builds on the ADT/GraphTraits.h file to build a generic graph
// post order iterator.  This should work over any graph type that has a
// GraphTraits specialization.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_POSTORDERITERATOR_H
#define LLVM_ADT_POSTORDERITERATOR_H

#include "llvm/ADT/GraphTraits.h"
#include "llvm/ADT/SmallPtrSet.h"
#include <set>
#include <vector>

namespace llvm {

// The po_iterator_storage template provides access to the set of already
// visited nodes during the po_iterator's depth-first traversal.
//
// The default implementation simply contains a set of visited nodes, while
// the Extended=true version uses a reference to an external set.
//
// It is possible to prune the depth-first traversal in several ways:
//
// - When providing an external set that already contains some graph nodes,
//   those nodes won't be visited again. This is useful for restarting a
//   post-order traversal on a graph with nodes that aren't dominated by a
//   single node.
//
// - By providing a custom SetType class, unwanted graph nodes can be excluded
//   by having the insert() function return false. This could for example
//   confine a CFG traversal to blocks in a specific loop.
//
// - Finally, by specializing the po_iterator_storage template itself, graph
//   edges can be pruned by returning false in the insertEdge() function. This
//   could be used to remove loop back-edges from the CFG seen by po_iterator.
//
// A specialized po_iterator_storage class can observe both the pre-order and
// the post-order. The insertEdge() function is called in a pre-order, while
// the finishPostorder() function is called just before the po_iterator moves
// on to the next node.

/// Default po_iterator_storage implementation with an internal set object.
template<class SetType, bool External>
class po_iterator_storage {
  SetType Visited;
public:
  // Return true if edge destination should be visited.
  template<typename NodeType>
  bool insertEdge(NodeType *From, NodeType *To) {
    return Visited.insert(To);
  }

  // Called after all children of BB have been visited.
  template<typename NodeType>
  void finishPostorder(NodeType *BB) {}
};

/// Specialization of po_iterator_storage that references an external set.
template<class SetType>
class po_iterator_storage<SetType, true> {
  SetType &Visited;
public:
  po_iterator_storage(SetType &VSet) : Visited(VSet) {}
  po_iterator_storage(const po_iterator_storage &S) : Visited(S.Visited) {}

  // Return true if edge destination should be visited, called with From = 0 for
  // the root node.
  // Graph edges can be pruned by specializing this function.
  template<class NodeType>
  bool insertEdge(NodeType *From, NodeType *To) { return Visited.insert(To); }

  // Called after all children of BB have been visited.
  template<class NodeType>
  void finishPostorder(NodeType *BB) {}
};

template<class GraphT,
  class SetType = llvm::SmallPtrSet<typename GraphTraits<GraphT>::NodeType*, 8>,
  bool ExtStorage = false,
  class GT = GraphTraits<GraphT> >
class po_iterator : public std::iterator<std::forward_iterator_tag,
                                         typename GT::NodeType, ptrdiff_t>,
                    public po_iterator_storage<SetType, ExtStorage> {
  typedef std::iterator<std::forward_iterator_tag,
                        typename GT::NodeType, ptrdiff_t> super;
  typedef typename GT::NodeType          NodeType;
  typedef typename GT::ChildIteratorType ChildItTy;

  // VisitStack - Used to maintain the ordering.  Top = current block
  // First element is basic block pointer, second is the 'next child' to visit
  std::vector<std::pair<NodeType *, ChildItTy> > VisitStack;

  void traverseChild() {
    while (VisitStack.back().second != GT::child_end(VisitStack.back().first)) {
      NodeType *BB = *VisitStack.back().second++;
      if (this->insertEdge(VisitStack.back().first, BB)) {
        // If the block is not visited...
        VisitStack.push_back(std::make_pair(BB, GT::child_begin(BB)));
      }
    }
  }

  inline po_iterator(NodeType *BB) {
    this->insertEdge((NodeType*)0, BB);
    VisitStack.push_back(std::make_pair(BB, GT::child_begin(BB)));
    traverseChild();
  }
  inline po_iterator() {} // End is when stack is empty.

  inline po_iterator(NodeType *BB, SetType &S) :
    po_iterator_storage<SetType, ExtStorage>(S) {
    if (this->insertEdge((NodeType*)0, BB)) {
      VisitStack.push_back(std::make_pair(BB, GT::child_begin(BB)));
      traverseChild();
    }
  }

  inline po_iterator(SetType &S) :
      po_iterator_storage<SetType, ExtStorage>(S) {
  } // End is when stack is empty.
public:
  typedef typename super::pointer pointer;
  typedef po_iterator<GraphT, SetType, ExtStorage, GT> _Self;

  // Provide static "constructors"...
  static inline _Self begin(GraphT G) { return _Self(GT::getEntryNode(G)); }
  static inline _Self end  (GraphT G) { return _Self(); }

  static inline _Self begin(GraphT G, SetType &S) {
    return _Self(GT::getEntryNode(G), S);
  }
  static inline _Self end  (GraphT G, SetType &S) { return _Self(S); }

  inline bool operator==(const _Self& x) const {
    return VisitStack == x.VisitStack;
  }
  inline bool operator!=(const _Self& x) const { return !operator==(x); }

  inline pointer operator*() const {
    return VisitStack.back().first;
  }

  // This is a nonstandard operator-> that dereferences the pointer an extra
  // time... so that you can actually call methods ON the BasicBlock, because
  // the contained type is a pointer.  This allows BBIt->getTerminator() f.e.
  //
  inline NodeType *operator->() const { return operator*(); }

  inline _Self& operator++() {   // Preincrement
    this->finishPostorder(VisitStack.back().first);
    VisitStack.pop_back();
    if (!VisitStack.empty())
      traverseChild();
    return *this;
  }

  inline _Self operator++(int) { // Postincrement
    _Self tmp = *this; ++*this; return tmp;
  }
};

// Provide global constructors that automatically figure out correct types...
//
template <class T>
po_iterator<T> po_begin(T G) { return po_iterator<T>::begin(G); }
template <class T>
po_iterator<T> po_end  (T G) { return po_iterator<T>::end(G); }

// Provide global definitions of external postorder iterators...
template<class T, class SetType=std::set<typename GraphTraits<T>::NodeType*> >
struct po_ext_iterator : public po_iterator<T, SetType, true> {
  po_ext_iterator(const po_iterator<T, SetType, true> &V) :
  po_iterator<T, SetType, true>(V) {}
};

template<class T, class SetType>
po_ext_iterator<T, SetType> po_ext_begin(T G, SetType &S) {
  return po_ext_iterator<T, SetType>::begin(G, S);
}

template<class T, class SetType>
po_ext_iterator<T, SetType> po_ext_end(T G, SetType &S) {
  return po_ext_iterator<T, SetType>::end(G, S);
}

// Provide global definitions of inverse post order iterators...
template <class T,
          class SetType = std::set<typename GraphTraits<T>::NodeType*>,
          bool External = false>
struct ipo_iterator : public po_iterator<Inverse<T>, SetType, External > {
  ipo_iterator(const po_iterator<Inverse<T>, SetType, External> &V) :
     po_iterator<Inverse<T>, SetType, External> (V) {}
};

template <class T>
ipo_iterator<T> ipo_begin(T G, bool Reverse = false) {
  return ipo_iterator<T>::begin(G, Reverse);
}

template <class T>
ipo_iterator<T> ipo_end(T G){
  return ipo_iterator<T>::end(G);
}

// Provide global definitions of external inverse postorder iterators...
template <class T,
          class SetType = std::set<typename GraphTraits<T>::NodeType*> >
struct ipo_ext_iterator : public ipo_iterator<T, SetType, true> {
  ipo_ext_iterator(const ipo_iterator<T, SetType, true> &V) :
    ipo_iterator<T, SetType, true>(V) {}
  ipo_ext_iterator(const po_iterator<Inverse<T>, SetType, true> &V) :
    ipo_iterator<T, SetType, true>(V) {}
};

template <class T, class SetType>
ipo_ext_iterator<T, SetType> ipo_ext_begin(T G, SetType &S) {
  return ipo_ext_iterator<T, SetType>::begin(G, S);
}

template <class T, class SetType>
ipo_ext_iterator<T, SetType> ipo_ext_end(T G, SetType &S) {
  return ipo_ext_iterator<T, SetType>::end(G, S);
}

//===--------------------------------------------------------------------===//
// Reverse Post Order CFG iterator code
//===--------------------------------------------------------------------===//
//
// This is used to visit basic blocks in a method in reverse post order.  This
// class is awkward to use because I don't know a good incremental algorithm to
// computer RPO from a graph.  Because of this, the construction of the
// ReversePostOrderTraversal object is expensive (it must walk the entire graph
// with a postorder iterator to build the data structures).  The moral of this
// story is: Don't create more ReversePostOrderTraversal classes than necessary.
//
// This class should be used like this:
// {
//   ReversePostOrderTraversal<Function*> RPOT(FuncPtr); // Expensive to create
//   for (rpo_iterator I = RPOT.begin(); I != RPOT.end(); ++I) {
//      ...
//   }
//   for (rpo_iterator I = RPOT.begin(); I != RPOT.end(); ++I) {
//      ...
//   }
// }
//

template<class GraphT, class GT = GraphTraits<GraphT> >
class ReversePostOrderTraversal {
  typedef typename GT::NodeType NodeType;
  std::vector<NodeType*> Blocks;       // Block list in normal PO order
  inline void Initialize(NodeType *BB) {
    std::copy(po_begin(BB), po_end(BB), std::back_inserter(Blocks));
  }
public:
  typedef typename std::vector<NodeType*>::reverse_iterator rpo_iterator;

  inline ReversePostOrderTraversal(GraphT G) {
    Initialize(GT::getEntryNode(G));
  }

  // Because we want a reverse post order, use reverse iterators from the vector
  inline rpo_iterator begin() { return Blocks.rbegin(); }
  inline rpo_iterator end()   { return Blocks.rend(); }
};

} // End llvm namespace

#endif
