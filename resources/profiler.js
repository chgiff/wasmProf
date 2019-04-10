//monkey patch the WASM instantiation to add the imports required by the profiler code
const oldInstantiate = WebAssembly.instantiate;
WebAssembly.instantiate = (sourceBuffer, importObject) => {
    let importObjectWithProfiler = importObject || {};
    var arcs = [];
    importObjectWithProfiler.prof = {printInt : arg => console.log("Debug int:" + arg), 
                                     getTime: () => performance.now(), 
                                     updateArc: function(srcID, destID, callCount, targetTime){
                                        arcs.push({"src": srcID, "dest": destID, "count": callCount, "time": targetTime});
                                     },
                                     printResults: function(){
                                         arcs.forEach(a => console.log(a.callCount + " calls from " + profFuncMap[a.srcID] + " to " + profFuncMap[a.destID]));
                                     } };

    const result = oldInstantiate(sourceBuffer, importObjectWithProfiler);
    return result;
};

// just fall-back to regular instantiation since Wasabi doesn't support streaming instrumentation (yet) anyway
const oldInstantiateStreaming = WebAssembly.instantiateStreaming;
WebAssembly.instantiateStreaming = async (source, importObject) => {
    let response = await source;
    let buffer = await response.arrayBuffer();
    return WebAssembly.instantiate(buffer, importObject);
};

WebAssembly.Instance = () => {
    throw "synchronous WebAssembly instantiation is not supported by Profiler"
};