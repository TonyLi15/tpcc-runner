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
include CMakeFiles/ycsb4_caracal.dir/depend.make

# Include the progress variables for this target.
include CMakeFiles/ycsb4_caracal.dir/progress.make

# Include the compile flags for this target's objects.
include CMakeFiles/ycsb4_caracal.dir/flags.make

CMakeFiles/ycsb4_caracal.dir/executables/ycsb_caracal.cpp.o: CMakeFiles/ycsb4_caracal.dir/flags.make
CMakeFiles/ycsb4_caracal.dir/executables/ycsb_caracal.cpp.o: ../executables/ycsb_caracal.cpp
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --progress-dir=/home/tonyli_15/tpcc-runner/build/CMakeFiles --progress-num=$(CMAKE_PROGRESS_1) "Building CXX object CMakeFiles/ycsb4_caracal.dir/executables/ycsb_caracal.cpp.o"
	/usr/bin/c++  $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -o CMakeFiles/ycsb4_caracal.dir/executables/ycsb_caracal.cpp.o -c /home/tonyli_15/tpcc-runner/executables/ycsb_caracal.cpp

CMakeFiles/ycsb4_caracal.dir/executables/ycsb_caracal.cpp.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing CXX source to CMakeFiles/ycsb4_caracal.dir/executables/ycsb_caracal.cpp.i"
	/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -E /home/tonyli_15/tpcc-runner/executables/ycsb_caracal.cpp > CMakeFiles/ycsb4_caracal.dir/executables/ycsb_caracal.cpp.i

CMakeFiles/ycsb4_caracal.dir/executables/ycsb_caracal.cpp.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling CXX source to assembly CMakeFiles/ycsb4_caracal.dir/executables/ycsb_caracal.cpp.s"
	/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -S /home/tonyli_15/tpcc-runner/executables/ycsb_caracal.cpp -o CMakeFiles/ycsb4_caracal.dir/executables/ycsb_caracal.cpp.s

# Object files for target ycsb4_caracal
ycsb4_caracal_OBJECTS = \
"CMakeFiles/ycsb4_caracal.dir/executables/ycsb_caracal.cpp.o"

# External object files for target ycsb4_caracal
ycsb4_caracal_EXTERNAL_OBJECTS =

bin/ycsb4_caracal: CMakeFiles/ycsb4_caracal.dir/executables/ycsb_caracal.cpp.o
bin/ycsb4_caracal: CMakeFiles/ycsb4_caracal.dir/build.make
bin/ycsb4_caracal: lib/libtpccrunner_static.a
bin/ycsb4_caracal: _deps/build/mimalloc/libmimalloc-debug.so.1.7
bin/ycsb4_caracal: /usr/lib/x86_64-linux-gnu/librt.so
bin/ycsb4_caracal: _deps/build/masstree/libmasstree.a
bin/ycsb4_caracal: CMakeFiles/ycsb4_caracal.dir/link.txt
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --bold --progress-dir=/home/tonyli_15/tpcc-runner/build/CMakeFiles --progress-num=$(CMAKE_PROGRESS_2) "Linking CXX executable bin/ycsb4_caracal"
	$(CMAKE_COMMAND) -E cmake_link_script CMakeFiles/ycsb4_caracal.dir/link.txt --verbose=$(VERBOSE)

# Rule to build all files generated by this target.
CMakeFiles/ycsb4_caracal.dir/build: bin/ycsb4_caracal

.PHONY : CMakeFiles/ycsb4_caracal.dir/build

CMakeFiles/ycsb4_caracal.dir/clean:
	$(CMAKE_COMMAND) -P CMakeFiles/ycsb4_caracal.dir/cmake_clean.cmake
.PHONY : CMakeFiles/ycsb4_caracal.dir/clean

CMakeFiles/ycsb4_caracal.dir/depend:
	cd /home/tonyli_15/tpcc-runner/build && $(CMAKE_COMMAND) -E cmake_depends "Unix Makefiles" /home/tonyli_15/tpcc-runner /home/tonyli_15/tpcc-runner /home/tonyli_15/tpcc-runner/build /home/tonyli_15/tpcc-runner/build /home/tonyli_15/tpcc-runner/build/CMakeFiles/ycsb4_caracal.dir/DependInfo.cmake --color=$(COLOR)
.PHONY : CMakeFiles/ycsb4_caracal.dir/depend

