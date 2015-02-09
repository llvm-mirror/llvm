include(AddFileDependencies)
include(CMakeParseArguments)

function(llvm_replace_compiler_option var old new)
  # Replaces a compiler option or switch `old' in `var' by `new'.
  # If `old' is not in `var', appends `new' to `var'.
  # Example: llvm_replace_compiler_option(CMAKE_CXX_FLAGS_RELEASE "-O3" "-O2")
  # If the option already is on the variable, don't add it:
  if( "${${var}}" MATCHES "(^| )${new}($| )" )
    set(n "")
  else()
    set(n "${new}")
  endif()
  if( "${${var}}" MATCHES "(^| )${old}($| )" )
    string( REGEX REPLACE "(^| )${old}($| )" " ${n} " ${var} "${${var}}" )
  else()
    set( ${var} "${${var}} ${n}" )
  endif()
  set( ${var} "${${var}}" PARENT_SCOPE )
endfunction(llvm_replace_compiler_option)

macro(add_td_sources srcs)
  file(GLOB tds *.td)
  if( tds )
    source_group("TableGen descriptions" FILES ${tds})
    set_source_files_properties(${tds} PROPERTIES HEADER_FILE_ONLY ON)
    list(APPEND ${srcs} ${tds})
  endif()
endmacro(add_td_sources)


macro(add_header_files srcs)
  file(GLOB hds *.h)
  if( hds )
    set_source_files_properties(${hds} PROPERTIES HEADER_FILE_ONLY ON)
    list(APPEND ${srcs} ${hds})
  endif()
endmacro(add_header_files)


function(llvm_process_sources OUT_VAR)
  cmake_parse_arguments(ARG "" "" "ADDITIONAL_HEADERS" ${ARGN})
  set(sources ${ARG_UNPARSED_ARGUMENTS})
  llvm_check_source_file_list( ${sources} )
  if( MSVC_IDE OR XCODE )
    # This adds .td and .h files to the Visual Studio solution:
    add_td_sources(sources)
    add_header_files(sources)
    set_source_files_properties(${ARG_ADDITIONAL_HEADERS} PROPERTIES HEADER_FILE_ONLY ON)
    list(APPEND sources ${ARG_ADDITIONAL_HEADERS})
  endif()

  set( ${OUT_VAR} ${sources} PARENT_SCOPE )
endfunction(llvm_process_sources)


function(llvm_check_source_file_list)
  set(listed ${ARGN})
  file(GLOB globbed *.c *.cpp)
  foreach(g ${globbed})
    get_filename_component(fn ${g} NAME)

    # Don't reject hidden files. Some editors create backups in the
    # same directory as the file.
    if (NOT "${fn}" MATCHES "^\\.")
      list(FIND LLVM_OPTIONAL_SOURCES ${fn} idx)
      if( idx LESS 0 )
        list(FIND listed ${fn} idx)
        if( idx LESS 0 )
          message(SEND_ERROR "Found unknown source file ${g}
Please update ${CMAKE_CURRENT_LIST_FILE}\n")
        endif()
      endif()
    endif()
  endforeach()
endfunction(llvm_check_source_file_list)
