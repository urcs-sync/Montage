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
include examples/CMakeFiles/doc_snippets_persistent.dir/depend.make

# Include the progress variables for this target.
include examples/CMakeFiles/doc_snippets_persistent.dir/progress.make

# Include the compile flags for this target's objects.
include examples/CMakeFiles/doc_snippets_persistent.dir/flags.make

examples/CMakeFiles/doc_snippets_persistent.dir/doc_snippets/persistent.cpp.o: examples/CMakeFiles/doc_snippets_persistent.dir/flags.make
examples/CMakeFiles/doc_snippets_persistent.dir/doc_snippets/persistent.cpp.o: examples/doc_snippets/persistent.cpp
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --progress-dir=/localdisk/rsanna/Montage/ext/Clevel-Hashing/CMakeFiles --progress-num=$(CMAKE_PROGRESS_1) "Building CXX object examples/CMakeFiles/doc_snippets_persistent.dir/doc_snippets/persistent.cpp.o"
	cd /localdisk/rsanna/Montage/ext/Clevel-Hashing/examples && /usr/bin/c++  $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -o CMakeFiles/doc_snippets_persistent.dir/doc_snippets/persistent.cpp.o -c /localdisk/rsanna/Montage/ext/Clevel-Hashing/examples/doc_snippets/persistent.cpp

examples/CMakeFiles/doc_snippets_persistent.dir/doc_snippets/persistent.cpp.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing CXX source to CMakeFiles/doc_snippets_persistent.dir/doc_snippets/persistent.cpp.i"
	cd /localdisk/rsanna/Montage/ext/Clevel-Hashing/examples && /usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -E /localdisk/rsanna/Montage/ext/Clevel-Hashing/examples/doc_snippets/persistent.cpp > CMakeFiles/doc_snippets_persistent.dir/doc_snippets/persistent.cpp.i

examples/CMakeFiles/doc_snippets_persistent.dir/doc_snippets/persistent.cpp.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling CXX source to assembly CMakeFiles/doc_snippets_persistent.dir/doc_snippets/persistent.cpp.s"
	cd /localdisk/rsanna/Montage/ext/Clevel-Hashing/examples && /usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -S /localdisk/rsanna/Montage/ext/Clevel-Hashing/examples/doc_snippets/persistent.cpp -o CMakeFiles/doc_snippets_persistent.dir/doc_snippets/persistent.cpp.s

doc_snippets_persistent: examples/CMakeFiles/doc_snippets_persistent.dir/doc_snippets/persistent.cpp.o
doc_snippets_persistent: examples/CMakeFiles/doc_snippets_persistent.dir/build.make

.PHONY : doc_snippets_persistent

# Rule to build all files generated by this target.
examples/CMakeFiles/doc_snippets_persistent.dir/build: doc_snippets_persistent

.PHONY : examples/CMakeFiles/doc_snippets_persistent.dir/build

examples/CMakeFiles/doc_snippets_persistent.dir/clean:
	cd /localdisk/rsanna/Montage/ext/Clevel-Hashing/examples && $(CMAKE_COMMAND) -P CMakeFiles/doc_snippets_persistent.dir/cmake_clean.cmake
.PHONY : examples/CMakeFiles/doc_snippets_persistent.dir/clean

examples/CMakeFiles/doc_snippets_persistent.dir/depend:
	cd /localdisk/rsanna/Montage/ext/Clevel-Hashing && $(CMAKE_COMMAND) -E cmake_depends "Unix Makefiles" /localdisk/rsanna/Montage/ext/Clevel-Hashing /localdisk/rsanna/Montage/ext/Clevel-Hashing/examples /localdisk/rsanna/Montage/ext/Clevel-Hashing /localdisk/rsanna/Montage/ext/Clevel-Hashing/examples /localdisk/rsanna/Montage/ext/Clevel-Hashing/examples/CMakeFiles/doc_snippets_persistent.dir/DependInfo.cmake --color=$(COLOR)
.PHONY : examples/CMakeFiles/doc_snippets_persistent.dir/depend

