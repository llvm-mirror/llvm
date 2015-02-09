//===-- llvm/Support/Compiler.h - Compiler abstraction support --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines several macros, based on the current compiler.  This allows
// use of compiler-specific features in a way that remains portable.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_COMPILER_H
#define LLVM_SUPPORT_COMPILER_H

#include "llvm/Config/llvm-config.h"

#ifndef __has_feature
# define __has_feature(x) 0
#endif

#ifndef __has_extension
# define __has_extension(x) 0
#endif

#ifndef __has_attribute
# define __has_attribute(x) 0
#endif

#ifndef __has_builtin
# define __has_builtin(x) 0
#endif

/// \macro LLVM_GNUC_PREREQ
/// \brief Extend the default __GNUC_PREREQ even if glibc's features.h isn't
/// available.
#ifndef LLVM_GNUC_PREREQ
# if defined(__GNUC__) && defined(__GNUC_MINOR__) && defined(__GNUC_PATCHLEVEL__)
#  define LLVM_GNUC_PREREQ(maj, min, patch) \
    ((__GNUC__ << 20) + (__GNUC_MINOR__ << 10) + __GNUC_PATCHLEVEL__ >= \
     ((maj) << 20) + ((min) << 10) + (patch))
# elif defined(__GNUC__) && defined(__GNUC_MINOR__)
#  define LLVM_GNUC_PREREQ(maj, min, patch) \
    ((__GNUC__ << 20) + (__GNUC_MINOR__ << 10) >= ((maj) << 20) + ((min) << 10))
# else
#  define LLVM_GNUC_PREREQ(maj, min, patch) 0
# endif
#endif

/// \macro LLVM_MSC_PREREQ
/// \brief Is the compiler MSVC of at least the specified version?
/// The common \param version values to check for are:
///  * 1700: Microsoft Visual Studio 2012 / 11.0
///  * 1800: Microsoft Visual Studio 2013 / 12.0
#ifdef _MSC_VER
#define LLVM_MSC_PREREQ(version) (_MSC_VER >= (version))

// We require at least MSVC 2012.
#if !LLVM_MSC_PREREQ(1700)
#error LLVM requires at least MSVC 2012.
#endif

#else
#define LLVM_MSC_PREREQ(version) 0
#endif

#if !defined(_MSC_VER) || defined(__clang__) || LLVM_MSC_PREREQ(1900)
#define LLVM_NOEXCEPT noexcept
#else
#define LLVM_NOEXCEPT
#endif

/// \brief Does the compiler support r-value reference *this?
///
/// Sadly, this is separate from just r-value reference support because GCC
/// implemented this later than everything else.
#if __has_feature(cxx_rvalue_references) || LLVM_GNUC_PREREQ(4, 8, 1)
#define LLVM_HAS_RVALUE_REFERENCE_THIS 1
#else
#define LLVM_HAS_RVALUE_REFERENCE_THIS 0
#endif

/// \macro LLVM_HAS_VARIADIC_TEMPLATES
/// \brief Does this compiler support variadic templates.
///
/// Implies LLVM_HAS_RVALUE_REFERENCES and the existence of std::forward.
#if __has_feature(cxx_variadic_templates) || LLVM_MSC_PREREQ(1800)
# define LLVM_HAS_VARIADIC_TEMPLATES 1
#else
# define LLVM_HAS_VARIADIC_TEMPLATES 0
#endif

/// Expands to '&' if r-value references are supported.
///
/// This can be used to provide l-value/r-value overrides of member functions.
/// The r-value override should be guarded by LLVM_HAS_RVALUE_REFERENCE_THIS
#if LLVM_HAS_RVALUE_REFERENCE_THIS
#define LLVM_LVALUE_FUNCTION &
#else
#define LLVM_LVALUE_FUNCTION
#endif

/// LLVM_DELETED_FUNCTION - Expands to = delete if the compiler supports it.
/// Use to mark functions as uncallable. Member functions with this should
/// be declared private so that some behavior is kept in C++03 mode.
///
/// class DontCopy {
/// private:
///   DontCopy(const DontCopy&) LLVM_DELETED_FUNCTION;
///   DontCopy &operator =(const DontCopy&) LLVM_DELETED_FUNCTION;
/// public:
///   ...
/// };
#if __has_feature(cxx_deleted_functions) || \
    defined(__GXX_EXPERIMENTAL_CXX0X__) || LLVM_MSC_PREREQ(1800)
#define LLVM_DELETED_FUNCTION = delete
#else
#define LLVM_DELETED_FUNCTION
#endif

#if __has_feature(cxx_constexpr) || defined(__GXX_EXPERIMENTAL_CXX0X__)
# define LLVM_CONSTEXPR constexpr
#else
# define LLVM_CONSTEXPR
#endif

/// LLVM_LIBRARY_VISIBILITY - If a class marked with this attribute is linked
/// into a shared library, then the class should be private to the library and
/// not accessible from outside it.  Can also be used to mark variables and
/// functions, making them private to any shared library they are linked into.
/// On PE/COFF targets, library visibility is the default, so this isn't needed.
#if (__has_attribute(visibility) || LLVM_GNUC_PREREQ(4, 0, 0)) &&              \
    !defined(__MINGW32__) && !defined(__CYGWIN__) && !defined(LLVM_ON_WIN32)
#define LLVM_LIBRARY_VISIBILITY __attribute__ ((visibility("hidden")))
#else
#define LLVM_LIBRARY_VISIBILITY
#endif

#if __has_attribute(sentinel) || LLVM_GNUC_PREREQ(3, 0, 0)
#define LLVM_END_WITH_NULL __attribute__((sentinel))
#else
#define LLVM_END_WITH_NULL
#endif

#if __has_attribute(used) || LLVM_GNUC_PREREQ(3, 1, 0)
#define LLVM_ATTRIBUTE_USED __attribute__((__used__))
#else
#define LLVM_ATTRIBUTE_USED
#endif

#if __has_attribute(warn_unused_result) || LLVM_GNUC_PREREQ(3, 4, 0)
#define LLVM_ATTRIBUTE_UNUSED_RESULT __attribute__((__warn_unused_result__))
#else
#define LLVM_ATTRIBUTE_UNUSED_RESULT
#endif

// Some compilers warn about unused functions. When a function is sometimes
// used or not depending on build settings (e.g. a function only called from
// within "assert"), this attribute can be used to suppress such warnings.
//
// However, it shouldn't be used for unused *variables*, as those have a much
// more portable solution:
//   (void)unused_var_name;
// Prefer cast-to-void wherever it is sufficient.
#if __has_attribute(unused) || LLVM_GNUC_PREREQ(3, 1, 0)
#define LLVM_ATTRIBUTE_UNUSED __attribute__((__unused__))
#else
#define LLVM_ATTRIBUTE_UNUSED
#endif

// FIXME: Provide this for PE/COFF targets.
#if (__has_attribute(weak) || LLVM_GNUC_PREREQ(4, 0, 0)) &&                    \
    (!defined(__MINGW32__) && !defined(__CYGWIN__) && !defined(LLVM_ON_WIN32))
#define LLVM_ATTRIBUTE_WEAK __attribute__((__weak__))
#else
#define LLVM_ATTRIBUTE_WEAK
#endif

// Prior to clang 3.2, clang did not accept any spelling of
// __has_attribute(const), so assume it is supported.
#if defined(__clang__) || defined(__GNUC__)
// aka 'CONST' but following LLVM Conventions.
#define LLVM_READNONE __attribute__((__const__))
#else
#define LLVM_READNONE
#endif

#if __has_attribute(pure) || defined(__GNUC__)
// aka 'PURE' but following LLVM Conventions.
#define LLVM_READONLY __attribute__((__pure__))
#else
#define LLVM_READONLY
#endif

#if __has_builtin(__builtin_expect) || LLVM_GNUC_PREREQ(4, 0, 0)
#define LLVM_LIKELY(EXPR) __builtin_expect((bool)(EXPR), true)
#define LLVM_UNLIKELY(EXPR) __builtin_expect((bool)(EXPR), false)
#else
#define LLVM_LIKELY(EXPR) (EXPR)
#define LLVM_UNLIKELY(EXPR) (EXPR)
#endif

// C++ doesn't support 'extern template' of template specializations.  GCC does,
// but requires __extension__ before it.  In the header, use this:
//   EXTERN_TEMPLATE_INSTANTIATION(class foo<bar>);
// in the .cpp file, use this:
//   TEMPLATE_INSTANTIATION(class foo<bar>);
#ifdef __GNUC__
#define EXTERN_TEMPLATE_INSTANTIATION(X) __extension__ extern template X
#define TEMPLATE_INSTANTIATION(X) template X
#else
#define EXTERN_TEMPLATE_INSTANTIATION(X)
#define TEMPLATE_INSTANTIATION(X)
#endif

/// LLVM_ATTRIBUTE_NOINLINE - On compilers where we have a directive to do so,
/// mark a method "not for inlining".
#if __has_attribute(noinline) || LLVM_GNUC_PREREQ(3, 4, 0)
#define LLVM_ATTRIBUTE_NOINLINE __attribute__((noinline))
#elif defined(_MSC_VER)
#define LLVM_ATTRIBUTE_NOINLINE __declspec(noinline)
#else
#define LLVM_ATTRIBUTE_NOINLINE
#endif

/// LLVM_ATTRIBUTE_ALWAYS_INLINE - On compilers where we have a directive to do
/// so, mark a method "always inline" because it is performance sensitive. GCC
/// 3.4 supported this but is buggy in various cases and produces unimplemented
/// errors, just use it in GCC 4.0 and later.
#if __has_attribute(always_inline) || LLVM_GNUC_PREREQ(4, 0, 0)
#define LLVM_ATTRIBUTE_ALWAYS_INLINE inline __attribute__((always_inline))
#elif defined(_MSC_VER)
#define LLVM_ATTRIBUTE_ALWAYS_INLINE __forceinline
#else
#define LLVM_ATTRIBUTE_ALWAYS_INLINE
#endif

#ifdef __GNUC__
#define LLVM_ATTRIBUTE_NORETURN __attribute__((noreturn))
#elif defined(_MSC_VER)
#define LLVM_ATTRIBUTE_NORETURN __declspec(noreturn)
#else
#define LLVM_ATTRIBUTE_NORETURN
#endif

#if __has_attribute(returns_nonnull) || LLVM_GNUC_PREREQ(4, 9, 0)
#define LLVM_ATTRIBUTE_RETURNS_NONNULL __attribute__((returns_nonnull))
#else
#define LLVM_ATTRIBUTE_RETURNS_NONNULL
#endif

/// LLVM_EXTENSION - Support compilers where we have a keyword to suppress
/// pedantic diagnostics.
#ifdef __GNUC__
#define LLVM_EXTENSION __extension__
#else
#define LLVM_EXTENSION
#endif

// LLVM_ATTRIBUTE_DEPRECATED(decl, "message")
#if __has_feature(attribute_deprecated_with_message)
# define LLVM_ATTRIBUTE_DEPRECATED(decl, message) \
  decl __attribute__((deprecated(message)))
#elif defined(__GNUC__)
# define LLVM_ATTRIBUTE_DEPRECATED(decl, message) \
  decl __attribute__((deprecated))
#elif defined(_MSC_VER)
# define LLVM_ATTRIBUTE_DEPRECATED(decl, message) \
  __declspec(deprecated(message)) decl
#else
# define LLVM_ATTRIBUTE_DEPRECATED(decl, message) \
  decl
#endif

/// LLVM_BUILTIN_UNREACHABLE - On compilers which support it, expands
/// to an expression which states that it is undefined behavior for the
/// compiler to reach this point.  Otherwise is not defined.
#if __has_builtin(__builtin_unreachable) || LLVM_GNUC_PREREQ(4, 5, 0)
# define LLVM_BUILTIN_UNREACHABLE __builtin_unreachable()
#elif defined(_MSC_VER)
# define LLVM_BUILTIN_UNREACHABLE __assume(false)
#endif

/// LLVM_BUILTIN_TRAP - On compilers which support it, expands to an expression
/// which causes the program to exit abnormally.
#if __has_builtin(__builtin_trap) || LLVM_GNUC_PREREQ(4, 3, 0)
# define LLVM_BUILTIN_TRAP __builtin_trap()
#else
# define LLVM_BUILTIN_TRAP *(volatile int*)0x11 = 0
#endif

/// \macro LLVM_ASSUME_ALIGNED
/// \brief Returns a pointer with an assumed alignment.
#if __has_builtin(__builtin_assume_aligned) || LLVM_GNUC_PREREQ(4, 7, 0)
# define LLVM_ASSUME_ALIGNED(p, a) __builtin_assume_aligned(p, a)
#elif defined(LLVM_BUILTIN_UNREACHABLE)
// As of today, clang does not support __builtin_assume_aligned.
# define LLVM_ASSUME_ALIGNED(p, a) \
           (((uintptr_t(p) % (a)) == 0) ? (p) : (LLVM_BUILTIN_UNREACHABLE, (p)))
#else
# define LLVM_ASSUME_ALIGNED(p, a) (p)
#endif

/// \macro LLVM_FUNCTION_NAME
/// \brief Expands to __func__ on compilers which support it.  Otherwise,
/// expands to a compiler-dependent replacement.
#if defined(_MSC_VER)
# define LLVM_FUNCTION_NAME __FUNCTION__
#else
# define LLVM_FUNCTION_NAME __func__
#endif

/// \macro LLVM_MEMORY_SANITIZER_BUILD
/// \brief Whether LLVM itself is built with MemorySanitizer instrumentation.
#if __has_feature(memory_sanitizer)
# define LLVM_MEMORY_SANITIZER_BUILD 1
# include <sanitizer/msan_interface.h>
#else
# define LLVM_MEMORY_SANITIZER_BUILD 0
# define __msan_allocated_memory(p, size)
# define __msan_unpoison(p, size)
#endif

/// \macro LLVM_ADDRESS_SANITIZER_BUILD
/// \brief Whether LLVM itself is built with AddressSanitizer instrumentation.
#if __has_feature(address_sanitizer) || defined(__SANITIZE_ADDRESS__)
# define LLVM_ADDRESS_SANITIZER_BUILD 1
#else
# define LLVM_ADDRESS_SANITIZER_BUILD 0
#endif

/// \macro LLVM_IS_UNALIGNED_ACCESS_FAST
/// \brief Is unaligned memory access fast on the host machine.
///
/// Don't specialize on alignment for platforms where unaligned memory accesses
/// generates the same code as aligned memory accesses for common types.
#if defined(_M_AMD64) || defined(_M_IX86) || defined(__amd64) || \
    defined(__amd64__) || defined(__x86_64) || defined(__x86_64__) || \
    defined(_X86_) || defined(__i386) || defined(__i386__)
# define LLVM_IS_UNALIGNED_ACCESS_FAST 1
#else
# define LLVM_IS_UNALIGNED_ACCESS_FAST 0
#endif

/// \macro LLVM_EXPLICIT
/// \brief Expands to explicit on compilers which support explicit conversion
/// operators. Otherwise expands to nothing.
#if __has_feature(cxx_explicit_conversions) || \
    defined(__GXX_EXPERIMENTAL_CXX0X__) || LLVM_MSC_PREREQ(1800)
#define LLVM_EXPLICIT explicit
#else
#define LLVM_EXPLICIT
#endif

/// \brief Does the compiler support generalized initializers (using braced
/// lists and std::initializer_list).  While clang may claim it supports general
/// initializers, if we're using MSVC's headers, we might not have a usable
/// std::initializer list type from the STL.  Disable this for now.
#if __has_feature(cxx_generalized_initializers) && !defined(_MSC_VER)
#define LLVM_HAS_INITIALIZER_LISTS 1
#else
#define LLVM_HAS_INITIALIZER_LISTS 0
#endif

/// \brief Mark debug helper function definitions like dump() that should not be
/// stripped from debug builds.
// FIXME: Move this to a private config.h as it's not usable in public headers.
#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
#define LLVM_DUMP_METHOD LLVM_ATTRIBUTE_NOINLINE LLVM_ATTRIBUTE_USED
#else
#define LLVM_DUMP_METHOD LLVM_ATTRIBUTE_NOINLINE
#endif

/// \macro LLVM_THREAD_LOCAL
/// \brief A thread-local storage specifier which can be used with globals,
/// extern globals, and static globals.
///
/// This is essentially an extremely restricted analog to C++11's thread_local
/// support, and uses that when available. However, it falls back on
/// platform-specific or vendor-provided extensions when necessary. These
/// extensions don't support many of the C++11 thread_local's features. You
/// should only use this for PODs that you can statically initialize to
/// some constant value. In almost all circumstances this is most appropriate
/// for use with a pointer, integer, or small aggregation of pointers and
/// integers.
#if LLVM_ENABLE_THREADS
#if __has_feature(cxx_thread_local)
#define LLVM_THREAD_LOCAL thread_local
#elif defined(_MSC_VER)
// MSVC supports this with a __declspec.
#define LLVM_THREAD_LOCAL __declspec(thread)
#else
// Clang, GCC, and other compatible compilers used __thread prior to C++11 and
// we only need the restricted functionality that provides.
#define LLVM_THREAD_LOCAL __thread
#endif
#else // !LLVM_ENABLE_THREADS
// If threading is disabled entirely, this compiles to nothing and you get
// a normal global variable.
#define LLVM_THREAD_LOCAL
#endif

#endif
