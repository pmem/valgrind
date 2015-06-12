Valgrind: an enhanced version for pmem
======================================

[![Build Status](https://travis-ci.org/pmem/valgrind.svg)](https://travis-ci.org/pmem/valgrind)

This is the top-level README.md the enhanced version on Valgrind.
This version has support for the new CLFLUSHOPT, PCOMMIT and CLWB
instructions. It also introduces a new tool called pmemcheck which
validates the correctness of stores made to persistent memory. Be aware
that this is still a prototype tool.

Please see the file COPYING for information on the license.

The layout is identical to the original Valgrind.
The new tool is available in:

* **pmemcheck** -- the new persistent memory aware tool

All packages necessary to build this modified version of Valgrind are
the same as for the original version.

Once the build system is setup, Valgrind is built using
these command at the top level:
```
	$ ./autogen.sh
	$ ./configure [--prefix=/where/to/install]
	$ make
```

To build tests:
```
	$ make check
```

To run all regression tests:
```
	$ make regtest
```

To run pmemcheck tests only:
```
	$ perl tests/vg_regtest pmemcheck
```

To install Valgrind run (possibly as root if destination permissions
require that):
```
	$ make install
```

For more information on Valgrind please refer to the original README
files and the documentation which is available at:
```
	$PREFIX/share/doc/valgrind/manual.html
```
Where $PREFIX is the path specified with --prefix to configure.

For information on how to run the new tool refer to the appropriate
part of the documentation or type:
```
	$ valgrind --tool=pmemcheck --help
```

For more information on the modifications made to Valgrind
contact Andy Rudoff (andy.rudoff@intel.com),
Tomasz Kapela (tomasz.kapela@intel.com) or
Krzysztof Czurylo (krzysztof.czurylo@intel.com).
