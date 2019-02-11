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
int curFuncID = 1 ;
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

    Expression *createStartTime(Index startTimeLocalIndex)
    {
        Call *time = new Call(getModule()->allocator);
        time->target = Name("getTime");

        SetLocal *sl = new SetLocal();
        sl->index = startTimeLocalIndex;
        sl->value = time;

        return sl;
    }

    //create AST subgraph to increment the time
    Expression *createArcTimeAccum(Name globalName, Index startTimeLocalIndex)
    {
        //TODO
        if(!getModule()->getGlobalOrNull(globalName)){
            Global *newGlobal = new Global();
            newGlobal->name = globalName;
            newGlobal->mutable_ = true;
            newGlobal->type = Type::f64;
            Const *c = new Const();
            c->set(Literal(0.0));
            newGlobal->init = c; 
            getModule()->addGlobal(newGlobal); 
        }

        //get difference between start time and current time
        Binary *timeDiff = new Binary();
        timeDiff->op = SubFloat64;
        GetLocal *gl = new GetLocal();
        gl->index = startTimeLocalIndex;
        Call *getTime = new Call(getModule()->allocator);
        getTime->target = Name("getTime");
        timeDiff->left = getTime;
        timeDiff->right = gl;

        Binary *accumulate = new Binary();
        accumulate->op = AddFloat64;
        GetGlobal *gg = new GetGlobal();
        gg->name = globalName;
        accumulate->left = gg;
        accumulate->right = timeDiff;

        SetGlobal *e = new SetGlobal();
        e->name = globalName;
        e->value = accumulate;

        return e;
    }

    int getOrAddFuncID(Name name)
    {
        std::map<Name, int>::iterator it;
        it = funcIDs.find(name);

        //not found, add to map
        if(it == funcIDs.end()){
            funcIDs[name] = curFuncID++;
            return funcIDs[name];
        }

        //found, return id
        return it->second;
    }

    void visitFunction(Function *curr)
    {
        //add this function to the list and assign it an id
        getOrAddFuncID(curr->name);

        if(curr->imported()){
            //std::cout << "import: " << curr->base << " " << curr->module << std::endl;
            return;
        }
        if(!strcmp(curr->name.c_str(), "_memcpy")){
            std::cout << "Found memcpy" << std::endl;
        }

        //if body is a block, just add to it
        Block *funcBlock;
        if(curr->body->is<Block>()){ 
            funcBlock = curr->body->cast<Block>();
        }
        //else create a new block with function body
        else{
            funcBlock = new Block(getModule()->allocator);
            funcBlock->list.push_back(curr->body);
        }
        curr->body = funcBlock;

        //if exported, add data export at the end
        if(getModule()->getExportOrNull(curr->name) && !strcmp(curr->name.c_str(), "_main")){ //TODO figure out which function to make top level
            std::cout << "This function is exported: " << curr->name << std::endl;
            //add call to result function at the end
            Call *result_call = new Call(getModule()->allocator);
            result_call->target = Name("_profPrintResult");
            
            //check if last expression is return and put call in front of it
            Expression *ret = funcBlock->list.back();
            funcBlock->list.pop_back();
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
    
    void hoistCallNewBlock(Call* currCall, struct CallPath& arc, Expression **newBlockPtr, int newBlockStackLevel)
    {
        Block *newBlk = new Block(getModule()->allocator);
        newBlk->list.push_back(createArcCounter(arc.globalCounter));
        getFunction()->vars.push_back(Type::f64); //add new local for start time
        Index startTimeLocalIndex = getFunction()->params.size() + getFunction()->vars.size() - 1;
        newBlk->list.push_back(createStartTime(startTimeLocalIndex));

        Type targetReturn = getModule()->getFunction(currCall->target)->result;
        Expression *callReplacement;
        if(targetReturn != Type::none){
            getFunction()->vars.push_back(targetReturn); 
            Index ind = getFunction()->params.size() + getFunction()->vars.size() - 1; //locals are both params and vars
            SetLocal *sl = new SetLocal();
            sl->index = ind;
            sl->value = currCall;
            newBlk->list.push_back(sl); //add to hoist block

            GetLocal *gl = new GetLocal();
            gl->index = ind;
            callReplacement = gl;
        }
        else{
            newBlk->list.push_back(currCall); //add to hoist block

            Nop *nop = new Nop();
            callReplacement = nop;
        }

        newBlk->list.push_back(createArcTimeAccum(arc.globalTimeInTarget, startTimeLocalIndex));

        if(*newBlockPtr == currCall){
            newBlk->list.push_back(callReplacement);
            replaceCurrent(newBlk);
            return;
        }


        //fix the traversal queue
        std::vector<Task> tmpStack; //tmpStack is in reverse
        auto s = popTask();
        while(*s.currp != *newBlockPtr){
            tmpStack.push_back(s);
            s = popTask();
        }

        newBlk->list.push_back(*newBlockPtr); //add old pointer to new block
        *newBlockPtr = newBlk; //do the swap
        expressionStack.insert(expressionStack.begin()+newBlockStackLevel, newBlk); //fix expression stack
        pushTask(s.func, &newBlk->list.back()); //push back task with new address of old pointer

        //push back the other tasks
        while(tmpStack.size() > 0){
            s = tmpStack.back();
            tmpStack.pop_back();
            pushTask(s.func, s.currp);
        }
        
        replaceCurrent(callReplacement);
    }

    //blk is the block to host the call into
    //first child is the exprssion that contains the call but is a direct child of the block
    void hoistCallExistingBlock(Call* currCall, struct CallPath& arc, Block *blk, Expression *firstChild)
    {
        //find where in the block the current execution is
        Block *newBlk = new Block(getModule()->allocator);
        int blkLoc;
        for(blkLoc = 0; blkLoc < blk->list.size(); blkLoc++){
            if(blk->list[blkLoc] == firstChild){
                break;
            }
            newBlk->list.push_back(blk->list[blkLoc]);
        }

        //hoist function call into the nearest block just before current execution
        newBlk->list.push_back(createArcCounter(arc.globalCounter));
        getFunction()->vars.push_back(Type::f64); //add new local for start time
        Index startTimeLocalIndex = getFunction()->params.size() + getFunction()->vars.size() - 1;
        newBlk->list.push_back(createStartTime(startTimeLocalIndex));
        //save call return in a local if not void
        Type targetReturn = getModule()->getFunction(currCall->target)->result;
        Expression *callReplacement;
        if(targetReturn != Type::none){
            getFunction()->vars.push_back(targetReturn); 
            Index ind = getFunction()->params.size() + getFunction()->vars.size() - 1; //locals are both params and vars
            SetLocal *sl = new SetLocal();
            sl->index = ind;
            sl->value = currCall;
            newBlk->list.push_back(sl); //add to hoist block

            GetLocal *gl = new GetLocal();
            gl->index = ind;
            callReplacement = gl;
        }
        else{
            newBlk->list.push_back(currCall); //add to hoist block

            Nop *nop = new Nop();
            callReplacement = nop;
        }
        newBlk->list.push_back(createArcTimeAccum(arc.globalTimeInTarget, startTimeLocalIndex));
        replaceCurrent(callReplacement);

        if(firstChild == currCall){
            blkLoc++;
        }

        //fill in rest of the block
        for(; blkLoc < blk->list.size(); blkLoc++){
            newBlk->list.push_back(blk->list[blkLoc]);
        }

        blk->list.swap(newBlk->list);
    }

    void visitCall(Call *curr)
    {
        static int id = 1;
        struct CallPath arc = 
            {
                getOrAddFuncID(getFunction()->name),
                getOrAddFuncID(curr->target),
                Name(std::to_string(id++)),
                Name(std::to_string(id++)) //TODO fix
            };
        arcs.push_back(arc);

        //search down expression stack to find a parent that is a block (size-2 is call's immediate parent)
        int expStackLevel;
        for(expStackLevel = expressionStack.size() - 2; expStackLevel >= 0; expStackLevel--){
            //look for control flow blocks
            if(expressionStack[expStackLevel]->is<Block>()){
                hoistCallExistingBlock(curr, arc, expressionStack[expStackLevel]->dynCast<Block>(), expressionStack[expStackLevel + 1]);
                return;
            }
            else if(expressionStack[expStackLevel]->is<If>()){
                If *ifExp = expressionStack[expStackLevel]->dynCast<If>();
                //check where in the if
                if(expressionStack[expStackLevel + 1] == ifExp->ifTrue){
                    hoistCallNewBlock(curr, arc, &ifExp->ifTrue, expStackLevel + 1);
                    return;
                }
                else if(expressionStack[expStackLevel + 1] == ifExp->ifFalse){
                    hoistCallNewBlock(curr, arc, &ifExp->ifFalse, expStackLevel + 1);
                    return;
                }
                else if(expressionStack[expStackLevel + 1] == ifExp->condition){
                    continue;
                }
                else{
                    std::cout << "Error matching expression in If" << std::endl;
                }
            }
            else if(expressionStack[expStackLevel]->is<Loop>()){
                Loop *loopExp = expressionStack[expStackLevel]->dynCast<Loop>();
                hoistCallNewBlock(curr, arc, &loopExp->body, expStackLevel + 1);
                return;
            }
        }
        if(expStackLevel < 0){
            //failed to find suitable block
            std::cout << "Error: could not find block to hoist call expression" << std::endl;
        }
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
        if(arc.srcFuncID == 0){
            std::cout << "Error invalid src ID" << std::endl;
        }
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

    //WasmPrinter::printModule(&mod);

    writer.write(mod, "prof_" + std::string(argv[1]));

    //write the accompanying js
    std::ofstream jsFile;
    jsFile.open("prof_" + std::string(argv[1]) + ".js");
    writeFuncNameMap(jsFile);
    jsFile.close();

    return 0;
}