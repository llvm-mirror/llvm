//===--- TrailingObjects.h - Variable-length classes ------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This header defines support for implementing classes that have
/// some trailing object (or arrays of objects) appended to them. The
/// main purpose is to make it obvious where this idiom is being used,
/// and to make the usage more idiomatic and more difficult to get
/// wrong.
///
/// The TrailingObject template abstracts away the reinterpret_cast,
/// pointer arithmetic, and size calculations used for the allocation
/// and access of appended arrays of objects, as well as asserts that
/// the alignment of the classes involved are appropriate for the
/// usage. Additionally, it ensures that the base type is final --
/// deriving from a class that expects data appended immediately after
/// it is typically not safe.
///
/// Users are expected to derive from this template, and provide
/// numTrailingObjects implementations for each trailing type,
/// e.g. like this sample:
///
/// \code
/// class VarLengthObj : private TrailingObjects<VarLengthObj, int, double> {
///   friend TrailingObjects;
///
///   unsigned NumInts, NumDoubles;
///   size_t numTrailingObjects(OverloadToken<int>) const { return NumInts; }
///   size_t numTrailingObjects(OverloadToken<double>) const {
///     return NumDoubles;
///   }
///  };
/// \endcode
///
/// You can access the appended arrays via 'getTrailingObjects', and
/// determine the size needed for allocation via
/// 'additionalSizeToAlloc' and 'totalSizeToAlloc'.
///
/// All the methods implemented by this class are are intended for use
/// by the implementation of the class, not as part of its interface
/// (thus, private inheritance is suggested).
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_TRAILINGOBJECTS_H
#define LLVM_SUPPORT_TRAILINGOBJECTS_H

#include <new>
#include <type_traits>
#include "llvm/Support/AlignOf.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/type_traits.h"

namespace llvm {

namespace trailing_objects_internal {
/// Helper template to calculate the max alignment requirement for a set of
/// objects.
template <typename First, typename... Rest> class AlignmentCalcHelper {
private:
  enum {
    FirstAlignment = AlignOf<First>::Alignment,
    RestAlignment = AlignmentCalcHelper<Rest...>::Alignment,
  };

public:
  enum {
    Alignment = FirstAlignment > RestAlignment ? FirstAlignment : RestAlignment
  };
};

template <typename First> class AlignmentCalcHelper<First> {
public:
  enum { Alignment = AlignOf<First>::Alignment };
};

/// The base class for TrailingObjects* classes.
class TrailingObjectsBase {
protected:
  /// OverloadToken's purpose is to allow specifying function overloads
  /// for different types, without actually taking the types as
  /// parameters. (Necessary because member function templates cannot
  /// be specialized, so overloads must be used instead of
  /// specialization.)
  template <typename T> struct OverloadToken {};
};

/// This helper template works-around MSVC 2013's lack of useful
/// alignas() support. The argument to LLVM_ALIGNAS(), in MSVC, is
/// required to be a literal integer. But, you *can* use template
/// specialization to select between a bunch of different LLVM_ALIGNAS
/// expressions...
template <int Align>
class TrailingObjectsAligner : public TrailingObjectsBase {};
template <>
class LLVM_ALIGNAS(1) TrailingObjectsAligner<1> : public TrailingObjectsBase {};
template <>
class LLVM_ALIGNAS(2) TrailingObjectsAligner<2> : public TrailingObjectsBase {};
template <>
class LLVM_ALIGNAS(4) TrailingObjectsAligner<4> : public TrailingObjectsBase {};
template <>
class LLVM_ALIGNAS(8) TrailingObjectsAligner<8> : public TrailingObjectsBase {};
template <>
class LLVM_ALIGNAS(16) TrailingObjectsAligner<16> : public TrailingObjectsBase {
};
template <>
class LLVM_ALIGNAS(32) TrailingObjectsAligner<32> : public TrailingObjectsBase {
};

// Just a little helper for transforming a type pack into the same
// number of a different type. e.g.:
//   ExtractSecondType<Foo..., int>::type
template <typename Ty1, typename Ty2> struct ExtractSecondType {
  typedef Ty2 type;
};

// TrailingObjectsImpl is somewhat complicated, because it is a
// recursively inheriting template, in order to handle the template
// varargs. Each level of inheritance picks off a single trailing type
// then recurses on the rest. The "Align", "BaseTy", and
// "TopTrailingObj" arguments are passed through unchanged through the
// recursion. "PrevTy" is, at each level, the type handled by the
// level right above it.

template <int Align, typename BaseTy, typename TopTrailingObj, typename PrevTy,
          typename... MoreTys>
struct TrailingObjectsImpl {
  // The main template definition is never used -- the two
  // specializations cover all possibilities.
};

template <int Align, typename BaseTy, typename TopTrailingObj, typename PrevTy,
          typename NextTy, typename... MoreTys>
struct TrailingObjectsImpl<Align, BaseTy, TopTrailingObj, PrevTy, NextTy,
                           MoreTys...>
    : public TrailingObjectsImpl<Align, BaseTy, TopTrailingObj, NextTy,
                                 MoreTys...> {

  typedef TrailingObjectsImpl<Align, BaseTy, TopTrailingObj, NextTy, MoreTys...>
      ParentType;

  // Ensure the methods we inherit are not hidden.
  using ParentType::getTrailingObjectsImpl;
  using ParentType::additionalSizeToAllocImpl;

  static void verifyTrailingObjectsAssertions() {
    static_assert(llvm::AlignOf<PrevTy>::Alignment >=
                      llvm::AlignOf<NextTy>::Alignment,
                  "A trailing object requires more alignment than the previous "
                  "trailing object provides");

    ParentType::verifyTrailingObjectsAssertions();
  }

  // These two functions are helper functions for
  // TrailingObjects::getTrailingObjects. They recurse to the left --
  // the result for each type in the list of trailing types depends on
  // the result of calling the function on the type to the
  // left. However, the function for the type to the left is
  // implemented by a *subclass* of this class, so we invoke it via
  // the TopTrailingObj, which is, via the
  // curiously-recurring-template-pattern, the most-derived type in
  // this recursion, and thus, contains all the overloads.
  static const NextTy *
  getTrailingObjectsImpl(const BaseTy *Obj,
                         TrailingObjectsBase::OverloadToken<NextTy>) {
    return reinterpret_cast<const NextTy *>(
        TopTrailingObj::getTrailingObjectsImpl(
            Obj, TrailingObjectsBase::OverloadToken<PrevTy>()) +
        TopTrailingObj::callNumTrailingObjects(
            Obj, TrailingObjectsBase::OverloadToken<PrevTy>()));
  }

  static NextTy *
  getTrailingObjectsImpl(BaseTy *Obj,
                         TrailingObjectsBase::OverloadToken<NextTy>) {
    return reinterpret_cast<NextTy *>(
        TopTrailingObj::getTrailingObjectsImpl(
            Obj, TrailingObjectsBase::OverloadToken<PrevTy>()) +
        TopTrailingObj::callNumTrailingObjects(
            Obj, TrailingObjectsBase::OverloadToken<PrevTy>()));
  }

  // Helper function for TrailingObjects::additionalSizeToAlloc: this
  // function recurses to superclasses, each of which requires one
  // fewer size_t argument, and adds its own size.
  static LLVM_CONSTEXPR size_t additionalSizeToAllocImpl(
      size_t Count1,
      typename ExtractSecondType<MoreTys, size_t>::type... MoreCounts) {
    return sizeof(NextTy) * Count1 + additionalSizeToAllocImpl(MoreCounts...);
  }
};

// The base case of the TrailingObjectsImpl inheritance recursion,
// when there's no more trailing types.
template <int Align, typename BaseTy, typename TopTrailingObj, typename PrevTy>
struct TrailingObjectsImpl<Align, BaseTy, TopTrailingObj, PrevTy>
    : public TrailingObjectsAligner<Align> {
  // This is a dummy method, only here so the "using" doesn't fail --
  // it will never be called, because this function recurses backwards
  // up the inheritance chain to subclasses.
  static void getTrailingObjectsImpl();

  static LLVM_CONSTEXPR size_t additionalSizeToAllocImpl() { return 0; }

  static void verifyTrailingObjectsAssertions() {}
};

} // end namespace trailing_objects_internal

// Finally, the main type defined in this file, the one intended for users...

/// See the file comment for details on the usage of the
/// TrailingObjects type.
template <typename BaseTy, typename... TrailingTys>
class TrailingObjects : private trailing_objects_internal::TrailingObjectsImpl<
                            trailing_objects_internal::AlignmentCalcHelper<
                                TrailingTys...>::Alignment,
                            BaseTy, TrailingObjects<BaseTy, TrailingTys...>,
                            BaseTy, TrailingTys...> {

  template <int A, typename B, typename T, typename P, typename... M>
  friend struct trailing_objects_internal::TrailingObjectsImpl;

  template <typename... Tys> class Foo {};

  typedef trailing_objects_internal::TrailingObjectsImpl<
      trailing_objects_internal::AlignmentCalcHelper<TrailingTys...>::Alignment,
      BaseTy, TrailingObjects<BaseTy, TrailingTys...>, BaseTy, TrailingTys...>
      ParentType;
  using TrailingObjectsBase = trailing_objects_internal::TrailingObjectsBase;

  using ParentType::getTrailingObjectsImpl;

  // Contains static_assert statements for the alignment of the
  // types. Must not be at class-level, because BaseTy isn't complete
  // at class instantiation time, but will be by the time this
  // function is instantiated. Recurses through the superclasses.
  static void verifyTrailingObjectsAssertions() {
#ifdef LLVM_IS_FINAL
    static_assert(LLVM_IS_FINAL(BaseTy), "BaseTy must be final.");
#endif
    ParentType::verifyTrailingObjectsAssertions();
  }

  // These two methods are the base of the recursion for this method.
  static const BaseTy *
  getTrailingObjectsImpl(const BaseTy *Obj,
                         TrailingObjectsBase::OverloadToken<BaseTy>) {
    return Obj;
  }

  static BaseTy *
  getTrailingObjectsImpl(BaseTy *Obj,
                         TrailingObjectsBase::OverloadToken<BaseTy>) {
    return Obj;
  }

  // callNumTrailingObjects simply calls numTrailingObjects on the
  // provided Obj -- except when the type being queried is BaseTy
  // itself. There is always only one of the base object, so that case
  // is handled here. (An additional benefit of indirecting through
  // this function is that consumers only say "friend
  // TrailingObjects", and thus, only this class itself can call the
  // numTrailingObjects function.)
  static size_t
  callNumTrailingObjects(const BaseTy *Obj,
                         TrailingObjectsBase::OverloadToken<BaseTy>) {
    return 1;
  }

  template <typename T>
  static size_t callNumTrailingObjects(const BaseTy *Obj,
                                       TrailingObjectsBase::OverloadToken<T>) {
    return Obj->numTrailingObjects(TrailingObjectsBase::OverloadToken<T>());
  }

public:
  // make this (privately inherited) class public.
  using ParentType::OverloadToken;

  /// Returns a pointer to the trailing object array of the given type
  /// (which must be one of those specified in the class template). The
  /// array may have zero or more elements in it.
  template <typename T> const T *getTrailingObjects() const {
    verifyTrailingObjectsAssertions();
    // Forwards to an impl function with overloads, since member
    // function templates can't be specialized.
    return this->getTrailingObjectsImpl(
        static_cast<const BaseTy *>(this),
        TrailingObjectsBase::OverloadToken<T>());
  }

  /// Returns a pointer to the trailing object array of the given type
  /// (which must be one of those specified in the class template). The
  /// array may have zero or more elements in it.
  template <typename T> T *getTrailingObjects() {
    verifyTrailingObjectsAssertions();
    // Forwards to an impl function with overloads, since member
    // function templates can't be specialized.
    return this->getTrailingObjectsImpl(
        static_cast<BaseTy *>(this), TrailingObjectsBase::OverloadToken<T>());
  }

  /// Returns the size of the trailing data, if an object were
  /// allocated with the given counts (The counts are in the same order
  /// as the template arguments). This does not include the size of the
  /// base object.  The template arguments must be the same as those
  /// used in the class; they are supplied here redundantly only so
  /// that it's clear what the counts are counting in callers.
  template <typename... Tys>
  static LLVM_CONSTEXPR typename std::enable_if<
      std::is_same<Foo<TrailingTys...>, Foo<Tys...>>::value, size_t>::type
      additionalSizeToAlloc(
          typename trailing_objects_internal::ExtractSecondType<
              TrailingTys, size_t>::type... Counts) {
    return ParentType::additionalSizeToAllocImpl(Counts...);
  }

  /// Returns the total size of an object if it were allocated with the
  /// given trailing object counts. This is the same as
  /// additionalSizeToAlloc, except it *does* include the size of the base
  /// object.
  template <typename... Tys>
  static LLVM_CONSTEXPR typename std::enable_if<
      std::is_same<Foo<TrailingTys...>, Foo<Tys...>>::value, size_t>::type
      totalSizeToAlloc(typename trailing_objects_internal::ExtractSecondType<
                       TrailingTys, size_t>::type... Counts) {
    return sizeof(BaseTy) + ParentType::additionalSizeToAllocImpl(Counts...);
  }
};

} // end namespace llvm

#endif
