const fs = require('fs');

const buf = fs.readFileSync(process.argv[2]);

const importObject = {
    env: {
    'memoryBase': 0,
    'tableBase': 0,
    'memory': new WebAssembly.Memory({initial: 256}),
    'table': new WebAssembly.Table({initial: 256, element: 'anyfunc'}),
    abort: function(msg){console.log("ABORT!!!" + msg)}
    }
}

WebAssembly.instantiate(buf, importObject).then(mod => {
    var main;
    if(mod.instance.exports._main != undefined){
        main = mod.instance.exports._main
    }
    else if(mod.instance.exports.main != undefined){
        main = mod.instance.exports.main
    }
    else{
        console.log("NO MAIN FUNCTION!!!");
    }
    console.log("Running main...");
    var ret = main();
    console.log(ret);
    console.log("Done!");
});
