#===------------------------------------------------------------------------===#
#
#                     The KLEE Symbolic Virtual Machine
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
#
#===------------------------------------------------------------------------===#
option(ENABLE_SOLVER_BITWUZLA "Enable Bitwuzla solver support" OFF)

if (ENABLE_SOLVER_BITWUZLA)
  message(STATUS "Bitwuzla solver support enabled")
  find_package(Bitwuzla)
  if (Bitwuzla_FOUND)
    message(STATUS "Found Bitwuzla")
    set(ENABLE_BITWUZLA 1) # For config.h
    list(APPEND KLEE_COMPONENT_EXTRA_INCLUDE_DIRS ${Bitwuzla_INCLUDE_DIRS})
    list(APPEND KLEE_SOLVER_INCLUDE_DIRS ${Bitwuzla_INCLUDE_DIRS})
    list(APPEND KLEE_SOLVER_LIBRARIES ${Bitwuzla_LIBRARIES})
  else()
    message(FATAL_ERROR "Bitwuzla not found.")
  endif()
else()
  message(STATUS "Bitwuzla solver support disabled")
  set(ENABLE_BITWUZLA 0) # For config.h
endif()
