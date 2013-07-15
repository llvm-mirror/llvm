# This CMake module is responsible for interpreting the user defined LLVM_
# options and executing the appropriate CMake commands to realize the users'
# selections.

include(AddLLVMDefinitions)
include(CheckCCompilerFlag)
include(CheckCXXCompilerFlag)

if( CMAKE_COMPILER_IS_GNUCXX )
  set(LLVM_COMPILER_IS_GCC_COMPATIBLE ON)
elseif( "${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang" )
  set(LLVM_COMPILER_IS_GCC_COMPATIBLE ON)
endif()

if( LLVM_ENABLE_ASSERTIONS )
  # MSVC doesn't like _DEBUG on release builds. See PR 4379.
  if( NOT MSVC )
    add_definitions( -D_DEBUG )
  endif()
  # On Release builds cmake automatically defines NDEBUG, so we
  # explicitly undefine it:
  if( uppercase_CMAKE_BUILD_TYPE STREQUAL "RELEASE" )
    add_definitions( -UNDEBUG )
    # Also remove /D NDEBUG to avoid MSVC warnings about conflicting defines.
    string (REGEX REPLACE "(^| )[/-]D *NDEBUG($| )" " "
      CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
  endif()
else()
  if( NOT uppercase_CMAKE_BUILD_TYPE STREQUAL "RELEASE" )
    if( NOT MSVC_IDE AND NOT XCODE )
      add_definitions( -DNDEBUG )
    endif()
  endif()
endif()

if(WIN32)
  if(CYGWIN)
    set(LLVM_ON_WIN32 0)
    set(LLVM_ON_UNIX 1)
  else(CYGWIN)
    set(LLVM_ON_WIN32 1)
    set(LLVM_ON_UNIX 0)
  endif(CYGWIN)
  set(LTDL_SHLIB_EXT ".dll")
  set(EXEEXT ".exe")
  # Maximum path length is 160 for non-unicode paths
  set(MAXPATHLEN 160)
else(WIN32)
  if(UNIX)
    set(LLVM_ON_WIN32 0)
    set(LLVM_ON_UNIX 1)
    if(APPLE)
      set(LTDL_SHLIB_EXT ".dylib")
    else(APPLE)
      set(LTDL_SHLIB_EXT ".so")
    endif(APPLE)
    set(EXEEXT "")
    # FIXME: Maximum path length is currently set to 'safe' fixed value
    set(MAXPATHLEN 2024)
  else(UNIX)
    MESSAGE(SEND_ERROR "Unable to determine platform")
  endif(UNIX)
endif(WIN32)

function(add_flag_or_print_warning flag)
  check_c_compiler_flag(${flag} C_SUPPORTS_FLAG)
  check_cxx_compiler_flag(${flag} CXX_SUPPORTS_FLAG)
  if (C_SUPPORTS_FLAG AND CXX_SUPPORTS_FLAG)
    message(STATUS "Building with ${flag}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${flag}" PARENT_SCOPE)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${flag}" PARENT_SCOPE)
  else()
    message(WARNING "${flag} is not supported.")
  endif()
endfunction()

function(append value)
  foreach(variable ${ARGN})
    set(${variable} "${${variable}} ${value}" PARENT_SCOPE)
  endforeach(variable)
endfunction()

function(append_if condition value)
  if (${condition})
    foreach(variable ${ARGN})
      set(${variable} "${${variable}} ${value}" PARENT_SCOPE)
    endforeach(variable)
  endif()
endfunction()

macro(add_flag_if_supported flag)
  check_c_compiler_flag(${flag} C_SUPPORTS_FLAG)
  append_if(C_SUPPORTS_FLAG "${flag}" CMAKE_C_FLAGS)
  check_cxx_compiler_flag(${flag} CXX_SUPPORTS_FLAG)
  append_if(CXX_SUPPORTS_FLAG "${flag}" CMAKE_CXX_FLAGS)
endmacro()

if( LLVM_ENABLE_PIC )
  if( XCODE )
    # Xcode has -mdynamic-no-pic on by default, which overrides -fPIC. I don't
    # know how to disable this, so just force ENABLE_PIC off for now.
    message(WARNING "-fPIC not supported with Xcode.")
  elseif( WIN32 OR CYGWIN)
    # On Windows all code is PIC. MinGW warns if -fPIC is used.
  else()
    add_flag_or_print_warning("-fPIC")

    if( WIN32 OR CYGWIN)
      # MinGW warns if -fvisibility-inlines-hidden is used.
    else()
      check_cxx_compiler_flag("-fvisibility-inlines-hidden" SUPPORTS_FVISIBILITY_INLINES_HIDDEN_FLAG)
      append_if(SUPPORTS_FVISIBILITY_INLINES_HIDDEN_FLAG "-fvisibility-inlines-hidden" CMAKE_CXX_FLAGS)
    endif()
  endif()
endif()

if( CMAKE_SIZEOF_VOID_P EQUAL 8 AND NOT WIN32 )
  # TODO: support other platforms and toolchains.
  if( LLVM_BUILD_32_BITS )
    message(STATUS "Building 32 bits executables and libraries.")
    add_llvm_definitions( -m32 )
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -m32")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -m32")
    set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} -m32")
  endif( LLVM_BUILD_32_BITS )
endif( CMAKE_SIZEOF_VOID_P EQUAL 8 AND NOT WIN32 )

# On Win32 using MS tools, provide an option to set the number of parallel jobs
# to use.
if( MSVC_IDE )
  set(LLVM_COMPILER_JOBS "0" CACHE STRING
    "Number of parallel compiler jobs. 0 means use all processors. Default is 0.")
  if( NOT LLVM_COMPILER_JOBS STREQUAL "1" )
    if( LLVM_COMPILER_JOBS STREQUAL "0" )
      add_llvm_definitions( /MP )
    else()
      if (MSVC10)
        message(FATAL_ERROR
          "Due to a bug in CMake only 0 and 1 is supported for "
          "LLVM_COMPILER_JOBS when generating for Visual Studio 2010")
      else()
        message(STATUS "Number of parallel compiler jobs set to " ${LLVM_COMPILER_JOBS})
        add_llvm_definitions( /MP${LLVM_COMPILER_JOBS} )
      endif()
    endif()
  else()
    message(STATUS "Parallel compilation disabled")
  endif()
endif()

if( MSVC )
  include(ChooseMSVCCRT)

  if( MSVC11 )
    add_llvm_definitions(-D_VARIADIC_MAX=10)
  endif()

  # Add definitions that make MSVC much less annoying.
  add_llvm_definitions(
    # For some reason MS wants to deprecate a bunch of standard functions...
    -D_CRT_SECURE_NO_DEPRECATE
    -D_CRT_SECURE_NO_WARNINGS
    -D_CRT_NONSTDC_NO_DEPRECATE
    -D_CRT_NONSTDC_NO_WARNINGS
    -D_SCL_SECURE_NO_DEPRECATE
    -D_SCL_SECURE_NO_WARNINGS

    # Disabled warnings.
    -wd4065 # Suppress 'switch statement contains 'default' but no 'case' labels'
    -wd4146 # Suppress 'unary minus operator applied to unsigned type, result still unsigned'
    -wd4180 # Suppress 'qualifier applied to function type has no meaning; ignored'
    -wd4181 # Suppress 'qualifier applied to reference type; ignored'
    -wd4224 # Suppress 'nonstandard extension used : formal parameter 'identifier' was previously defined as a type'
    -wd4244 # Suppress ''argument' : conversion from 'type1' to 'type2', possible loss of data'
    -wd4267 # Suppress ''var' : conversion from 'size_t' to 'type', possible loss of data'
    -wd4275 # Suppress 'An exported class was derived from a class that was not exported.'
    -wd4291 # Suppress ''declaration' : no matching operator delete found; memory will not be freed if initialization throws an exception'
    -wd4345 # Suppress 'behavior change: an object of POD type constructed with an initializer of the form () will be default-initialized'
    -wd4351 # Suppress 'new behavior: elements of array 'array' will be default initialized'
    -wd4355 # Suppress ''this' : used in base member initializer list'
    -wd4503 # Suppress ''identifier' : decorated name length exceeded, name was truncated'
    -wd4551 # Suppress 'function call missing argument list'
    -wd4624 # Suppress ''derived class' : destructor could not be generated because a base class destructor is inaccessible'
    -wd4715 # Suppress ''function' : not all control paths return a value'
    -wd4722 # Suppress ''function' : destructor never returns, potential memory leak'
    -wd4800 # Suppress ''type' : forcing value to bool 'true' or 'false' (performance warning)'

    # Promoted warnings.
    -w14062 # Promote 'enumerator in switch of enum is not handled' to level 1 warning.

    # Promoted warnings to errors.
    -we4238 # Promote 'nonstandard extension used : class rvalue used as lvalue' to error.
    )

  # Enable warnings
  if (LLVM_ENABLE_WARNINGS)
    add_llvm_definitions( /W4 )
    if (LLVM_ENABLE_PEDANTIC)
      # No MSVC equivalent available
    endif (LLVM_ENABLE_PEDANTIC)
  endif (LLVM_ENABLE_WARNINGS)
  if (LLVM_ENABLE_WERROR)
    add_llvm_definitions( /WX )
  endif (LLVM_ENABLE_WERROR)
elseif( LLVM_COMPILER_IS_GCC_COMPATIBLE )
  if (LLVM_ENABLE_WARNINGS)
    append("-Wall -W -Wno-unused-parameter -Wwrite-strings" CMAKE_C_FLAGS CMAKE_CXX_FLAGS)

    # Turn off missing field initializer warnings for gcc to avoid noise from
    # false positives with empty {}. Turn them on otherwise (they're off by
    # default for clang).
    check_cxx_compiler_flag("-Wmissing-field-initializers" CXX_SUPPORTS_MISSING_FIELD_INITIALIZERS_FLAG)
    if (CXX_SUPPORTS_MISSING_FIELD_INITIALIZERS_FLAG)
      if (CMAKE_COMPILER_IS_GNUCXX)
        append("-Wno-missing-field-initializers" CMAKE_C_FLAGS CMAKE_CXX_FLAGS)
      else()
        append("-Wmissing-field-initializers" CMAKE_C_FLAGS CMAKE_CXX_FLAGS)
      endif()
    endif()

    append_if(LLVM_ENABLE_PEDANTIC "-pedantic -Wno-long-long" CMAKE_C_FLAGS CMAKE_CXX_FLAGS)
    check_cxx_compiler_flag("-Werror -Wcovered-switch-default" CXX_SUPPORTS_COVERED_SWITCH_DEFAULT_FLAG)
    append_if(CXX_SUPPORTS_COVERED_SWITCH_DEFAULT_FLAG "-Wcovered-switch-default" CMAKE_CXX_FLAGS)
    check_c_compiler_flag("-Werror -Wcovered-switch-default" C_SUPPORTS_COVERED_SWITCH_DEFAULT_FLAG)
    append_if(C_SUPPORTS_COVERED_SWITCH_DEFAULT_FLAG "-Wcovered-switch-default" CMAKE_C_FLAGS)
    append_if(USE_NO_UNINITIALIZED "-Wno-uninitialized" CMAKE_CXX_FLAGS)
    append_if(USE_NO_MAYBE_UNINITIALIZED "-Wno-maybe-uninitialized" CMAKE_CXX_FLAGS)
    check_cxx_compiler_flag("-Werror -Wnon-virtual-dtor" CXX_SUPPORTS_NON_VIRTUAL_DTOR_FLAG)
    append_if(CXX_SUPPORTS_NON_VIRTUAL_DTOR_FLAG "-Wnon-virtual-dtor" CMAKE_CXX_FLAGS)
  endif (LLVM_ENABLE_WARNINGS)
  if (LLVM_ENABLE_WERROR)
    add_llvm_definitions( -Werror )
  endif (LLVM_ENABLE_WERROR)
endif( MSVC )

macro(append_common_sanitizer_flags)
  # Append -fno-omit-frame-pointer and turn on debug info to get better
  # stack traces.
  add_flag_if_supported("-fno-omit-frame-pointer")
  if (NOT uppercase_CMAKE_BUILD_TYPE STREQUAL "DEBUG" AND
      NOT uppercase_CMAKE_BUILD_TYPE STREQUAL "RELWITHDEBINFO")
    add_flag_if_supported("-gline-tables-only")
  endif()
endmacro()

# Turn on sanitizers if necessary.
if(LLVM_USE_SANITIZER)
  if (LLVM_ON_UNIX)
    if (LLVM_USE_SANITIZER STREQUAL "Address")
      append_common_sanitizer_flags()
      add_flag_or_print_warning("-fsanitize=address")
    elseif (LLVM_USE_SANITIZER MATCHES "Memory(WithOrigins)?")
      append_common_sanitizer_flags()
      add_flag_or_print_warning("-fsanitize=memory")
      if(LLVM_USE_SANITIZER STREQUAL "MemoryWithOrigins")
        add_flag_or_print_warning("-fsanitize-memory-track-origins")
      endif()
    else()
      message(WARNING "Unsupported value of LLVM_USE_SANITIZER: ${LLVM_USE_SANITIZER}")
    endif()
  else()
    message(WARNING "LLVM_USE_SANITIZER is not supported on this platform.")
  endif()
endif()

add_llvm_definitions( -D__STDC_CONSTANT_MACROS )
add_llvm_definitions( -D__STDC_FORMAT_MACROS )
add_llvm_definitions( -D__STDC_LIMIT_MACROS )

# clang doesn't print colored diagnostics when invoked from Ninja
if (UNIX AND
    CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND
    CMAKE_GENERATOR STREQUAL "Ninja")
  append("-fcolor-diagnostics" CMAKE_C_FLAGS CMAKE_CXX_FLAGS)
endif()
