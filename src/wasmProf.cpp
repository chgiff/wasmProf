#include <fstream>
#include "wasm-io.h"
#include "js.h"
#include "visitor.h"
#include "wasm-validator.h"
#include "ast_gen.h"

#define WASI_EXPERIMENTAL 0
#define DEBUG_ALWAYS_EXPORT_DATA 0

using namespace wasm;

void addProfFunctions(Module *mod, bool forcePrint)
{
    //TODO debugging only
    Function *printInt = new Function();
    printInt->name = Name("printInt");
    printInt->module = Name("prof");
    printInt->base = Name("printInt");
    printInt->params.push_back(Type::i32);
    mod->addFunction(printInt);

    //getTime function import
    Function *getTime = new Function();
    getTime->name = Name("getTime");
    getTime->module = Name("prof");
    getTime->base = Name("getTime");
    getTime->result = Type::f64;
    mod->addFunction(getTime);

    //clearResults function import
    Function *clearResults = new Function();
    clearResults->name = Name("clearResults");
    clearResults->module = Name("prof");
    clearResults->base = Name("clearResults");
    clearResults->result = Type::none;
    mod->addFunction(clearResults);

    //addArcData function import
    Function *addArcData = new Function();
    addArcData->name = Name("addArcData");
    addArcData->module = Name("prof");
    addArcData->base = Name("addArcData");
    addArcData->params.push_back(Type::i32); //src function
    addArcData->params.push_back(Type::i32); //target function id
    addArcData->params.push_back(Type::i32); //arc call count
    addArcData->params.push_back(Type::f64); //target accumulate time //TODO check type
    mod->addFunction(addArcData);

    //setArcData function import
    Function *setArcData = new Function();
    setArcData->name = Name("setArcData");
    setArcData->module = Name("prof");
    setArcData->base = Name("setArcData");
    setArcData->params.push_back(Type::i32); //src function
    setArcData->params.push_back(Type::i32); //target function id
    setArcData->params.push_back(Type::i32); //arc call count
    setArcData->params.push_back(Type::f64); //target accumulate time //TODO check type
    mod->addFunction(setArcData);

    //printResults function import
    Function *printResults = new Function();
    printResults->name = Name("printResults");
    printResults->module = Name("prof");
    printResults->base = Name("printResults");
    mod->addFunction(printResults);

    //print result function
    Function *exportData = new Function();
    exportData->name = Name("exportData");
    Block *body = new Block(mod->allocator);

    //DEBUG
    //body->list.push_back(createCall(mod->allocator, Name("printInt"), 1, createConst(Literal(74))));

    Index countLocalInd = 0;
    if(!DEBUG_ALWAYS_EXPORT_DATA){
        //add local to store count of each arc for checking
        exportData->vars.push_back(Type::i32);
        countLocalInd = exportData->params.size() + exportData->vars.size() - 1;
    }

    //update tracking info for each arc by calling out to host
    // for(struct CallPath & arc : arcs){
    for(auto & pair : arcs){
        struct CallPath & arc = pair.second;
        Call *c = new Call(mod->allocator);
        c->target = setArcData->name;

        Const *src = new Const();
        src->set(Literal(arc.srcFuncID));
        Const *target = new Const();
        target->set(Literal(arc.targetFuncID));
        GetGlobal *gCount = new GetGlobal();
        gCount->name = arc.globalCounter;
        GetGlobal *gTime = new GetGlobal();
        gTime->name = arc.globalTimeInTarget;

        c->operands.push_back(src);
        c->operands.push_back(target);
        c->operands.push_back(gCount);
        c->operands.push_back(gTime);

        if(DEBUG_ALWAYS_EXPORT_DATA){
            body->list.push_back(c);
        }
        else{
            SetLocal *teeCount = createSetLocal(countLocalInd, c->operands[2]);
            teeCount->setTee(true);
            teeCount->type = Type::i32;

            //set call to use local instead of getting global twice
            c->operands[2] = createGetLocal(countLocalInd);

            If *countCheck = new If();
            countCheck->condition = teeCount;
            countCheck->ifTrue = c;
            body->list.push_back(countCheck);
        }
    }
    if(forcePrint){
        //call imported print results function
        Call *c = new Call(mod->allocator);
        c->target = printResults->name;
        body->list.push_back(c);
    }
    exportData->body = body;

    mod->addFunction(exportData);

    //make it exported
    Export *expor = new Export();
    expor->name = "exportData";
    expor->value = "exportData";
    expor->kind = ExternalKind::Function;
    mod->addExport(expor);
}

void addWasiFunctionExperimental(Module *mod)
{
    Function *getTime = wasi_getTime(mod);
    getTime->name = Name("getTime");

    Function *importedClockTime = new Function();
    importedClockTime->name = Name("clock_time_get");
    importedClockTime->base = Name("clock_time_get");
    importedClockTime->module = Name("wasi_unstable");
    importedClockTime->params.push_back(Type::i32);
    importedClockTime->params.push_back(Type::i64);
    importedClockTime->params.push_back(Type::i32);
    importedClockTime->result = Type::i32;

    mod->addFunction(getTime);
    mod->addFunction(importedClockTime);


    //stub for other prof functions
    Function *printRes = new Function();
    printRes->name = Name("_profPrintResultInternal");
    printRes->body = new Nop();
    mod->addFunction(printRes);

    //clearResults function import
    Function *clearResults = new Function();
    clearResults->name = Name("clearResults");
    clearResults->result = Type::none;
    clearResults->body = new Nop();
    mod->addFunction(clearResults);

    //addArcData function import
    Function *addArcData = new Function();
    addArcData->name = Name("addArcData");
    addArcData->params.push_back(Type::i32); //src function
    addArcData->params.push_back(Type::i32); //target function id
    addArcData->params.push_back(Type::i32); //arc call count
    addArcData->params.push_back(Type::f64); //target accumulate time //TODO check type
    addArcData->body = new Nop();
    mod->addFunction(addArcData);

    //setArcData function import
    Function *setArcData = new Function();
    setArcData->name = Name("setArcData");
    setArcData->params.push_back(Type::i32); //src function
    setArcData->params.push_back(Type::i32); //target function id
    setArcData->params.push_back(Type::i32); //arc call count
    setArcData->params.push_back(Type::f64); //target accumulate time //TODO check type
    setArcData->body = new Nop();
    mod->addFunction(setArcData);

    //TODO debugging only
    Function *printInt = new Function();
    printInt->name = Name("printInt");
    printInt->params.push_back(Type::i32);
    printInt->body = new Nop();
    mod->addFunction(printInt);
}

//write out a js file that declares a json object mapping function id to function name
void writeFuncNameMap(std::ofstream& jsFile)
{
    std::string jsFuncMapName = "WasmProf.fMap";

    //build the function id -> function name map
    std::string jsFuncMap = jsFuncMapName + "=[]; ";
    for(auto const& f : funcIDs){
        jsFuncMap = jsFuncMap + jsFuncMapName + "[" + std::to_string(f.second) + "]=\"" + f.first.str + "\";"; //id number index to name
    }
    jsFile << JS_SRC(jsFuncMapName, jsFuncMap);
}

int main(int argc, char* argv[]) 
{
    bool forcePrint = false;
    char empty = 0;
    char *sourceMapFile = &empty;
    if(argc < 2){
        std::cout << "Usage: "<< argv[0] << "[-p] <wasm file>" << std::endl;
        std::cout << "[-p] force print, cause results to be printed main function exits" << std::endl;
        std::cout << "[-s] <source> set sourcemap file" << std::endl;
        exit(0);
    }

    int curArg = 0;
    while(curArg < argc){
        if(!strcmp(argv[curArg], "-p")){
            forcePrint = true;
            for(int i = curArg +1; i < argc; i++){
                argv[i-1] = argv[i];
            }
            argc--;
        }
        else if(!strcmp(argv[curArg], "-s")){
            sourceMapFile = argv[curArg + 1];
            for(int i = curArg+2; i < argc; i++){
                argv[i-2] = argv[i];
            }
            argc--;
        }
        else{
            curArg++;
        }
    }

    Module mod;
    ProfVisitor v;
    ModuleReader reader;
    ModuleWriter writer;
    WasmValidator wasmValid;

    try {
        reader.read(argv[1], mod, sourceMapFile);
    } catch (ParseException& p) {
        p.dump(std::cerr);
        std::cerr << "Error in parsing input, Exiting" << std::endl;
        exit(1);
    }

    wasmValid.validate(mod, FeatureSet::MVP, WasmValidator::FlagValues::Minimal);

    v.setDynamicIndirectUpdate(true);
    //v.forceDataExport = forcePrint;
    v.forceDataExport = true;
    //forcePrint = true;
    v.instrument(&mod);
    v.report();
    if(WASI_EXPERIMENTAL){
        addWasiFunctionExperimental(&mod);
    }
    else{
        addProfFunctions(&mod, forcePrint);
    }
    //WasmPrinter::printModule(&mod);

    std::string path = std::string(argv[1]);
    size_t pathBasePos = path.rfind('/');
    std::string profPath;
    if(pathBasePos == std::string::npos) profPath = "prof_" + path;
    else profPath = path.substr(0, pathBasePos+1) + "prof_" + path.substr(pathBasePos+1, path.length()-pathBasePos);

    writer.write(mod, profPath);

    //write the accompanying js
    std::ofstream jsFile;
    jsFile.open(profPath + ".js");
    writeFuncNameMap(jsFile);
    jsFile.close();

    

    return 0;
}