//this should match what is in js.h
if (typeof window == 'undefined' && typeof module !== 'undefined') {
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
        clone(){
            var copy = new WasmProf.Arc(this.count, this.time);
            copy.dynamicCount = this.dynamicCount;
            copy.dynamicTime = this.dynamicCount;
            return copy;
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

            //get the 0th element which is the 'external' function
            this.external = this.funcResults.shift();
            //remove everything not called
            this.funcResults = this.funcResults.filter(elem => elem.called > 0);

            //create sorted arrays
            this.selfTimeSort = this.funcResults.slice();
            this.selfTimeSort.sort((a,b) => b.selfTime - a.selfTime);
            this.cumulativeTimeSort = this.funcResults.slice();
            this.cumulativeTimeSort.sort((a,b) => b.cumulativeTime - a.cumulativeTime);
        }

        flatProfile(count) {
            var totTime = (-1 * this.external.selfTime);
            console.log('Total time: ' + totTime);
            console.log(' % \tcumulative\tself\tcalled\tname');
            for (var i = 0; i < this.selfTimeSort.length && count != 0; i++) {
                if (this.selfTimeSort[i] != undefined && this.selfTimeSort[i].called > 0) {
                    console.log((100*this.selfTimeSort[i].selfTime/totTime).toFixed(2) + '\t' + this.selfTimeSort[i].cumulativeTime.toFixed(3) + '\t\t' + this.selfTimeSort[i].selfTime.toFixed(3) + '\t' + this.selfTimeSort[i].called + '\t' + this.selfTimeSort[i].name);
                    count--;
                }
            }
        }

        callGraph(count) {
            function printParent(parent){
                if(parent.function.index == undefined) console.log('\t\t\t\t\t\t  <spontaneous>');
                else {
                    var parentCount = (parent.arc.count + parent.arc.dynamicCount);
                    var parentSelf = curFunc.selfTime * parentCount / curFunc.called;
                    var parentChildren = (curFunc.cumulativeTime - curFunc.selfTime) * parentCount / curFunc.called;
                    console.log('\t\t' + parentSelf.toFixed(3) + '\t' + parentChildren.toFixed(3) + '\t\t' + parentCount + '/' + curFunc.called + '\t' + 
                        parent.function.name + ' ['+parent.function.index+']');
                }
            }
            function printChild(child){
                var childCount = (child.arc.count + child.arc.dynamicCount);
                var childSelf = child.function.selfTime * childCount / curFunc.called;
                var childChildren = (child.function.cumulativeTime - child.function.selfTime) * childCount / curFunc.called;
                console.log('\t\t' + childSelf.toFixed(3) + '\t' + childChildren.toFixed(3) + '\t\t' + childCount + '/' + child.function.called + '\t' + 
                        child.function.name + ' ['+child.function.index+']');
            }

            if(count == undefined) count = 5; //default of 5 entries

            for(var i = 0; i < this.cumulativeTimeSort.length; i++){
                if(this.cumulativeTimeSort[i] != undefined && this.cumulativeTimeSort[i].called > 0) this.cumulativeTimeSort[i].index = i;
            }
            var totTime = (-1 * this.external.selfTime);
            console.log('index\t% time\tself\tchildren\tcalled\tname');
            for(var i = 0; i < this.cumulativeTimeSort.length && count != 0; i++){
                var curFunc = this.cumulativeTimeSort[i];
                if(curFunc != undefined && curFunc.called > 0){
                    curFunc.parents.forEach(printParent);
                    console.log('['+curFunc.index+']' + '\t' + (100*curFunc.cumulativeTime/totTime).toFixed(2) + '\t' + curFunc.selfTime.toFixed(3) + '\t' +
                        (curFunc.cumulativeTime - curFunc.selfTime).toFixed(3) + '\t\t' + curFunc.called + '\t' + curFunc.name);
                    curFunc.children.forEach(printChild);
                    console.log('--------------------------------------------------');
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
                var count = WasmProf.arcs[s][d].count + WasmProf.arcs[s][d].dynamicCount;
                var time = WasmProf.arcs[s][d].time + WasmProf.arcs[s][d].dynamicTime;
                if(count > 0){
                    functions[s].children.push({function: functions[d], arc: WasmProf.arcs[s][d].clone()});
                    functions[s].selfTime -= time;

                    functions[d].cumulativeTime += time;
                    functions[d].selfTime += time;
                    functions[d].called += count;
                    functions[d].parents.push({function: functions[s], arc: WasmProf.arcs[s][d].clone()});
                }
            }
        }

        return new WasmProf.Results(functions);
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