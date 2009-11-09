# This will not work as a stand-alone make file - it must be
# called recursively from the make file in the directory above.

TARGET=plosc.$(SO)
PLLDFLAGS=-I$(LIBLO)/include -L$(LIBLO)/lib -llo -fPIC -Wall

.SUFFIXES: .c .o .so .dylib

main: $(TARGET)

clean:
	rm $(TARGET)

.c.so: 
	$(PLLD) -v $(PLLDFLAGS) -shared -o $@ $<
	strip -x $@

.c.dylib: 
	$(PLLD) -v $(PLLDFLAGS) -shared -o $@ $<
	strip -x $@

install:
	install $(INSTALL_FLAGS) $(TARGET) $(INSTALL_LIB_TO)

