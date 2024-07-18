# from here:
#
# https://github.com/lefticus/cppbestpractices/blob/master/02-Use_the_Tools_Available.md

function(
  set_project_warnings_full_args
  target_name

  # (boolean) Say if the warnings should be treated as errors
  WARNINGS_AS_ERRORS

  # (boolean) Say if the target is the test suite or if it is other target
  TARGET_IS_TESTS

  # Define some values to be filtered out (it may be empty)
  MSVC_FILTER_WARNINGS
  CLANG_FILTER_WARNINGS
  GCC_FILTER_WARNINGS
)

  # TODO: MSVC flags are untested
  set(MSVC_WARNINGS
      /W4 # Baseline reasonable warnings
      /w14242 # 'identifier': conversion from 'type1' to 'type2', possible loss of data
      /w14254 # 'operator': conversion from 'type1:field_bits' to 'type2:field_bits', possible loss of data
      /w14263 # 'function': member function does not override any base class virtual member function
      /w14265 # 'classname': class has virtual functions, but destructor is not virtual instances of this class may not
              # be destructed correctly
      /w14287 # 'operator': unsigned/negative constant mismatch
      /we4289 # nonstandard extension used: 'variable': loop control variable declared in the for-loop is used outside
              # the for-loop scope
      /w14296 # 'operator': expression is always 'boolean_value'
      /w14311 # 'variable': pointer truncation from 'type1' to 'type2'
      /w14545 # expression before comma evaluates to a function which is missing an argument list
      /w14546 # function call before comma missing argument list
      /w14547 # 'operator': operator before comma has no effect; expected operator with side-effect
      /w14549 # 'operator': operator before comma has no effect; did you intend 'operator'?
      /w14555 # expression has no effect; expected expression with side- effect
      /w14619 # pragma warning: there is no warning number 'number'
      /w14640 # Enable warning on thread un-safe static member initialization
      /w14826 # Conversion from 'type1' to 'type2' is sign-extended. This may cause unexpected runtime behavior.
      /w14905 # wide string literal cast to 'LPSTR'
      /w14906 # string literal cast to 'LPWSTR'
      /w14928 # illegal copy-initialization; more than one user-defined conversion has been implicitly applied
      /permissive- # standards conformance mode for MSVC compiler.
  )

  set(CLANG_WARNINGS
      -Wall
      -Wextra # reasonable and standard
      -Wmisleading-indentation # warn if indentation implies blocks where blocks do not exist
      -Wzero-as-null-pointer-constant # warn when a literal 0 is used as null pointer constant.
      -Wnon-virtual-dtor # warn the user if a class with virtual functions has a non-virtual destructor. This helps
      # catch hard to track down memory errors
      -Wunused # warn on anything being unused
      -Woverloaded-virtual # warn if you overload (not override) a virtual function
      -Wpedantic # warn if non-standard C++ is used
      -Wconversion # warn on type conversions that may lose data
      -Wnull-dereference # warn if a null dereference is detected
      -Wformat=2 # warn on security issues around functions that format output (ie printf)
      -Wimplicit-fallthrough # warn on statements that fallthrough without an explicit annotation
      -Wmismatched-tags # warn if for type, it is tagged as struct in one place and as class as another
  )

  # the following are extra warnings for the xoz lib and demos compilation;
  # for tests we don't want them
  if(NOT TARGET_IS_TESTS)
      list(APPEND CLANG_WARNINGS
            -Wold-style-cast # warn for c-style casts
            -Wcast-align # warn for potential performance problem casts
            -Wdouble-promotion # warn if float is implicit promoted to double
            -Wsign-conversion # warn on sign conversions
            -Wno-unused-command-line-argument
      )
  else()
      list(APPEND CLANG_WARNINGS
            -Wno-sign-conversion
      )
  endif()

  set(GCC_WARNINGS
      ${CLANG_WARNINGS}
      -Wduplicated-cond # warn if if / else chain has duplicated conditions
      -Wduplicated-branches # warn if if / else branches have duplicated code
      -Wlogical-op # warn about logical operations being used where bitwise were probably wanted
      -Wsuggest-override # warn if an overridden member function is not marked 'override' or 'final'
  )

  if(WARNINGS_AS_ERRORS)
    message(TRACE "Warnings are treated as errors")
    list(APPEND CLANG_WARNINGS -Werror)
    list(APPEND GCC_WARNINGS -Werror)
    list(APPEND MSVC_WARNINGS /WX)
  endif()

  # Allow the caller to remove any flag set here
  list(REMOVE_ITEM CLANG_WARNINGS CLANG_FILTER_WARNINGS)
  list(REMOVE_ITEM GCC_WARNINGS GCC_FILTER_WARNINGS)
  list(REMOVE_ITEM MSVC_WARNINGS MSVC_FILTER_WARNINGS)

  if(MSVC)
    set(PROJECT_WARNINGS_CXX ${MSVC_WARNINGS})
  elseif(CMAKE_CXX_COMPILER_ID MATCHES ".*Clang")
    set(PROJECT_WARNINGS_CXX ${CLANG_WARNINGS})
  elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(PROJECT_WARNINGS_CXX ${GCC_WARNINGS})
  else()
    message(AUTHOR_WARNING "No compiler warnings set for CXX compiler: '${CMAKE_CXX_COMPILER_ID}'")
  endif()

  target_compile_options(
    ${target_name}
    PRIVATE
        ${PROJECT_WARNINGS_CXX}
  )
endfunction()

function(
  set_project_warnings
  target_name

  # (boolean) Say if the warnings should be treated as errors
  WARNINGS_AS_ERRORS

  # (boolean) Say if the target is the test suite or if it is other target
  TARGET_IS_TESTS
)
  set_project_warnings_full_args(${target_name}
      ${WARNINGS_AS_ERRORS}
      ${TARGET_IS_TESTS}

      # Filter out warning lists
      "" "" ""
  )
endfunction()
