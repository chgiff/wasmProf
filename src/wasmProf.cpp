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

    //resultsReady function import
    Function *resultsReady = new Function();
    resultsReady->name = Name("resultsReady");
    resultsReady->module = Name("prof");
    resultsReady->base = Name("resultsReady");
    resultsReady->result = Type::none;
    mod->addFunction(resultsReady);

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
    //call imported resultsReady function
    Call *resReadyCall = new Call(mod->allocator);
    resReadyCall->target = resultsReady->name;
    body->list.push_back(resReadyCall);
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

    v.instrument(&mod);
    addProfFunctions(&mod);

    //WasmPrinter::printModule(&mod);

    writer.write(mod, "prof_" + std::string(argv[1]));

    //write the accompanying js
    std::ofstream jsFile;
    jsFile.open("prof_" + std::string(argv[1]) + ".js");
    writeFuncNameMap(jsFile);
    jsFile.close();

    return 0;
}