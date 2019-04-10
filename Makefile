BINARYEN_ROOT=ext/binaryen
BINARYEN_INC=$(BINARYEN_ROOT)/src
BINARYEN_LIB=$(BINARYEN_ROOT)/lib

all: binaryen
	g++ -I$(BINARYEN_INC) -g -std=c++11 -o wasmProf src/*.cpp -rdynamic $(BINARYEN_LIB)/libwasm.a $(BINARYEN_LIB)/libasmjs.a $(BINARYEN_LIB)/libemscripten-optimizer.a $(BINARYEN_LIB)/libpasses.a $(BINARYEN_LIB)/libir.a $(BINARYEN_LIB)/libcfg.a $(BINARYEN_LIB)/libsupport.a $(BINARYEN_LIB)/libwasm.a -pthread

binaryen:
	cd ext/binaryen && $(MAKE)

setup:
	cd ext/binaryen && cmake -DCMAKE_BUILD_TYPE=Debug .
	
clean:
	rm wasmProf