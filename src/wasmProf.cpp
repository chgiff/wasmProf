#include <fstream>
#include "wasm-io.h"
#include "js.h"
#include "visitor.h"

using namespace wasm;

void addProfFunctions(Module *mod)
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

    //updateArc function import
    Function *updateArc = new Function();
    updateArc->name = Name("updateArc");
    updateArc->module = Name("prof");
    updateArc->base = Name("updateArc");
    updateArc->params.push_back(Type::i32); //src function
    updateArc->params.push_back(Type::i32); //target function id
    updateArc->params.push_back(Type::i32); //arc call count
    updateArc->params.push_back(Type::f64); //target accumulate time //TODO check type
    mod->addFunction(updateArc);

    //printResults function import
    Function *printResults = new Function();
    printResults->name = Name("printResults");
    printResults->module = Name("prof");
    printResults->base = Name("printResults");
    mod->addFunction(printResults);

    //print result function
    Function *printRes = new Function();
    printRes->name = Name("_profPrintResultInternal");
    Block *body = new Block(mod->allocator);

    //update tracking info for each arc by calling out to host
    for(struct CallPath& arc : arcs){
        Call *c = new Call(mod->allocator);
        c->target = updateArc->name;

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
        body->list.push_back(c);
    }
    //call imported print results function
    Call *c = new Call(mod->allocator);
    c->target = printResults->name;
    body->list.push_back(c);

    printRes->body = body;
    mod->addFunction(printRes);
}

//write out a js file that declares a json object mapping function id to function name
void writeFuncNameMap(std::ofstream& jsFile)
{
    std::string jsFuncMapName = "fMap";

    //build the function id -> function name map
    std::string jsFuncMap = jsFuncMapName + "=[]; ";
    for(auto const& f : funcIDs){
        jsFuncMap = jsFuncMap + jsFuncMapName + "[" + std::to_string(f.second) + "]=\"" + f.first.str + "\";"; //id number index to name
    }
    jsFile << JS_SRC(jsFuncMapName, jsFuncMap);
}

int main(int argc, const char* argv[]) 
{
    if(argc < 2){
        std::cout << "Usage: "<< argv[0] << " <wasm file>" << std::endl;
    }

    Module mod;
    ProfVisitor v;
    ModuleReader reader;
    ModuleWriter writer;

    try {
        reader.read(argv[1], mod);
    } catch (ParseException& p) {
        p.dump(std::cerr);
        std::cout << "error in parsing input";
    }


    v.setDynamicIndirectUpdate();
    v.instrument(&mod);
    addProfFunctions(&mod);

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