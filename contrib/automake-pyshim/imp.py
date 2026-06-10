# Shim for the `imp` module that automake's py-compile (bundled in 2018-era
# GNOME tarballs: glib 2.56, pango 1.42, atk 2.28) imports during its
# install-*PYTHON byte-compile step.  Python 3.12 removed `imp`; forward the
# handful of names automake uses to their importlib equivalents.
# Put this dir on PYTHONPATH for `make install`.
import sys
import importlib.util as _u

def get_tag():
    return sys.implementation.cache_tag

def cache_from_source(path, debug_override=None):
    return _u.cache_from_source(path, debug_override)

def source_from_cache(path):
    return _u.source_from_cache(path)

PY_SOURCE = 1
PY_COMPILED = 2
C_EXTENSION = 3
