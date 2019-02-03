BINARYEN_ROOT=../binaryen
BINARYEN_INC=-I$(BINARYEN_ROOT)/src
BINARYEN_LIB=-L$(BINARYEN_ROOT)/lib

all:
	g++ $(BINARYEN_INC) -g -std=c++11 -o wasmProf src/wasmProf.cpp -rdynamic $(BINARYEN_ROOT)/lib/libwasm.a $(BINARYEN_ROOT)/lib/libasmjs.a $(BINARYEN_ROOT)/lib/libemscripten-optimizer.a $(BINARYEN_ROOT)/lib/libpasses.a $(BINARYEN_ROOT)/lib/libir.a $(BINARYEN_ROOT)/lib/libcfg.a $(BINARYEN_ROOT)/lib/libsupport.a $(BINARYEN_ROOT)/lib/libwasm.a -pthread

clean:
	rm wasmProf