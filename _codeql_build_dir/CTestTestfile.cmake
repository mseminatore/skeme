# CMake generated Testfile for 
# Source directory: /home/runner/work/skeme/skeme
# Build directory: /home/runner/work/skeme/skeme/_codeql_build_dir
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(skeme_test "/home/runner/work/skeme/skeme/_codeql_build_dir/skeme" "-v" "test.scm")
set_tests_properties(skeme_test PROPERTIES  WORKING_DIRECTORY "/home/runner/work/skeme/skeme" _BACKTRACE_TRIPLES "/home/runner/work/skeme/skeme/CMakeLists.txt;11;add_test;/home/runner/work/skeme/skeme/CMakeLists.txt;0;")
subdirs("hash")
