#define JS_SRC(FUNCMAPNAME, FUNCMAP) "\
const { PerformanceObserver, performance } = require('perf_hooks'); \n\
//monkey patch the WASM instantiation to add the imports required by the profiler code \n\
const oldInstantiate = WebAssembly.instantiate; \n\
WebAssembly.instantiate = (sourceBuffer, importObject) => { \n\
    let importObjectWithProfiler = importObject || {}; \n\
    var arcs = []; \n" + \
    FUNCMAP + "\n\
    importObjectWithProfiler.prof = {printInt : arg => console.log('Debug int:' + arg), \n\
                                     getTime: () => performance.now(),  \n\
                                     updateArc: function(srcID, destID, callCount, targetTime){ \n\
                                        arcs.push({'src': srcID, 'dest': destID, 'count': callCount, 'time': targetTime}); \n\
                                     }, \n\
                                     printResults: function(){ \n\
                                         arcs.forEach(a => {if(a.count > 0) console.log(a.count + ' calls from ' + " + FUNCMAPNAME+"[a.src] + ' to ' + " + FUNCMAPNAME+"[a.dest] + \n\
                                            ' with ' + a.time.toFixed(3) + ' ms')}); \n\
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
