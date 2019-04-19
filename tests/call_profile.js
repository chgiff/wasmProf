var arcs = {};
var call_stack = new Array();
call_stack.peek = function(){return call_stack[call_stack.length-1];}

function fctName(fctId) {
    const fct = Wasabi.module.info.functions[fctId];
    if (fct.export[0] !== undefined) return fct.export[0];
    if (fct.import !== null) return fct.import;
    return fctId;
}

for(i = 0; i < Wasabi.module.info.functions.length; i++){
    if(fctName(i) == "_main" || fctName(i) == "main"){
        call_stack.push({id:i, start_time:performance.now()});
        break;
    }
}


function visitArc(arcs, src, dest)
{
    arcs[src][dest].visted = true;
    for(const newDest in arcs[dest]){
        visitArc(dest, newDest);
    }
}



function callInfo()
{
    var functions = [];
    for(var s in arcs){
        if(functions[s] == undefined){
            functions[s] = {name:fctName(s), selfTime:0.0, cumulativeTime:0.0, called:0, children:[], parents:[]};
        }
        for(var d in arcs[s]){
            if(functions[d] == undefined){
                functions[d] = {name:fctName(d), selfTime:0.0, cumulativeTime:0.0,  called:0, children:[], parents:[]};
            }
            functions[s].children.push(d);
            functions[s].selfTime -= arcs[s][d].time;

            functions[d].cumulativeTime += arcs[s][d].time;
            functions[d].selfTime += arcs[s][d].time;
            functions[d].called += arcs[s][d].count;
            functions[d].parents.push(s);
        }
    }

    functions.sort(function(a,b){b.selfTime - a.selfTime});

    console.log("\tcummulative\tself\tcalled\tname");
    for(i = 0; i < functions.length; i++){
        console.log("\t" + functions[i].cumulativeTime.toFixed(3) + "\t\t" + functions[i].selfTime.toFixed(3) + "\t" + functions[i].called + "\t" + functions[i].name);
    }
}

function printResults(){ 
    console.log('__RESULTS__');
    //arcs.forEach(srcArc => srcArc.forEach(a => {if(a.count > 0) console.log(a.count + ' calls from ' + fMap[a.src] + ' to ' + fMap[a.dest] + 
    //   ' with ' + a.time.toFixed(3) + ' ms')})); 
    for(var s in arcs){
      for(var d in arcs[s]){
        if(arcs[s][d].count > 0) console.log(arcs[s][d].count + ' calls from ' + fctName(s) + ' to ' + fctName(d) + 
           ' with ' + arcs[s][d].time.toFixed(3) + ' ms');
      }
    }
    callInfo();
}

// before each call, print function index and passed arguments
Wasabi.analysis.call_pre = function (location, func, args) {
    console.log("started call to " + fctName(func));
    call_stack.push({id:func, start_time:performance.now()});  
};

Wasabi.analysis.call_post = function(location, value) {
    var func = call_stack.pop();
    console.log("ended call to " + fctName(func.id));
    if(undefined == arcs[call_stack.peek().id]){
        arcs[call_stack.peek().id] = {};
    }
    if(undefined == arcs[call_stack.peek().id][func.id]){
       arcs[call_stack.peek().id][func.id] = {count:0, time:0.0};
    }
    arcs[call_stack.peek().id][func.id].count++
    arcs[call_stack.peek().id][func.id].time += (performance.now()-func.start_time);
    
    if(fctName(call_stack.peek().id) == "_main" || fctName(call_stack.peek().id) == "main"){
    //if(call_stack.peek() == undefined){
        printResults();
    }
};

