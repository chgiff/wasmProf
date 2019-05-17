BINARYEN_ROOT=ext/binaryen
BINARYEN_INC=$(BINARYEN_ROOT)/src
BINARYEN_LIB=$(BINARYEN_ROOT)/lib

tests_wasm := $(wildcard tests/*/*.wasm) 
tests_wat := $(tests_wasm:.wasm=.wat)

#executables
WASM_PROF=./wasmProf
WASABI=wasabi

.PHONY: all test $(tests_wasm)

all: binaryen
	g++ -I$(BINARYEN_INC) -g -std=c++11 -o wasmProf src/*.cpp -rdynamic $(BINARYEN_LIB)/libwasm.a $(BINARYEN_LIB)/libasmjs.a $(BINARYEN_LIB)/libemscripten-optimizer.a $(BINARYEN_LIB)/libpasses.a $(BINARYEN_LIB)/libir.a $(BINARYEN_LIB)/libcfg.a $(BINARYEN_LIB)/libsupport.a $(BINARYEN_LIB)/libwasm.a -pthread

binaryen:
	cd ext/binaryen && $(MAKE)

setup:
	cd ext/binaryen && cmake -DCMAKE_BUILD_TYPE=Debug .

test: $(tests_wasm)

$(tests_wasm): 
	@echo "\n\n\033[0;31mRunning test " $@ "\033[0m\n"
	@#echo "Baseline:"
	@#node tests/wasm_loader.js $@
	@#echo "\nWasmProf:"
	@#$(WASM_PROF) -p $@ > /dev/null
	@#mkdir -p $(dir $@)wasmProf_out
	@#mv $(dir $@)prof_* $(dir $@)wasmProf_out/
	@#cat $(dir $@)wasmProf_out/prof_$(notdir $@).js > $(dir $@)wasmProf_out/prof_wasm_loader.js 
	@#cat tests/wasm_loader.js >> $(dir $@)wasmProf_out/prof_wasm_loader.js
	@#node $(dir $@)wasmProf_out/prof_wasm_loader.js $(dir $@)wasmProf_out/prof_$(notdir $@)
	@#echo "\nWasabi:"
	@#$(WASABI) --hooks=call,return $@ > /dev/null
	@#mkdir -p $(dir $@)wasabi_out
	@#mv out/* $(dir $@)wasabi_out/
	@#rm -r out
	@#echo 'const { PerformanceObserver, performance } = require("perf_hooks");\nconst Long = require("long");' > $(dir $@)wasabi_out/wasabi_wasm_loader.js 
	@#cat $(dir $@)wasabi_out/$(notdir $(basename $@)).wasabi.js >> $(dir $@)wasabi_out/wasabi_wasm_loader.js 
	@#cat tests/call_profile.js >> $(dir $@)wasabi_out/wasabi_wasm_loader.js
	@#cat tests/wasm_loader.js >> $(dir $@)wasabi_out/wasabi_wasm_loader.js
	@#node $(dir $@)wasabi_out/wasabi_wasm_loader.js $(dir $@)wasabi_out/$(notdir $@)
	
	@echo "var filename = \"$(notdir $@)\";" > $@.js
	@cat tests/wasm_loader.js >> $@.js
	./run_test.py --wasmProf $(WASM_PROF) --wasabi $(WASABI) --wasabi_js tests/call_profile.js --wasm_validate ../wabt/bin/wasm-validate $@ $@.js


clean:
	rm wasmProf