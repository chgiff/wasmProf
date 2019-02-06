#include <fstream>
#include "wasm-io.h"
#include "wasm-traversal.h"

using namespace wasm;

#define MEM_OFFSET_TEMP 16*1024

Function *createMemCopy()
{
    Function *cpy = new Function();
}

//std::vector<Name> countGlobals;

struct CallPath{
    //Name srcFunc;
    //Name targetFunc;
    int srcFuncID;
    int targetFuncID;
    Name globalCounter;
    Name globalTimeInTarget; //accumulate amount of time spent in target function
};

//give each function a numberic id
int curFuncID = 0;
std::map<Name, int> funcIDs;

//list of all arcs
std::vector<struct CallPath> arcs;

struct MyVisitor : public ExpressionStackWalker<MyVisitor>
{
    //create a ast subgraph to increment the given global by 1
    Expression *createArcCounter(Name globalName)
    {
        if(!getModule()->getGlobalOrNull(globalName)){
            Global *newGlobal = new Global();
            newGlobal->name = globalName;
            newGlobal->mutable_ = true;
            newGlobal->type = Type::i32;
            Const *c = new Const();
            c->set(Literal(0));
            newGlobal->init = c; 
            getModule()->addGlobal(newGlobal);
        }

        Binary *b = new Binary();
        b->op = AddInt32;
        GetGlobal *gg = new GetGlobal();
        gg->name = globalName;
        b->left = gg;
        Const *c = new Const();
        c->set(Literal(1));
        b->right = c;

        SetGlobal *e = new SetGlobal();
        e->name = globalName;
        e->value = b;

        return e;
    }

    Expression *createArcTimeAccum(Name globalName)
    {
        //TODO
        if(!getModule()->getGlobalOrNull(globalName)){
            Global *newGlobal = new Global();
            newGlobal->name = globalName;
            newGlobal->mutable_ = true;
            newGlobal->type = Type::f64; //TODO f64 or f32 ??
            Const *c = new Const();
            c->set(Literal(0.0));
            newGlobal->init = c; 
            getModule()->addGlobal(newGlobal);
        }

        Binary *b = new Binary();
        b->op = AddFloat64; //TODO type
        GetGlobal *gg = new GetGlobal();
        gg->name = globalName;
        b->left = gg;
        //TODO call get time function
        Const *c = new Const();
        c->set(Literal(1.0));
        b->right = c;

        SetGlobal *e = new SetGlobal();
        e->name = globalName;
        e->value = b;

        return e;
    }

    void visitFunction(Function *curr)
    {
        //add this function to the list and assign it an id
        funcIDs[curr->name] = curFuncID++;

        if(curr->imported()){
            //std::cout << "import: " << curr->base << " " << curr->module << std::endl;
            return;
        }
        if(!strcmp(curr->name.c_str(), "_memcpy")){
            std::cout << "Found memcpy" << std::endl;
        }

        /*
        //counter global name (fcnName + "Cnt")
        size_t nameLen = strlen(curr->name.c_str());
        char *globalStr = (char*)malloc(nameLen + 4);
        strcpy(globalStr, curr->name.c_str());
        strcpy(globalStr + nameLen, "Cnt");
        Name globalName = Name(globalStr);
        */

        //if body is a block, just add to it
        Block *funcBlock = new Block(getModule()->allocator);
        //funcBlock->list.push_back(createArcCounter(globalName)); //No longer tracking on functions, now tracking at call arcs
        if(curr->body->is<Block>()){ 
            for(Expression *e : ((Block *)curr->body)->list){
                funcBlock->list.push_back(e);
            }
        }
        //else create a new block with counter and function body
        else{
            funcBlock->list.push_back(curr->body);
        }
        curr->body = funcBlock;

        //if exported, add setup at begining and cleanup at end
        if(getModule()->getExportOrNull(curr->name) && !strcmp(curr->name.c_str(), "_main")){ //TODO figure out which function to make top level
            std::cout << "This function is exported: " << curr->name << std::endl;
            //add call to result function at the end
            Call *result_call = new Call(getModule()->allocator);
            result_call->target = Name("_profPrintResult");
            
            //check if last expression is return and put call in front of it
            Expression *ret = funcBlock->list.pop_back();
            if(ret->is<Return>()){
                funcBlock->list.push_back(result_call);
                funcBlock->list.push_back(ret);
            }
            else{
                //otherwise put the call last
                funcBlock->list.push_back(ret);
                funcBlock->list.push_back(result_call);
            }
        }
    }

    /*
    * In order to use the lower addresses of memory for profiler data, 
    * we must add a memory offset to all loads and stores
    */
    int offset = MEM_OFFSET_TEMP;
    void visitLoad(Load *curr)
    {
        Expression * oldPtr = curr->ptr;

        //optimization if it is a constant
        if(oldPtr->is<Const>()){
            Const *c = (Const *)oldPtr;
            c->set(Literal(c->value.geti32() + offset)); //TODO making assumption about type
        }
        else{
            Binary *addOffset = new Binary();
            addOffset->op = AddInt32; //TODO check type of pointer (iPTR)
            addOffset->left = oldPtr;
            Const *c = new Const();
            c->set(Literal(offset));
            addOffset->right = c;

            curr->ptr = addOffset;
        }
    }

    void visitStore(Store *curr)
    {
        Expression *oldPtr = curr->ptr;

        //optimization if it is a constant
        if(oldPtr->is<Const>()){
            Const *c = (Const *)oldPtr;
            c->set(Literal(c->value.geti32() + offset)); //TODO making assumption about type
        }
        else{
            Binary *addOffset = new Binary();
            addOffset->op = AddInt32; //TODO check type of pointer (iPTR)
            addOffset->left = oldPtr;
            Const *c = new Const();
            c->set(Literal(offset));
            addOffset->right = c;

            curr->ptr = addOffset;
        }
    }
    
    void visitCall(Call *curr)
    {
        static int id = 0;
        struct CallPath arc = 
            {
                funcIDs[getFunction()->name],
                funcIDs[curr->target],
                Name(std::to_string(id++)),
                Name(std::to_string(id++)) //TODO fix
            };
        arcs.push_back(arc);

        //search up expression stack to find a parent that is a block (size-2 is call's immediate parent)
        int expStackLevel;
        for(expStackLevel = expressionStack.size() - 2; expStackLevel >= 0; expStackLevel--){
            //look for control flow blocks
            if(expressionStack[expStackLevel]->is<Block>()){
                break;
            }
            else if(expressionStack[expStackLevel]->is<If>(){

            }
            else if(expressionStack[expStackLevel]->is<Loop>()
        }
        if(expStackLevel < 0){
            //failed to find suitable block
            std::cout << "Error: could not find block to hoist call expression" << std::endl;
        }

        //find where in the block the current execution is
        Block *blk = expressionStack[expStackLevel]->dynCast<Block>();
        Block *newBlk = new Block(getModule()->allocator);
        int blkLoc;
        for(blkLoc = 0; blkLoc < blk->list.size(); blkLoc++){
            if(blk->list[blkLoc] == expressionStack[expStackLevel + 1]){
                break;
            }
            newBlk->list.push_back(blk->list[blkLoc]);
        }

        //hoist function call into the nearest block just before current execution
        newBlk->list.push_back(createArcCounter(arc.globalCounter));
        newBlk->list.push_back(createArcTimeAccum(arc.globalTimeInTarget));
        //save call return in a local if not void
        Type targetReturn = getModule()->getFunction(curr->target)->result;
        if(targetReturn != Type::none){
            getFunction()->vars.push_back(targetReturn); 
            Index ind = getFunction()->params.size() + getFunction()->vars.size() - 1; //locals are both params and vars
            SetLocal *sl = new SetLocal();
            sl->index = ind;
            sl->value = curr;
            newBlk->list.push_back(sl); //add to hoist block

            GetLocal *gl = new GetLocal();
            gl->index = ind;
            replaceCurrent(gl); //replace where it used to be with getLocal
        }
        else{
            newBlk->list.push_back(curr); //add to hoist block

            Nop *nop = new Nop();
            replaceCurrent(nop); //replace where it used to be with nop
        }

        //fill in rest of the block
        for(blkLoc = blkLoc+1; blkLoc < blk->list.size(); blkLoc++){
            newBlk->list.push_back(blk->list[blkLoc]);
        }

        blk->list.swap(newBlk->list);
    }
};


void addProfFunctions(Module *mod)
{
    //printInt function import
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
    printRes->name = Name("_profPrintResult");
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

    /*
    Export *memOut = new Export();
    memOut->name = "profMem";
    memOut->value = "profMem";
    memOut->kind = ExternalKind::Memory;
    mod->addExport(memOut);
    */
}

void setupProfMemory(Module *mod, int memOffset)
{
    //shift the offset of all existing data definitions
    for(Memory::Segment &s : mod->memory.segments){
        Expression *oldOffset = s.offset;

        //optimization if it is a constant
        if(oldOffset->is<Const>()){
            Const *c = (Const *)oldOffset;
            c->set(Literal(c->value.geti32() + memOffset)); //TODO making assumption about type
        }
        else{
            //TODO not working with non literals
            Binary *newOffset = new Binary();
            newOffset->op = AddInt32; //TODO check type of pointer (iPTR)
            newOffset->left = oldOffset;
            Const *c = new Const();
            c->set(Literal(memOffset));
            newOffset->right = c;

            s.offset = newOffset;
        }
    }

    //TODO add new data segment for prof data at lowest address
}

//write out a js file that declares a json object mapping function id to function name
void writeFuncNameMap(std::ofstream& jsFile)
{
    /*
    jsFile << "profFuncMap = [";
    for(auto const& f : funcIDs){
        jsFile << "{" << f.second << ":\"" << f.first << "\"},"; //id number then name
    }
    jsFile << "{\"error\":\"error\"}];";
    */
    
    //probably a better way to do it
    jsFile << "profFuncMap=[]; ";
    for(auto const& f : funcIDs){
        jsFile << "profFuncMap[" << f.second << "]=\"" << f.first << "\";"; //id number index to name
    }
}

int main(int argc, const char* argv[]) 
{
    if(argc < 2){
        std::cout << "Usage: "<< argv[0] << " <wasm file>" << std::endl;
    }

    Module mod;
    MyVisitor v;
    ModuleReader reader;
    ModuleWriter writer;

    try {
        reader.read(argv[1], mod);
    } catch (ParseException& p) {
        p.dump(std::cerr);
        std::cout << "error in parsing input";
    }

    v.walkModule(&mod);
    addProfFunctions(&mod);
    //setupProfMemory(&mod, MEM_OFFSET_TEMP);

    //std::cout << mod.memory.initial << ", " << mod.memory.max << std::endl;
    //std::cout << mod.memory.segments[0].offset << std::endl;

    //WasmPrinter::printModule(&mod);

    writer.write(mod, "prof_" + std::string(argv[1]));

    //write the accompanying js
    std::ofstream jsFile;
    jsFile.open("prof_" + std::string(argv[1]) + ".js");
    writeFuncNameMap(jsFile);
    jsFile.close();

    return 0;
}