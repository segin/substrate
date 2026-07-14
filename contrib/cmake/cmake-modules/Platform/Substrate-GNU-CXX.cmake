include(Platform/Substrate-GNU)
__linux_compiler_gnu(CXX)

# Substrate's libstdc++.so has hard references to the pthread_* symbols
# (gthr-posix) but does not carry libpthread in its DT_NEEDED, so every C++
# link must supply -lpthread explicitly or ld reports the libstdc++ refs as
# undefined.  Append it to the always-linked standard libraries so both cross
# and native (on-VM) C++ builds resolve cleanly.  Harmless if a future
# libstdc++ carries the dependency itself (libpthread.so always exists).
string(APPEND CMAKE_CXX_STANDARD_LIBRARIES " -lpthread")
