#define JS_SRC(FUNCMAPNAME, FUNCMAP) "\
const { PerformanceObserver, performance } = require('perf_hooks'); \n\
//monkey patch the WASM instantiation to add the imports required by the profiler code \n\
const oldInstantiate = WebAssembly.instantiate; \n\
WebAssembly.instantiate = (sourceBuffer, importObject) => { \n\
    let importObjectWithProfiler = importObject || {}; \n\
    var arcs = {}; \n" + \
    FUNCMAP + "\n\
    importObjectWithProfiler.prof = {printInt : arg => console.log('Debug int:' + arg), \n\
                                     getTime: () => performance.now(),  \n\
                                     resultsReady: function(){ arcs = {};}, \n\
                                     updateArc: function(srcID, destID, callCount, targetTime){ \n\
                                        //arcs.push({'src': srcID, 'dest': destID, 'count': callCount, 'time': targetTime}); \n\
                                        if(arcs[srcID] == undefined) arcs[srcID] = {};\n\
                                        if(arcs[srcID][destID] == undefined) arcs[srcID][destID] = {'src': srcID, 'dest': destID, 'count': 0, 'time': 0.0};\n\
                                        arcs[srcID][destID].count += callCount;\n\
                                        arcs[srcID][destID].time += targetTime;\n\
                                      }, \n\
                                     printResults: function(){ \n\
                                         console.log('__RESULTS__'); \n\
                                         //arcs.forEach(srcArc => srcArc.forEach(a => {if(a.count > 0) console.log(a.count + ' calls from ' + fMap[a.src] + ' to ' + fMap[a.dest] + \n\
                                         //   ' with ' + a.time.toFixed(3) + ' ms')})); \n\
                                         for(var s in arcs){\n\
                                           for(var d in arcs[s]){\n\
                                             if(arcs[s][d].count > 0) console.log(arcs[s][d].count + ' calls from ' + fMap[s] + ' to ' + fMap[d] + \n\
                                                ' with ' + arcs[s][d].time.toFixed(3) + ' ms');\n\
                                           }\n\
                                         }\n\
                                     } }; \n\
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
