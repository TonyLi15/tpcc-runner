# CMAKE generated file: DO NOT EDIT!
# Generated by "Unix Makefiles" Generator, CMake Version 3.16

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
CMAKE_SOURCE_DIR = /home/tonyli_15/tpcc-runner

# The top-level build directory on which CMake was run.
CMAKE_BINARY_DIR = /home/tonyli_15/tpcc-runner/build

# Include any dependencies generated for this target.
include _deps/build/mimalloc/CMakeFiles/mimalloc-test-stress.dir/depend.make

# Include the progress variables for this target.
include _deps/build/mimalloc/CMakeFiles/mimalloc-test-stress.dir/progress.make

# Include the compile flags for this target's objects.
include _deps/build/mimalloc/CMakeFiles/mimalloc-test-stress.dir/flags.make

_deps/build/mimalloc/CMakeFiles/mimalloc-test-stress.dir/test/test-stress.c.o: _deps/build/mimalloc/CMakeFiles/mimalloc-test-stress.dir/flags.make
_deps/build/mimalloc/CMakeFiles/mimalloc-test-stress.dir/test/test-stress.c.o: _deps/src/mimalloc/test/test-stress.c
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --progress-dir=/home/tonyli_15/tpcc-runner/build/CMakeFiles --progress-num=$(CMAKE_PROGRESS_1) "Building C object _deps/build/mimalloc/CMakeFiles/mimalloc-test-stress.dir/test/test-stress.c.o"
	cd /home/tonyli_15/tpcc-runner/build/_deps/build/mimalloc && /usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -o CMakeFiles/mimalloc-test-stress.dir/test/test-stress.c.o   -c /home/tonyli_15/tpcc-runner/build/_deps/src/mimalloc/test/test-stress.c

_deps/build/mimalloc/CMakeFiles/mimalloc-test-stress.dir/test/test-stress.c.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing C source to CMakeFiles/mimalloc-test-stress.dir/test/test-stress.c.i"
	cd /home/tonyli_15/tpcc-runner/build/_deps/build/mimalloc && /usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -E /home/tonyli_15/tpcc-runner/build/_deps/src/mimalloc/test/test-stress.c > CMakeFiles/mimalloc-test-stress.dir/test/test-stress.c.i

_deps/build/mimalloc/CMakeFiles/mimalloc-test-stress.dir/test/test-stress.c.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling C source to assembly CMakeFiles/mimalloc-test-stress.dir/test/test-stress.c.s"
	cd /home/tonyli_15/tpcc-runner/build/_deps/build/mimalloc && /usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -S /home/tonyli_15/tpcc-runner/build/_deps/src/mimalloc/test/test-stress.c -o CMakeFiles/mimalloc-test-stress.dir/test/test-stress.c.s

# Object files for target mimalloc-test-stress
mimalloc__test__stress_OBJECTS = \
"CMakeFiles/mimalloc-test-stress.dir/test/test-stress.c.o"

# External object files for target mimalloc-test-stress
mimalloc__test__stress_EXTERNAL_OBJECTS =

_deps/build/mimalloc/mimalloc-test-stress: _deps/build/mimalloc/CMakeFiles/mimalloc-test-stress.dir/test/test-stress.c.o
_deps/build/mimalloc/mimalloc-test-stress: _deps/build/mimalloc/CMakeFiles/mimalloc-test-stress.dir/build.make
_deps/build/mimalloc/mimalloc-test-stress: _deps/build/mimalloc/libmimalloc.so.1.7
_deps/build/mimalloc/mimalloc-test-stress: /usr/lib/x86_64-linux-gnu/librt.so
_deps/build/mimalloc/mimalloc-test-stress: _deps/build/mimalloc/CMakeFiles/mimalloc-test-stress.dir/link.txt
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --bold --progress-dir=/home/tonyli_15/tpcc-runner/build/CMakeFiles --progress-num=$(CMAKE_PROGRESS_2) "Linking C executable mimalloc-test-stress"
	cd /home/tonyli_15/tpcc-runner/build/_deps/build/mimalloc && $(CMAKE_COMMAND) -E cmake_link_script CMakeFiles/mimalloc-test-stress.dir/link.txt --verbose=$(VERBOSE)

# Rule to build all files generated by this target.
_deps/build/mimalloc/CMakeFiles/mimalloc-test-stress.dir/build: _deps/build/mimalloc/mimalloc-test-stress

.PHONY : _deps/build/mimalloc/CMakeFiles/mimalloc-test-stress.dir/build

_deps/build/mimalloc/CMakeFiles/mimalloc-test-stress.dir/clean:
	cd /home/tonyli_15/tpcc-runner/build/_deps/build/mimalloc && $(CMAKE_COMMAND) -P CMakeFiles/mimalloc-test-stress.dir/cmake_clean.cmake
.PHONY : _deps/build/mimalloc/CMakeFiles/mimalloc-test-stress.dir/clean

_deps/build/mimalloc/CMakeFiles/mimalloc-test-stress.dir/depend:
	cd /home/tonyli_15/tpcc-runner/build && $(CMAKE_COMMAND) -E cmake_depends "Unix Makefiles" /home/tonyli_15/tpcc-runner /home/tonyli_15/tpcc-runner/build/_deps/src/mimalloc /home/tonyli_15/tpcc-runner/build /home/tonyli_15/tpcc-runner/build/_deps/build/mimalloc /home/tonyli_15/tpcc-runner/build/_deps/build/mimalloc/CMakeFiles/mimalloc-test-stress.dir/DependInfo.cmake --color=$(COLOR)
.PHONY : _deps/build/mimalloc/CMakeFiles/mimalloc-test-stress.dir/depend
