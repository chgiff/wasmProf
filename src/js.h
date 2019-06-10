#define JS_SRC(FUNCMAPNAME, FUNCMAP) "\
if (typeof window == 'undefined' && typeof module !== 'undefined') {\n\
    var performance = require('perf_hooks')['performance'];\n\
}\n\
//monkey patch the WASM instantiation to add the imports required by the profiler code \n\
let WasmProf = {\n\
    arcs: {}, //actively updated list of arcs\n\
    Arc: class {\n\
        constructor(count, time) {\n\
            this.count = count;\n\
            this.dynamicCount = 0;\n\
            this.time = time;\n\
            this.dynamicTime = 0;\n\
        }\n\
        clone(){\n\
            var copy = new WasmProf.Arc(this.count, this.time);\n\
            copy.dynamicCount = this.dynamicCount;\n\
            copy.dynamicTime = this.dynamicCount;\n\
            return copy;\n\
        }\n\
    },\n\
    fMap: [], //pregenerated function map\n\
\n\
    //print function called by user or from wasm code on return if force print was enabled\n\
    printResults: function() {\n\
        var r = WasmProf.saveResults();\n\
        r.callGraph();\n\
        r.flatProfile();\n\
    },\n\
\n\
    //result info about a single function\n\
    FunctionResult: class {\n\
        constructor(name) {\n\
            this.name = name;\n\
            this.selfTime = 0.0;\n\
            this.cumulativeTime = 0.0;\n\
            this.called = 0;\n\
            this.children = [];\n\
            this.parents = [];\n\
        }\n\
\n\
        clone() {\n\
            var copy = new WasmProf.FunctionResult(this.name);\n\
            copy.selfTime = this.selfTime;\n\
            copy.cumulativeTime = this.cumulativeTime;\n\
            copy.called = this.called;\n\
            copy.children = this.children.slice();\n\
            copy.parents = this.parents.slice();\n\
            return copy;\n\
        }\n\
    },\n\
\n\
    //result info about all functions\n\
    Results: class {\n\
        constructor(funcResultArr, clone) {\n\
            if (clone) {\n\
                this.funcResults = [];\n\
                funcResultArr.forEach(function(elem) {\n\
                    this.funcResult.push(elem.clone())\n\
                });\n\
            } else {\n\
                this.funcResults = funcResultArr;\n\
            }\n\
\n\
            //get the 0th element which is the 'external' function\n\
            this.external = this.funcResults.shift();\n\
            //remove everything not called\n\
            this.funcResults = this.funcResults.filter(elem => elem.called > 0);\n\
\n\
            //create sorted arrays\n\
            this.selfTimeSort = this.funcResults.slice();\n\
            this.selfTimeSort.sort((a,b) => b.selfTime - a.selfTime);\n\
            this.cumulativeTimeSort = this.funcResults.slice();\n\
            this.cumulativeTimeSort.sort((a,b) => b.cumulativeTime - a.cumulativeTime);\n\
        }\n\
\n\
        flatProfile(count) {\n\
            var totTime = (-1 * this.external.selfTime);\n\
            console.log('Total time: ' + totTime);\n\
            console.log(' % \tcumulative\tself\tcalled\tself ms/call\t total ms/call\tname');\n\
            for (var i = 0; i < this.selfTimeSort.length && count != 0; i++) {\n\
                if (this.selfTimeSort[i] != undefined && this.selfTimeSort[i].called > 0) {\n\
                    console.log((100*this.selfTimeSort[i].selfTime/totTime).toFixed(2) + '\t' + this.selfTimeSort[i].cumulativeTime.toFixed(3) + '\t\t' + \n\
                            this.selfTimeSort[i].selfTime.toFixed(3) + '\t' + this.selfTimeSort[i].called + '\t' + \n\
                            (this.selfTimeSort[i].selfTime/this.selfTimeSort[i].called ).toFixed(3) + '\t' + \n\
                            (this.selfTimeSort[i].cumulativeTime/this.selfTimeSort[i].called).toFixed(3) + '\t' + this.selfTimeSort[i].name);\n\
                    count--;\n\
                }\n\
            }\n\
        }\n\
\n\
        callGraph(count) {\n\
            function printParent(parent){\n\
                if(parent.function.index == undefined) console.log('\t\t\t\t\t\t  <spontaneous>');\n\
                else {\n\
                    var parentCount = (parent.arc.count + parent.arc.dynamicCount);\n\
                    var parentTotalTime = (parent.arc.time + parent.arc.dynamicTime);\n\
                    var parentSelf = parentTotalTime * (curFunc.selfTime/curFunc.cumulativeTime);\n\
                    var parentChildren = parentTotalTime - parentSelf;\n\
                    console.log('\t\t' + parentSelf.toFixed(3) + '\t' + parentChildren.toFixed(3) + '\t\t' + parentCount + '/' + curFunc.called + '\t' + \n\
                        parent.function.name + ' ['+parent.function.index+']');\n\
                }\n\
            }\n\
            function printChild(child){\n\
                var childCount = (child.arc.count + child.arc.dynamicCount);\n\
                var childTotalTime = (child.arc.time + child.arc.dynamicTime);\n\
                var childSelf = childTotalTime * (curFunc.selfTime/curFunc.cumulativeTime);\n\
                var childChildren = childTotalTime - childSelf;\n\
                console.log('\t\t' + childSelf.toFixed(3) + '\t' + childChildren.toFixed(3) + '\t\t' + childCount + '/' + child.function.called + '\t' + \n\
                        child.function.name + ' ['+child.function.index+']');\n\
            }\n\
\n\
            if(count == undefined) count = 5; //default of 5 entries\n\
\n\
            for(var i = 0; i < this.cumulativeTimeSort.length; i++){\n\
                if(this.cumulativeTimeSort[i] != undefined && this.cumulativeTimeSort[i].called > 0) this.cumulativeTimeSort[i].index = i;\n\
            }\n\
            var totTime = (-1 * this.external.selfTime);\n\
            console.log('index\t% time\tself\tchildren\tcalled\tname');\n\
            for(var i = 0; i < this.cumulativeTimeSort.length && count != 0; i++){\n\
                var curFunc = this.cumulativeTimeSort[i];\n\
                if(curFunc != undefined && curFunc.called > 0){\n\
                    curFunc.parents.forEach(printParent);\n\
                    console.log('['+curFunc.index+']' + '\t' + (100*curFunc.cumulativeTime/totTime).toFixed(2) + '\t' + curFunc.selfTime.toFixed(3) + '\t' +\n\
                        (curFunc.cumulativeTime - curFunc.selfTime).toFixed(3) + '\t\t' + curFunc.called + '\t' + curFunc.name);\n\
                    curFunc.children.forEach(printChild);\n\
                    console.log('--------------------------------------------------');\n\
                    count--;\n\
                }\n\
            }\n\
        }\n\
    },\n\
\n\
    //saves the current state of the results and return a Results object\n\
    saveResults: function() {\n\
        var functions = [];\n\
        for (var s in WasmProf.arcs) {\n\
            if (functions[s] == undefined) {\n\
                functions[s] = new WasmProf.FunctionResult(WasmProf.fMap[s]);\n\
            }\n\
            for (var d in WasmProf.arcs[s]) {\n\
                if (functions[d] == undefined) {\n\
                    functions[d] = new WasmProf.FunctionResult(WasmProf.fMap[d]);\n\
                }\n\
                var count = WasmProf.arcs[s][d].count + WasmProf.arcs[s][d].dynamicCount;\n\
                var time = WasmProf.arcs[s][d].time + WasmProf.arcs[s][d].dynamicTime;\n\
                if(count > 0){\n\
                    functions[s].children.push({function: functions[d], arc: WasmProf.arcs[s][d].clone()});\n\
                    functions[s].selfTime -= time;\n\
\n\
                    functions[d].cumulativeTime += time;\n\
                    functions[d].selfTime += time;\n\
                    functions[d].called += count;\n\
                    functions[d].parents.push({function: functions[s], arc: WasmProf.arcs[s][d].clone()});\n\
                }\n\
            }\n\
        }\n\
\n\
        return new WasmProf.Results(functions);\n\
    }\n\
};\n\
if(typeof window != 'undefined') window.WasmProf = WasmProf;\n\
" + FUNCMAP + "\n\
const oldInstantiate = WebAssembly.instantiate;\n\
WebAssembly.instantiate = (sourceBuffer, importObject) => {\n\
    let importObjectWithProfiler = importObject || {};\n\
    importObjectWithProfiler.prof = {\n\
        printInt: arg => console.log('Debug int:' + arg),\n\
        getTime: () => Date.now(),\n\
        clearResults: function() {\n\
            WasmProf.arcs = {};\n\
        },\n\
        addArcData: function(srcID, destID, callCount, targetTime) {\n\
            if (WasmProf.arcs[srcID] == undefined) WasmProf.arcs[srcID] = {};\n\
            if (WasmProf.arcs[srcID][destID] == undefined) WasmProf.arcs[srcID][destID] = new WasmProf.Arc(0, 0.0);\n\
            WasmProf.arcs[srcID][destID].dynamicCount += callCount;\n\
            WasmProf.arcs[srcID][destID].dynamicTime += targetTime;\n\
        },\n\
        setArcData: function(srcID, destID, callCount, targetTime) {\n\
            if (WasmProf.arcs[srcID] == undefined) WasmProf.arcs[srcID] = {};\n\
            if (WasmProf.arcs[srcID][destID] == undefined) {\n\
                WasmProf.arcs[srcID][destID] = new WasmProf.Arc(callCount, targetTime);\n\
                return;\n\
            }\n\
            WasmProf.arcs[srcID][destID].count = callCount;\n\
            WasmProf.arcs[srcID][destID].time = targetTime;\n\
        },\n\
        printResults: WasmProf.printResults\n\
    };\n\
    const result = oldInstantiate(sourceBuffer, importObjectWithProfiler);\n\
    result.then(function(obj){\n\
        if(obj.exports != undefined){\n\
            var exp = obj.exports;\n\
        }\n\
        else{\n\
            var exp = obj.instance.exports;\n\
        }\n\
        if(exp.exportData != undefined){\n\
            WasmProf.exportData = exp.exportData;\n\
        }\n\
        else{\n\
            console.log('Error WebAssembly file is missing exported function \"exportData\"');\n\
            WasmProf.exportData = function(){};\n\
        }\n\
    });\n\
    return result;\n\
};\n\
const oldInstantiateStreaming = WebAssembly.instantiateStreaming;\n\
WebAssembly.instantiateStreaming = async (source, importObject) => {\n\
    let response = await source;\n\
    let buffer = await response.arrayBuffer();\n\
    return WebAssembly.instantiate(buffer, importObject);\n\
};\n\
WebAssembly.Instance = () => {\n\
    throw 'synchronous WebAssembly instantiation is not supported by Profiler'\n\
};"
