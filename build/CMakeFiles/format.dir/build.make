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

# Utility rule file for format.

# Include the progress variables for this target.
include CMakeFiles/format.dir/progress.make

CMakeFiles/format:
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --blue --bold --progress-dir=/home/tonyli_15/tpcc-runner/build/CMakeFiles --progress-num=$(CMAKE_PROGRESS_1) "Running: format"
	../build_support/run-clang-format.py --clang-format-executable /usr/bin/clang-format -r /home/tonyli_15/tpcc-runner/benchmarks /home/tonyli_15/tpcc-runner/test /home/tonyli_15/tpcc-runner/utils /home/tonyli_15/tpcc-runner/executables /home/tonyli_15/tpcc-runner/protocols /home/tonyli_15/tpcc-runner/indexes -i -q

format: CMakeFiles/format
format: CMakeFiles/format.dir/build.make

.PHONY : format

# Rule to build all files generated by this target.
CMakeFiles/format.dir/build: format

.PHONY : CMakeFiles/format.dir/build

CMakeFiles/format.dir/clean:
	$(CMAKE_COMMAND) -P CMakeFiles/format.dir/cmake_clean.cmake
.PHONY : CMakeFiles/format.dir/clean

CMakeFiles/format.dir/depend:
	cd /home/tonyli_15/tpcc-runner/build && $(CMAKE_COMMAND) -E cmake_depends "Unix Makefiles" /home/tonyli_15/tpcc-runner /home/tonyli_15/tpcc-runner /home/tonyli_15/tpcc-runner/build /home/tonyli_15/tpcc-runner/build /home/tonyli_15/tpcc-runner/build/CMakeFiles/format.dir/DependInfo.cmake --color=$(COLOR)
.PHONY : CMakeFiles/format.dir/depend
