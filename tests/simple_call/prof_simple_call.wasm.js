const { PerformanceObserver, performance } = require('perf_hooks'); 
//monkey patch the WASM instantiation to add the imports required by the profiler code 
const oldInstantiate = WebAssembly.instantiate; 
WebAssembly.instantiate = (sourceBuffer, importObject) => { 
    let importObjectWithProfiler = importObject || {}; 
    var arcs = {}; 
fMap=[]; fMap[1]="0";fMap[2]="1";fMap[3]="2";
    importObjectWithProfiler.prof = {printInt : arg => console.log('Debug int:' + arg), 
                                     getTime: () => performance.now(),  
                                     resultsReady: function(){ arcs = {};}, 
                                     updateArc: function(srcID, destID, callCount, targetTime){ 
                                        //arcs.push({'src': srcID, 'dest': destID, 'count': callCount, 'time': targetTime}); 
                                        if(arcs[srcID] == undefined) arcs[srcID] = {};
                                        if(arcs[srcID][destID] == undefined) arcs[srcID][destID] = {'src': srcID, 'dest': destID, 'count': 0, 'time': 0.0};
                                        arcs[srcID][destID].count += callCount;
                                        arcs[srcID][destID].time += targetTime;
                                      }, 
                                     printResults: function(){ 
                                         console.log('__RESULTS__'); 
                                         //arcs.forEach(srcArc => srcArc.forEach(a => {if(a.count > 0) console.log(a.count + ' calls from ' + fMap[a.src] + ' to ' + fMap[a.dest] + 
                                         //   ' with ' + a.time.toFixed(3) + ' ms')})); 
                                         for(var s in arcs){
                                           for(var d in arcs[s]){
                                             if(arcs[s][d].count > 0) console.log(arcs[s][d].count + ' calls from ' + fMap[s] + ' to ' + fMap[d] + 
                                                ' with ' + arcs[s][d].time.toFixed(3) + ' ms');
                                           }
                                         }
                                     } }; 
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
