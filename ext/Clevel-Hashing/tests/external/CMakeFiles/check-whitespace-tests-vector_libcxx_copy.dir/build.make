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

# Utility rule file for check-whitespace-tests-vector_libcxx_copy.

# Include the progress variables for this target.
include tests/external/CMakeFiles/check-whitespace-tests-vector_libcxx_copy.dir/progress.make

tests/external/CMakeFiles/check-whitespace-tests-vector_libcxx_copy: check-whitespace-tests-vector_libcxx_copy-status


check-whitespace-tests-vector_libcxx_copy-status: tests/external/libcxx/vector/vector.cons/copy.pass.cpp
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --blue --bold --progress-dir=/localdisk/rsanna/Montage/ext/Clevel-Hashing/CMakeFiles --progress-num=$(CMAKE_PROGRESS_1) "Generating ../../check-whitespace-tests-vector_libcxx_copy-status"
	cd /localdisk/rsanna/Montage/ext/Clevel-Hashing/tests/external && /usr/bin/perl /localdisk/rsanna/Montage/ext/Clevel-Hashing/utils/check_whitespace /localdisk/rsanna/Montage/ext/Clevel-Hashing/tests/external/libcxx/vector/vector.cons/copy.pass.cpp
	cd /localdisk/rsanna/Montage/ext/Clevel-Hashing/tests/external && /usr/bin/cmake -E touch /localdisk/rsanna/Montage/ext/Clevel-Hashing/check-whitespace-tests-vector_libcxx_copy-status

check-whitespace-tests-vector_libcxx_copy: tests/external/CMakeFiles/check-whitespace-tests-vector_libcxx_copy
check-whitespace-tests-vector_libcxx_copy: check-whitespace-tests-vector_libcxx_copy-status
check-whitespace-tests-vector_libcxx_copy: tests/external/CMakeFiles/check-whitespace-tests-vector_libcxx_copy.dir/build.make

.PHONY : check-whitespace-tests-vector_libcxx_copy

# Rule to build all files generated by this target.
tests/external/CMakeFiles/check-whitespace-tests-vector_libcxx_copy.dir/build: check-whitespace-tests-vector_libcxx_copy

.PHONY : tests/external/CMakeFiles/check-whitespace-tests-vector_libcxx_copy.dir/build

tests/external/CMakeFiles/check-whitespace-tests-vector_libcxx_copy.dir/clean:
	cd /localdisk/rsanna/Montage/ext/Clevel-Hashing/tests/external && $(CMAKE_COMMAND) -P CMakeFiles/check-whitespace-tests-vector_libcxx_copy.dir/cmake_clean.cmake
.PHONY : tests/external/CMakeFiles/check-whitespace-tests-vector_libcxx_copy.dir/clean

tests/external/CMakeFiles/check-whitespace-tests-vector_libcxx_copy.dir/depend:
	cd /localdisk/rsanna/Montage/ext/Clevel-Hashing && $(CMAKE_COMMAND) -E cmake_depends "Unix Makefiles" /localdisk/rsanna/Montage/ext/Clevel-Hashing /localdisk/rsanna/Montage/ext/Clevel-Hashing/tests/external /localdisk/rsanna/Montage/ext/Clevel-Hashing /localdisk/rsanna/Montage/ext/Clevel-Hashing/tests/external /localdisk/rsanna/Montage/ext/Clevel-Hashing/tests/external/CMakeFiles/check-whitespace-tests-vector_libcxx_copy.dir/DependInfo.cmake --color=$(COLOR)
.PHONY : tests/external/CMakeFiles/check-whitespace-tests-vector_libcxx_copy.dir/depend

