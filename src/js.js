//this should match what is in js.h
if (typeof window == 'undefined') {
    var performance = require('perf_hooks')['performance'];
}
//monkey patch the WASM instantiation to add the imports required by the profiler code 
let WasmProf = {
    arcs: {}, //actively updated list of arcs
    Arc: class {
        constructor(count, time) {
            this.count = count;
            this.dynamicCount = 0;
            this.time = time;
            this.dynamicTime = 0;
        }
    },
    fMap: [], //pregenerated function map

    //print function called by user or from wasm code on return if force print was enabled
    printResults: function() {
        WasmProf.saveResults().flatProfile(-1);
    },

    //result info about a single function
    FunctionResult: class {
        constructor(name) {
            this.name = name;
            this.selfTime = 0.0;
            this.cumulativeTime = 0.0;
            this.called = 0;
            this.children = [];
            this.parents = [];
        }

        clone() {
            var copy = new WasmProf.FunctionResult(this.name);
            copy.selfTime = this.selfTime;
            copy.cumulativeTime = this.cumulativeTime;
            copy.called = this.called;
            copy.children = this.children.slice();
            copy.parents = this.parents.slice();
            return copy;
        }
    },

    //result info about all functions
    Results: class {
        constructor(funcResultArr, clone) {
            if (clone) {
                this.funcResults = [];
                funcResultArr.forEach(function(elem) {
                    this.funcResult.push(elem.clone())
                });
            } else {
                this.funcResults = funcResultArr;
            }
        }

        flatProfile(count) {
            this.funcResults.sort(function(a, b) {
                b.selfTime - a.selfTime
            });

            console.log("\tcummulative\tself\tcalled\tname");
            for (i = 0; i < this.funcResults.length && count != 0; i++) {
                if (this.funcResults[i] != undefined && this.funcResults[i] > 0) {
                    console.log("\t" + this.funcResults[i].cumulativeTime.toFixed(3) + "\t\t" + this.funcResults[i].selfTime.toFixed(3) + "\t" + this.funcResults[i].called + "\t" + this.funcResults[i].name);
                    count--;
                }
            }
        }
    },

    //saves the current state of the results and return a Results object
    saveResults: function() {
        var functions = [];
        for (var s in WasmProf.arcs) {
            if (functions[s] == undefined) {
                functions[s] = new WasmProf.FunctionResult(WasmProf.fMap[s]);
            }
            for (var d in WasmProf.arcs[s]) {
                if (functions[d] == undefined) {
                    functions[d] = new WasmProf.FunctionResult(WasmProf.fMap[d]);
                }
                functions[s].children.push(d);
                functions[s].selfTime -= WasmProf.arcs[s][d].time;

                functions[d].cumulativeTime += WasmProf.arcs[s][d].time;
                functions[d].selfTime += WasmProf.arcs[s][d].time;
                functions[d].called += WasmProf.arcs[s][d].count;
                functions[d].parents.push(s);
            }
        }

        return new Results(functions);
    }
};
WasmProf.fMap = [];
const oldInstantiate = WebAssembly.instantiate;
WebAssembly.instantiate = (sourceBuffer, importObject) => {
    let importObjectWithProfiler = importObject || {};
    importObjectWithProfiler.prof = {
        printInt: arg => console.log('Debug int:' + arg),
        getTime: () => performance.now(),
        clearResults: function() {
            WasmProf.arcs = {};
        },
        addArcData: function(srcID, destID, callCount, targetTime) {
            if (WasmProf.arcs[srcID] == undefined) WasmProf.arcs[srcID] = {};
            if (WasmProf.arcs[srcID][destID] == undefined) WasmProf.arcs[srcID][destID] = new WasmProf.Arc(0, 0.0);
            WasmProf.arcs[srcID][destID].dynamicCount += callCount;
            WasmProf.arcs[srcID][destID].dynamicTime += targetTime;
        },
        setArcData: function(srcID, destID, callCount, targetTime) {
            if (WasmProf.arcs[srcID] == undefined) WasmProf.arcs[srcID] = {};
            if (WasmProf.arcs[srcID][destID] == undefined) {
                WasmProf.arcs[srcID][destID] = new WasmProf.Arc(callCount, targetTime);
                return;
            }
            WasmProf.arcs[srcID][destID].count = callCount;
            WasmProf.arcs[srcID][destID].time = targetTime;
        },
        printResults: WasmProf.printResults
    };
    const result = oldInstantiate(sourceBuffer, importObjectWithProfiler);
    return result;
};
const oldInstantiateStreaming = WebAssembly.instantiateStreaming;
WebAssembly.instantiateStreaming = async (source, importObject) => {
    let response = await source;
    let buffer = await response.arrayBuffer();
    return WebAssembly.instantiate(buffer, importObject);
};
WebAssembly.Instance = () => {
    throw 'synchronous WebAssembly instantiation is not supported by Profiler'
};