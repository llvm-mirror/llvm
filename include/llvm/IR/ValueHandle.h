//===- ValueHandle.h - Value Smart Pointer classes --------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares the ValueHandle class and its sub-classes.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_VALUEHANDLE_H
#define LLVM_IR_VALUEHANDLE_H

#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/IR/Value.h"

namespace llvm {
class ValueHandleBase;
template<typename From> struct simplify_type;

// ValueHandleBase** is only 4-byte aligned.
template<>
class PointerLikeTypeTraits<ValueHandleBase**> {
public:
  static inline void *getAsVoidPointer(ValueHandleBase** P) { return P; }
  static inline ValueHandleBase **getFromVoidPointer(void *P) {
    return static_cast<ValueHandleBase**>(P);
  }
  enum { NumLowBitsAvailable = 2 };
};

/// \brief This is the common base class of value handles.
///
/// ValueHandle's are smart pointers to Value's that have special behavior when
/// the value is deleted or ReplaceAllUsesWith'd.  See the specific handles
/// below for details.
class ValueHandleBase {
  friend class Value;
protected:
  /// \brief This indicates what sub class the handle actually is.
  ///
  /// This is to avoid having a vtable for the light-weight handle pointers. The
  /// fully general Callback version does have a vtable.
  enum HandleBaseKind {
    Assert,
    Callback,
    Tracking,
    Weak
  };

private:
  PointerIntPair<ValueHandleBase**, 2, HandleBaseKind> PrevPair;
  ValueHandleBase *Next;

  Value* V;

  ValueHandleBase(const ValueHandleBase&) LLVM_DELETED_FUNCTION;
public:
  explicit ValueHandleBase(HandleBaseKind Kind)
    : PrevPair(nullptr, Kind), Next(nullptr), V(nullptr) {}
  ValueHandleBase(HandleBaseKind Kind, Value *V)
    : PrevPair(nullptr, Kind), Next(nullptr), V(V) {
    if (isValid(V))
      AddToUseList();
  }
  ValueHandleBase(HandleBaseKind Kind, const ValueHandleBase &RHS)
    : PrevPair(nullptr, Kind), Next(nullptr), V(RHS.V) {
    if (isValid(V))
      AddToExistingUseList(RHS.getPrevPtr());
  }
  ~ValueHandleBase() {
    if (isValid(V))
      RemoveFromUseList();
  }

  Value *operator=(Value *RHS) {
    if (V == RHS) return RHS;
    if (isValid(V)) RemoveFromUseList();
    V = RHS;
    if (isValid(V)) AddToUseList();
    return RHS;
  }

  Value *operator=(const ValueHandleBase &RHS) {
    if (V == RHS.V) return RHS.V;
    if (isValid(V)) RemoveFromUseList();
    V = RHS.V;
    if (isValid(V)) AddToExistingUseList(RHS.getPrevPtr());
    return V;
  }

  Value *operator->() const { return V; }
  Value &operator*() const { return *V; }

protected:
  Value *getValPtr() const { return V; }

  static bool isValid(Value *V) {
    return V &&
           V != DenseMapInfo<Value *>::getEmptyKey() &&
           V != DenseMapInfo<Value *>::getTombstoneKey();
  }

public:
  // Callbacks made from Value.
  static void ValueIsDeleted(Value *V);
  static void ValueIsRAUWd(Value *Old, Value *New);

private:
  // Internal implementation details.
  ValueHandleBase **getPrevPtr() const { return PrevPair.getPointer(); }
  HandleBaseKind getKind() const { return PrevPair.getInt(); }
  void setPrevPtr(ValueHandleBase **Ptr) { PrevPair.setPointer(Ptr); }

  /// \brief Add this ValueHandle to the use list for V.
  ///
  /// List is the address of either the head of the list or a Next node within
  /// the existing use list.
  void AddToExistingUseList(ValueHandleBase **List);

  /// \brief Add this ValueHandle to the use list after Node.
  void AddToExistingUseListAfter(ValueHandleBase *Node);

  /// \brief Add this ValueHandle to the use list for V.
  void AddToUseList();
  /// \brief Remove this ValueHandle from its current use list.
  void RemoveFromUseList();
};

/// \brief Value handle that is nullable, but tries to track the Value.
///
/// This is a value handle that tries hard to point to a Value, even across
/// RAUW operations, but will null itself out if the value is destroyed.  this
/// is useful for advisory sorts of information, but should not be used as the
/// key of a map (since the map would have to rearrange itself when the pointer
/// changes).
class WeakVH : public ValueHandleBase {
public:
  WeakVH() : ValueHandleBase(Weak) {}
  WeakVH(Value *P) : ValueHandleBase(Weak, P) {}
  WeakVH(const WeakVH &RHS)
    : ValueHandleBase(Weak, RHS) {}

  Value *operator=(Value *RHS) {
    return ValueHandleBase::operator=(RHS);
  }
  Value *operator=(const ValueHandleBase &RHS) {
    return ValueHandleBase::operator=(RHS);
  }

  operator Value*() const {
    return getValPtr();
  }
};

// Specialize simplify_type to allow WeakVH to participate in
// dyn_cast, isa, etc.
template<> struct simplify_type<WeakVH> {
  typedef Value* SimpleType;
  static SimpleType getSimplifiedValue(WeakVH &WVH) {
    return WVH;
  }
};

/// \brief Value handle that asserts if the Value is deleted.
///
/// This is a Value Handle that points to a value and asserts out if the value
/// is destroyed while the handle is still live.  This is very useful for
/// catching dangling pointer bugs and other things which can be non-obvious.
/// One particularly useful place to use this is as the Key of a map.  Dangling
/// pointer bugs often lead to really subtle bugs that only occur if another
/// object happens to get allocated to the same address as the old one.  Using
/// an AssertingVH ensures that an assert is triggered as soon as the bad
/// delete occurs.
///
/// Note that an AssertingVH handle does *not* follow values across RAUW
/// operations.  This means that RAUW's need to explicitly update the
/// AssertingVH's as it moves.  This is required because in non-assert mode this
/// class turns into a trivial wrapper around a pointer.
template <typename ValueTy>
class AssertingVH
#ifndef NDEBUG
  : public ValueHandleBase
#endif
  {
  friend struct DenseMapInfo<AssertingVH<ValueTy> >;

#ifndef NDEBUG
  Value *getRawValPtr() const { return ValueHandleBase::getValPtr(); }
  void setRawValPtr(Value *P) { ValueHandleBase::operator=(P); }
#else
  Value *ThePtr;
  Value *getRawValPtr() const { return ThePtr; }
  void setRawValPtr(Value *P) { ThePtr = P; }
#endif
  // Convert a ValueTy*, which may be const, to the raw Value*.
  static Value *GetAsValue(Value *V) { return V; }
  static Value *GetAsValue(const Value *V) { return const_cast<Value*>(V); }

  ValueTy *getValPtr() const { return static_cast<ValueTy *>(getRawValPtr()); }
  void setValPtr(ValueTy *P) { setRawValPtr(GetAsValue(P)); }

public:
#ifndef NDEBUG
  AssertingVH() : ValueHandleBase(Assert) {}
  AssertingVH(ValueTy *P) : ValueHandleBase(Assert, GetAsValue(P)) {}
  AssertingVH(const AssertingVH &RHS) : ValueHandleBase(Assert, RHS) {}
#else
  AssertingVH() : ThePtr(nullptr) {}
  AssertingVH(ValueTy *P) : ThePtr(GetAsValue(P)) {}
#endif

  operator ValueTy*() const {
    return getValPtr();
  }

  ValueTy *operator=(ValueTy *RHS) {
    setValPtr(RHS);
    return getValPtr();
  }
  ValueTy *operator=(const AssertingVH<ValueTy> &RHS) {
    setValPtr(RHS.getValPtr());
    return getValPtr();
  }

  ValueTy *operator->() const { return getValPtr(); }
  ValueTy &operator*() const { return *getValPtr(); }
};

// Specialize DenseMapInfo to allow AssertingVH to participate in DenseMap.
template<typename T>
struct DenseMapInfo<AssertingVH<T> > {
  static inline AssertingVH<T> getEmptyKey() {
    AssertingVH<T> Res;
    Res.setRawValPtr(DenseMapInfo<Value *>::getEmptyKey());
    return Res;
  }
  static inline AssertingVH<T> getTombstoneKey() {
    AssertingVH<T> Res;
    Res.setRawValPtr(DenseMapInfo<Value *>::getTombstoneKey());
    return Res;
  }
  static unsigned getHashValue(const AssertingVH<T> &Val) {
    return DenseMapInfo<Value *>::getHashValue(Val.getRawValPtr());
  }
  static bool isEqual(const AssertingVH<T> &LHS, const AssertingVH<T> &RHS) {
    return DenseMapInfo<Value *>::isEqual(LHS.getRawValPtr(),
                                          RHS.getRawValPtr());
  }
};

template <typename T>
struct isPodLike<AssertingVH<T> > {
#ifdef NDEBUG
  static const bool value = true;
#else
  static const bool value = false;
#endif
};


/// \brief Value handle that tracks a Value across RAUW.
///
/// TrackingVH is designed for situations where a client needs to hold a handle
/// to a Value (or subclass) across some operations which may move that value,
/// but should never destroy it or replace it with some unacceptable type.
///
/// It is an error to do anything with a TrackingVH whose value has been
/// destroyed, except to destruct it.
///
/// It is an error to attempt to replace a value with one of a type which is
/// incompatible with any of its outstanding TrackingVHs.
template<typename ValueTy>
class TrackingVH : public ValueHandleBase {
  void CheckValidity() const {
    Value *VP = ValueHandleBase::getValPtr();

    // Null is always ok.
    if (!VP) return;

    // Check that this value is valid (i.e., it hasn't been deleted). We
    // explicitly delay this check until access to avoid requiring clients to be
    // unnecessarily careful w.r.t. destruction.
    assert(ValueHandleBase::isValid(VP) && "Tracked Value was deleted!");

    // Check that the value is a member of the correct subclass. We would like
    // to check this property on assignment for better debugging, but we don't
    // want to require a virtual interface on this VH. Instead we allow RAUW to
    // replace this value with a value of an invalid type, and check it here.
    assert(isa<ValueTy>(VP) &&
           "Tracked Value was replaced by one with an invalid type!");
  }

  ValueTy *getValPtr() const {
    CheckValidity();
    return (ValueTy*)ValueHandleBase::getValPtr();
  }
  void setValPtr(ValueTy *P) {
    CheckValidity();
    ValueHandleBase::operator=(GetAsValue(P));
  }

  // Convert a ValueTy*, which may be const, to the type the base
  // class expects.
  static Value *GetAsValue(Value *V) { return V; }
  static Value *GetAsValue(const Value *V) { return const_cast<Value*>(V); }

public:
  TrackingVH() : ValueHandleBase(Tracking) {}
  TrackingVH(ValueTy *P) : ValueHandleBase(Tracking, GetAsValue(P)) {}
  TrackingVH(const TrackingVH &RHS) : ValueHandleBase(Tracking, RHS) {}

  operator ValueTy*() const {
    return getValPtr();
  }

  ValueTy *operator=(ValueTy *RHS) {
    setValPtr(RHS);
    return getValPtr();
  }
  ValueTy *operator=(const TrackingVH<ValueTy> &RHS) {
    setValPtr(RHS.getValPtr());
    return getValPtr();
  }

  ValueTy *operator->() const { return getValPtr(); }
  ValueTy &operator*() const { return *getValPtr(); }
};

/// \brief Value handle with callbacks on RAUW and destruction.
///
/// This is a value handle that allows subclasses to define callbacks that run
/// when the underlying Value has RAUW called on it or is destroyed.  This
/// class can be used as the key of a map, as long as the user takes it out of
/// the map before calling setValPtr() (since the map has to rearrange itself
/// when the pointer changes).  Unlike ValueHandleBase, this class has a vtable
/// and a virtual destructor.
class CallbackVH : public ValueHandleBase {
  virtual void anchor();
protected:
  CallbackVH(const CallbackVH &RHS)
    : ValueHandleBase(Callback, RHS) {}

  virtual ~CallbackVH() {}

  void setValPtr(Value *P) {
    ValueHandleBase::operator=(P);
  }

public:
  CallbackVH() : ValueHandleBase(Callback) {}
  CallbackVH(Value *P) : ValueHandleBase(Callback, P) {}

  operator Value*() const {
    return getValPtr();
  }

  /// \brief Callback for Value destruction.
  ///
  /// Called when this->getValPtr() is destroyed, inside ~Value(), so you
  /// may call any non-virtual Value method on getValPtr(), but no subclass
  /// methods.  If WeakVH were implemented as a CallbackVH, it would use this
  /// method to call setValPtr(NULL).  AssertingVH would use this method to
  /// cause an assertion failure.
  ///
  /// All implementations must remove the reference from this object to the
  /// Value that's being destroyed.
  virtual void deleted() { setValPtr(nullptr); }

  /// \brief Callback for Value RAUW.
  ///
  /// Called when this->getValPtr()->replaceAllUsesWith(new_value) is called,
  /// _before_ any of the uses have actually been replaced.  If WeakVH were
  /// implemented as a CallbackVH, it would use this method to call
  /// setValPtr(new_value).  AssertingVH would do nothing in this method.
  virtual void allUsesReplacedWith(Value *) {}
};

} // End llvm namespace

#endif
