# CMAKE generated file: DO NOT EDIT!
# Generated by "Unix Makefiles" Generator, CMake Version 3.14

# Delete rule output on recipe failure.
.DELETE_ON_ERROR:


#=============================================================================
# Special targets provided by cmake.

# Disable implicit rules so canonical targets will work.
.SUFFIXES:


# Remove some rules from gmake that .SUFFIXES does not remove.
SUFFIXES =

.SUFFIXES: .hpux_make_needs_suffix_list


# Suppress display of executed commands.
$(VERBOSE).SILENT:


# A target that is always out of date.
cmake_force:

.PHONY : cmake_force

#=============================================================================
# Set environment variables for the build.

# The shell in which to execute make rules.
SHELL = /bin/sh

# The CMake executable.
CMAKE_COMMAND = /usr/bin/cmake

# The command to remove a file.
RM = /usr/bin/cmake -E remove -f

# Escaping for special characters.
EQUALS = =

# The top-level source directory on which CMake was run.
CMAKE_SOURCE_DIR = /localdisk/rsanna/Montage/ext/Clevel-Hashing

# The top-level build directory on which CMake was run.
CMAKE_BINARY_DIR = /localdisk/rsanna/Montage/ext/Clevel-Hashing

# Include any dependencies generated for this target.
include tests/external/CMakeFiles/array_front_back.dir/depend.make

# Include the progress variables for this target.
include tests/external/CMakeFiles/array_front_back.dir/progress.make

# Include the compile flags for this target's objects.
include tests/external/CMakeFiles/array_front_back.dir/flags.make

tests/external/CMakeFiles/array_front_back.dir/libcxx/array/front_back.pass.cpp.o: tests/external/CMakeFiles/array_front_back.dir/flags.make
tests/external/CMakeFiles/array_front_back.dir/libcxx/array/front_back.pass.cpp.o: tests/external/libcxx/array/front_back.pass.cpp
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --progress-dir=/localdisk/rsanna/Montage/ext/Clevel-Hashing/CMakeFiles --progress-num=$(CMAKE_PROGRESS_1) "Building CXX object tests/external/CMakeFiles/array_front_back.dir/libcxx/array/front_back.pass.cpp.o"
	cd /localdisk/rsanna/Montage/ext/Clevel-Hashing/tests/external && /usr/bin/c++  $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -o CMakeFiles/array_front_back.dir/libcxx/array/front_back.pass.cpp.o -c /localdisk/rsanna/Montage/ext/Clevel-Hashing/tests/external/libcxx/array/front_back.pass.cpp

tests/external/CMakeFiles/array_front_back.dir/libcxx/array/front_back.pass.cpp.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing CXX source to CMakeFiles/array_front_back.dir/libcxx/array/front_back.pass.cpp.i"
	cd /localdisk/rsanna/Montage/ext/Clevel-Hashing/tests/external && /usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -E /localdisk/rsanna/Montage/ext/Clevel-Hashing/tests/external/libcxx/array/front_back.pass.cpp > CMakeFiles/array_front_back.dir/libcxx/array/front_back.pass.cpp.i

tests/external/CMakeFiles/array_front_back.dir/libcxx/array/front_back.pass.cpp.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling CXX source to assembly CMakeFiles/array_front_back.dir/libcxx/array/front_back.pass.cpp.s"
	cd /localdisk/rsanna/Montage/ext/Clevel-Hashing/tests/external && /usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -S /localdisk/rsanna/Montage/ext/Clevel-Hashing/tests/external/libcxx/array/front_back.pass.cpp -o CMakeFiles/array_front_back.dir/libcxx/array/front_back.pass.cpp.s

# Object files for target array_front_back
array_front_back_OBJECTS = \
"CMakeFiles/array_front_back.dir/libcxx/array/front_back.pass.cpp.o"

# External object files for target array_front_back
array_front_back_EXTERNAL_OBJECTS =

tests/external/array_front_back: tests/external/CMakeFiles/array_front_back.dir/libcxx/array/front_back.pass.cpp.o
tests/external/array_front_back: tests/external/CMakeFiles/array_front_back.dir/build.make
tests/external/array_front_back: /usr/lib64/libpmemobj.so
tests/external/array_front_back: /usr/lib64/libpmem.so
tests/external/array_front_back: tests/libtest_backtrace.a
tests/external/array_front_back: tests/libvalgrind_internal.a
tests/external/array_front_back: tests/external/CMakeFiles/array_front_back.dir/link.txt
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --bold --progress-dir=/localdisk/rsanna/Montage/ext/Clevel-Hashing/CMakeFiles --progress-num=$(CMAKE_PROGRESS_2) "Linking CXX executable array_front_back"
	cd /localdisk/rsanna/Montage/ext/Clevel-Hashing/tests/external && $(CMAKE_COMMAND) -E cmake_link_script CMakeFiles/array_front_back.dir/link.txt --verbose=$(VERBOSE)

# Rule to build all files generated by this target.
tests/external/CMakeFiles/array_front_back.dir/build: tests/external/array_front_back

.PHONY : tests/external/CMakeFiles/array_front_back.dir/build

tests/external/CMakeFiles/array_front_back.dir/clean:
	cd /localdisk/rsanna/Montage/ext/Clevel-Hashing/tests/external && $(CMAKE_COMMAND) -P CMakeFiles/array_front_back.dir/cmake_clean.cmake
.PHONY : tests/external/CMakeFiles/array_front_back.dir/clean

tests/external/CMakeFiles/array_front_back.dir/depend:
	cd /localdisk/rsanna/Montage/ext/Clevel-Hashing && $(CMAKE_COMMAND) -E cmake_depends "Unix Makefiles" /localdisk/rsanna/Montage/ext/Clevel-Hashing /localdisk/rsanna/Montage/ext/Clevel-Hashing/tests/external /localdisk/rsanna/Montage/ext/Clevel-Hashing /localdisk/rsanna/Montage/ext/Clevel-Hashing/tests/external /localdisk/rsanna/Montage/ext/Clevel-Hashing/tests/external/CMakeFiles/array_front_back.dir/DependInfo.cmake --color=$(COLOR)
.PHONY : tests/external/CMakeFiles/array_front_back.dir/depend

