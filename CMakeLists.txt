cmake_minimum_required(VERSION 2.8 FATAL_ERROR)

project(mapreduce)
# Find modules
set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake ${CMAKE_MODULE_PATH})
set(CMAKE_BUILD_TYPE Release CACHE STRING "Release" FORCE)

# Use, i.e. don't skip the full RPATH for the build tree
set(CMAKE_SKIP_BUILD_RPATH  FALSE)


set(LIB_SUFFIX CACHE STRING "suffix for the library directory need for x86-64 systems that use lib64 ")

# The RPATH to be used when installing
set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib${LIB_SUFFIX}")
# Add the automatically determined parts of the RPATH
# which point to directories outside the build tree to the install RPATH
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

include(CheckCXXCompilerFlag)
CHECK_CXX_COMPILER_FLAG("-std=c++11" COMPILER_SUPPORTS_CXX11)
CHECK_CXX_COMPILER_FLAG("-std=c++0x" COMPILER_SUPPORTS_CXX0X)
if(COMPILER_SUPPORTS_CXX11)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
elseif(COMPILER_SUPPORTS_CXX0X)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++0x")
else()
        message(STATUS "The compiler ${CMAKE_CXX_COMPILER} has no C++11 support. Please use a different C++ compiler.")
endif()

find_package(Boost COMPONENTS exception filesystem system iostreams)
include_directories(${Boost_INCLUDE_DIRS})
link_directories(${Boost_LIBRARY_DIRS})

include_directories(${PROJECT_SOURCE_DIR}/include)

export(PACKAGE MAPREDUCE)

# Create the executables
add_executable (friends examples/friends/friends.cpp)
target_link_libraries(friends ${Boost_EXCEPTION_LIBRARY} ${Boost_FILESYSTEM_LIBRARY} ${Boost_SYSTEM_LIBRARY} ${Boost_IOSTREAMS_LIBRARY})

# Install the Header files preserving the directory
set(HEADER_FILES include/mapreduce.hpp
  include/detail/datasource.hpp
  include/detail/hash_partitioner.hpp
  include/detail/intermediates/in_memory.hpp
  include/detail/intermediates/local_disk.hpp
  include/detail/intermediates.hpp
  include/detail/job.hpp
  include/detail/mergesort.hpp
  include/detail/null_combiner.hpp
  include/detail/platform.hpp
  include/detail/schedule_policy/cpu_parallel.hpp
  include/detail/schedule_policy/sequential.hpp
  include/detail/schedule_policy.hpp
)

FOREACH(HEADER ${HEADER_FILES})
	STRING(REGEX MATCH "(.*)[/]" DIR ${HEADER})
	INSTALL(FILES ${HEADER} DESTINATION ${DIR})
ENDFOREACH(HEADER)

# Install MAPREDUCE cmake config file for FIND_PACKAGE()
configure_file (${PROJECT_SOURCE_DIR}/MAPREDUCEConfig.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/MAPREDUCEConfig.cmake @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/MAPREDUCEConfig.cmake DESTINATION share/mapreduce)


