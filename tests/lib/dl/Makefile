# tests/lib/dl — dynamic-linker tests.
#
# torture_cxxdso is substrate-target only: it asserts properties of the
# shared C++ runtime and of ld.so's dlopen path, so there is no meaningful
# host build (the host loader would simply pass).
#
#     make CROSS=/opt/substrate/bin/i386-unknown-substrate-
#
# Install BOTH artifacts on the target.  The plugin has to land in a default
# search directory -- ld.so implements neither DT_RPATH nor DT_RUNPATH, so a
# -Wl,-rpath on the executable is silently ignored at runtime:
#
#     cp libtorture_plugin.so /lib/
#     cp torture_cxxdso /root/ && /root/torture_cxxdso

CROSS  ?=
CXX     = $(CROSS)g++
CXXFLAGS = -Wall -Wextra -O2 -g

# Match the userland baseline so atomics inline rather than expanding to
# libcalls substrate's libgcc.a does not provide.
ifneq ($(CROSS),)
  CXXFLAGS += -march=i486 -mtune=i486
endif

# -l:libdl.so.0 rather than -ldl: libdl.a resolves its weak __ldso_dlopen
# reference to 0 at static-link time, so dlopen() silently returns NULL with
# no dlerror and never enters ld.so at all.  The SHARED libdl keeps the weak
# reference dynamic, for ld.so to bind against its own exports at runtime.
LDLIBS  = -l:libdl.so.0 -lpthread

all: torture_cxxdso libtorture_plugin.so

torture_cxxdso: torture_cxxdso.cpp
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDLIBS)

libtorture_plugin.so: torture_plugin.cpp
	$(CXX) $(CXXFLAGS) -shared -fPIC -o $@ $< -lpthread

# Both must link the SHARED libstdc++.  If the libstdc++.so link name is
# missing from the sysroot ld falls through to libstdc++.a without a word,
# and the test would then pass for the wrong reason -- every module carrying
# its own runtime copy is precisely what this is here to catch.
check-shared: all
	@for f in torture_cxxdso libtorture_plugin.so; do \
	    if $(CROSS)readelf -dW $$f | grep -q 'NEEDED.*libstdc++\.so'; then \
	        echo "ok       $$f links shared libstdc++"; \
	    else \
	        echo "FAIL     $$f linked libstdc++ STATICALLY"; \
	        echo "         (missing $(dir)/libstdc++.so link name?"; \
	        echo "          run contrib/gcc/install-specs.sh)"; \
	        exit 1; \
	    fi; \
	done

clean:
	rm -f torture_cxxdso libtorture_plugin.so

.PHONY: all clean check-shared
