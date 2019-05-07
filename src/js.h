#define JS_SRC(FUNCMAPNAME, FUNCMAP) "\
if(typeof window == 'undefined'){var performance = require('perf_hooks')['performance'];}  \n\
//monkey patch the WASM instantiation to add the imports required by the profiler code \n\
let WasmProf = { \n\
  arcs : {},\n\
  fMap : [],\n\
  printResults: function(){\n\
    console.log('__RESULTS__');\n\
    for(var s in WasmProf.arcs){\n\
      for(var d in WasmProf.arcs[s]){\n\
        if(WasmProf.arcs[s][d].count > 0) console.log((WasmProf.arcs[s][d].count + WasmProf.arcs[s][d].dynamicCount) + ' calls from ' + WasmProf.fMap[s] + \n\
        ' to ' + WasmProf.fMap[d] + ' with ' + (WasmProf.arcs[s][d].dynamicTime + WasmProf.arcs[s][d].time).toFixed(3) + ' ms');\n\
      }\n\
    }\n\
  },\n\
  printResults2: function(){\n\
    class function_class {\n\
      constructor(name) {\n\
          this.name = name;\n\
          this.selfTime = 0.0;\n\
          this.cumulativeTime = 0.0;\n\
          this.called = 0;\n\
          this.children = [];\n\
          this.parents = [];\n\
      }\n\
    }\n\
    var functions = [];\n\
    for(var s in WasmProf.arcs){\n\
        if(functions[s] == undefined){\n\
            functions[s] = new function_class(WasmProf.fMap[s]);\n\
        }\n\
        for(var d in WasmProf.arcs[s]){\n\
            if(functions[d] == undefined){\n\
                functions[d] = new function_class(WasmProf.fMap[d]);\n\
            }\n\
            functions[s].children.push(d);\n\
            functions[s].selfTime -= WasmProf.arcs[s][d].time;\n\
\n\
            functions[d].cumulativeTime += WasmProf.arcs[s][d].time;\n\
            functions[d].selfTime += WasmProf.arcs[s][d].time;\n\
            functions[d].called += WasmProf.arcs[s][d].count;\n\
            functions[d].parents.push(s);\n\
        }\n\
    }\n\
\n\
    functions.sort((a, b) => b.selfTime - a.selfTime);\n\
\n\
    console.log('\tcummulative\tself\tcalled\tname');\n\
    for(i = 0; i < functions.length; i++){\n\
        if(functions[i] != undefined && functions[i].called > 0){\n\
            console.log('\t' + functions[i].cumulativeTime.toFixed(3) + '\t\t' + functions[i].selfTime.toFixed(3) + '\t' + functions[i].called + '\t' + functions[i].name);\n\
        }\n\
    }\n\
  },\n\
  arc : function(count, time){\n\
    this.count = count;\n\
    this.dynamicCount = 0;\n\
    this.time = time;\n\
    this.dynamicTime = 0;\n\
  }\n\
};\n" + \
FUNCMAP + "\n\
const oldInstantiate = WebAssembly.instantiate; \n\
WebAssembly.instantiate = (sourceBuffer, importObject) => { \n\
    let importObjectWithProfiler = importObject || {}; \n\
    importObjectWithProfiler.prof = {printInt : arg => console.log('Debug int:' + arg), \n\
                                     getTime: () => performance.now(),  \n\
                                     clearResults: function(){ WasmProf.arcs = {};},\n\
                                     addArcData: function(srcID, destID, callCount, targetTime){ \n\
                                        if(WasmProf.arcs[srcID] == undefined) WasmProf.arcs[srcID] = {};\n\
                                        if(WasmProf.arcs[srcID][destID] == undefined) WasmProf.arcs[srcID][destID] = new WasmProf.arc(0, 0.0);\n\
                                        WasmProf.arcs[srcID][destID].dynamicCount += callCount;\n\
                                        WasmProf.arcs[srcID][destID].dynamicTime += targetTime;\n\
                                      }, \n\
                                      setArcData: function(srcID, destID, callCount, targetTime){ \n\
                                        if(WasmProf.arcs[srcID] == undefined) WasmProf.arcs[srcID] = {};\n\
                                        if(WasmProf.arcs[srcID][destID] == undefined){ WasmProf.arcs[srcID][destID] = new WasmProf.arc(callCount, targetTime); return;} \n\
                                        WasmProf.arcs[srcID][destID].count = callCount;\n\
                                        WasmProf.arcs[srcID][destID].time = targetTime;\n\
                                      }, \n\
                                      printResults: WasmProf.printResults2\n\
                                    };\n\
    const result = oldInstantiate(sourceBuffer, importObjectWithProfiler); \n\
    return result; \n\
}; \n\
const oldInstantiateStreaming = WebAssembly.instantiateStreaming; \n\
WebAssembly.instantiateStreaming = async (source, importObject) => { \n\
    let response = await source; \n\
    let buffer = await response.arrayBuffer(); \n\
    return WebAssembly.instantiate(buffer, importObject); \n\
}; \n\
WebAssembly.Instance = () => { \n\
    throw 'synchronous WebAssembly instantiation is not supported by Profiler' \n\
}; \n\
"
