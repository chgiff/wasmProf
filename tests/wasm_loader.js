const fs = require('fs');

const buf = fs.readFileSync(process.argv[2]);

WebAssembly.instantiate(buf).then(mod => {
    console.log("Running main...");
    var ret = mod.instance.exports.main();
    console.log(ret);
    console.log("Done!");
});
