# WasmProf
WebAssembly Instrumenting Profiler


# Usage

## Instrumenting WASM file
```
Usage: ./wasmProf [-p] <wasm file>  
[-p] force print, cause results to be printed main function exits
```

When WasmProf is run, two output file will be created. One is the instrumented .wasm file which should be swapped in to replace the original .wasm file. The other output is a .js file which should be included before the JavaScript code that does the WebAssembly initialization.  
For example, prepend the code in the .js file to the JavaScript produced by Emscripten or similar WebAssembly compiler. 

### Note on force print
The force print option is disabled by default and is not necessary if running the WebAssembly program in a browser. In the browser, profiler output can be printed using the JavaScript console. The force print is useful when running a WebAssembly program from the command line (such as in Node.js) where a JavaScript console is not available. With force print enabled, all profiler results will be printed whenever a top-level WebAssembly function finishes executing. 

## Running instrumented WASM file
Once a WebAssembly binary is instrumented and the .wasm and .js files have been swapped in the WebAssembly program can be run as it usually would.

Profiling data can be accessed using the browser’s JavaScript console by running the following commands:  
```javascript
//This command saves a snapshot of all profiling data collected so far.
var results = window.WasmProf.saveResults();
```

```javascript
//This command prints the flat profile representation
results.flatProfile();
```

```javascript
//This command prints the call graph representation
results.callGraph();
```

## Output
The printed profiler outputs mimick the formatting of gprof. See 
https://ftp.gnu.org/old-gnu/Manuals/gprof-2.9.1/html_chapter/gprof_5.html
for more info. Only flat profile and call graph outputs are implemented and the call graph output does not yet handle recursion as gprof does.


# Paper
This project is part of a master's thesis at Cal Poly - San Luis Obispo. The full paper will be linked here once available. 
