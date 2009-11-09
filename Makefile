# ---------------- configuration ----------------------

# set this to the prefix directory of your liblo installation
export LIBLO=/usr/local

# target is plml.dylib for OSX, plml.so under Linux
export SO=dylib

# if you have multiple SWI Prolog installations or an installation
# in a non-standard place, set PLLD to the appropriate plld invokation, eg
# PLLD=/usr/local/bin/plld -p /usr/local/bin/swipl
export PLLD=plld

# install directories
export INSTALL_LIB_TO=~/lib/prolog
export INSTALL_PL_TO=~/lib/prolog

# flags for install - BSD install seems to be different from GNU install
export INSTALL_FLAGS='-bCS'

VER=0.2
# ---------------- end of configuration ---------------

main: 
	make -C cpp

clean:
	make -C cpp clean

install: main
	make -C cpp install
	make -C prolog install

install-bin: main
	make -C cpp install

install-pl:
	make -C prolog install

tarball:
	mkdirhier release
	(cd .. && tar czf plosc/release/plosc-$(VER).tar.gz --exclude .DS_Store --exclude .swiple_history --exclude CVS --exclude "*.gz" --exclude release plosc)
