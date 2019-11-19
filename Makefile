#Makefile for the OVF importer

LIBS=-lWSTP64i4 -lm -lpthread -lrt -lstdc++ -ldl -luuid -lboost_system -lboost_filesystem
EXTRA_FLAGS=-m64
CXX = g++
CXX_FLAGS = -O2


importOVF: importOVF.o importOVFtm.o OVFReader.o
	g++ ${EXTRA_FLAGS} importOVFtm.o importOVF.o  OVFReader.o ${LIBS} -o $@
	
.cpp.o:
	$(CXX) -c $(CXX_FLAGS) $<

.c.o:
	cc -c ${EXTRA_FLAGS} $<
	
importOVFtm.c:
	wsprep importOVF.tm -o importOVFtm.c

clean:
	rm -f *.o
	rm -f importOVFtm.c
	rm -f importOVF
	rm -f OVFReader.o
