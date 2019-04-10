#include "visitor.h"
#include "ast_gen.h"

using namespace wasm;

//global which tracks which function most recently returned (used for tracking indirect call arcs)
Name lastReturn;
Name lastCaller;

int curFuncID = 1 ;

std::map<wasm::Name, int> funcIDs;

//list of all arcs
std::vector<struct CallPath> arcs;

std::vector<Name> functionImports;

void ProfVisitor::instrument(Module* module)
{
    //populate functionImports
    for(auto & func : module->functions){
        if(func->imported()){
            getOrAddFuncID(func->name);
            functionImports.push_back(func->name);
        }
    }

    lastCaller = createGlobal(module, Type::i32, Literal(0));
    lastReturn = createGlobal(module, Type::i32, Literal(0));

    walkModule(module);
}

//TODONT when returning from an indirect call, will figure out which arc to increment by looking at the value of lastReturn
Expression * ProfVisitor::createIndirectLookup(GetLocal *index)
{
    /*
    Table & table = getModule()->table; 
    for(auto & segment : table.segments){
        for(Name n : segment.data){

        }
    }
    Switch *sw = new Switch(getModule()->allacator);
    sw->condition = index;
    */
}

//create a ast subgraph to increment the given global by 1
Expression * ProfVisitor::createArcCounter(Name globalName)
{
    Binary *b = new Binary();
    b->op = AddInt32;
    b->left = createGetGlobal(globalName);
    b->right = createConst(Literal(1));

    SetGlobal *e = createSetGlobal(globalName, b);

    return e;
}

//Creates a call to the time function and saves time in the local at the given index
Expression * ProfVisitor::createStartTime(Index startTimeLocalIndex)
{
    return createSetLocal(startTimeLocalIndex, createCall(getModule()->allocator, Name("getTime"), 0));
}

//create AST subgraph to increment the time
Expression * ProfVisitor::createArcTimeAccum(Name globalName, Index startTimeLocalIndex)
{
    //get difference between start time and current time
    Binary *timeDiff = new Binary();
    timeDiff->op = SubFloat64;
    timeDiff->left = createCall(getModule()->allocator, Name("getTime"), 0);
    timeDiff->right = createGetLocal(startTimeLocalIndex);

    Binary *accumulate = new Binary();
    accumulate->op = AddFloat64;
    accumulate->left = createGetGlobal(globalName);
    accumulate->right = timeDiff;

    return createSetGlobal(globalName, accumulate);
}

int ProfVisitor::getOrAddFuncID(Name name)
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

/*
 * adds decorator for functions that need it
 * both exported functions (called from JS) and 
 * functions in the function table (called with call_indirect)
 */
void ProfVisitor::addDecorator(Function *curr)
{
    bool exported = false;
    Function *decorator = new Function();
    //generate unique name
    std::string tmpStr = std::string(curr->name.str);
    while(getModule()->getFunctionOrNull(Name(tmpStr))){
        tmpStr += "_";
    }
    Name newName = Name(tmpStr);
    decorator->name = newName;

    //check all exports to see if they export the original function
    for(auto & e : getModule()->exports){
        if(e->value == curr->name)
        {
           //update export to use decorated function call
            e->value = decorator->name;
            exported = true;
            std::cout << "Updated export with decorator: " << decorator->name << std::endl;
        }
    }

    //if not exporeted, don't build this function
    if(!exported){
        delete decorator;
        return;
    }


    //set type same as original
    decorator->result = curr->result;
    decorator->params = curr->params;
    //vars must be a copy because we are going to add vars
    for(int i = 0; i < curr->vars.size(); i++){
        decorator->vars.push_back(curr->vars[i]);
    }

    //create body
    Block *body  = new Block(getModule()->allocator);
    decorator->body = body;
    //add time tracking
    decorator->vars.push_back(Type::f64);
    Index startTimeLocalIndex = decorator->params.size() + decorator->vars.size() - 1;
    body->list.push_back(createStartTime(startTimeLocalIndex));
    //build decorator function
    Call *call = createCall(getModule()->allocator, curr->name, 0);
    for(int i = 0; i < decorator->params.size(); i++){
        call->operands.push_back(createGetLocal(i));
    }
    //save call return if not void
    Return *ret = new Return();
    if(curr->result == Type::none){
        body->list.push_back(call);
    }
    else{
        decorator->vars.push_back(curr->result);
        Index retIndex = decorator->params.size() + decorator->vars.size() - 1;
        body->list.push_back(createSetLocal(retIndex, call));

        ret->value = createGetLocal(retIndex);
    }

    //TODO add time tracking
    //need to figure out where this call came from 
    //it could be any of the imported functions or the first call into wasm
    GetGlobal *getLastCall = createGetGlobal(lastCaller);

    //TODO DEBUG
    body->list.push_back(createCall(getModule()->allocator, Name("printInt"), 1, getLastCall));

    Switch *lookupCaller = new Switch(getModule()->allocator);
    lookupCaller->targets.resize(functionImports.size() + 1);
    lookupCaller->condition = getLastCall;

    Block *curBlock = body;
    for(int i = 0; i < functionImports.size(); i++){
        Function *imported = getModule()->getFunction(functionImports[i]);
        struct CallPath arc = 
        {
            getOrAddFuncID(imported->name),
            getOrAddFuncID(curr->name),
            createGlobal(getModule(), Type::i32, Literal(0)),
            createGlobal(getModule(), Type::f64, Literal(0.0))
        };
        arcs.push_back(arc);

        Block *nextBlock = new Block(getModule()->allocator);
        nextBlock->name = Name::fromInt(i); //TODO unique name
        curBlock->list.push_back(nextBlock);
        curBlock->list.push_back(createArcTimeAccum(arc.globalTimeInTarget, startTimeLocalIndex));
        curBlock->list.push_back(createArcCounter(arc.globalCounter));
        curBlock->list.push_back(ret);

        lookupCaller->targets[getOrAddFuncID(imported->name)] = nextBlock->name;

        curBlock = nextBlock;
    }

    //special case where this is the first call into wasm (lastCaller will be 0)
    //TODO for now I'll lump the default (unknown in with this)
    struct CallPath arc = 
    {
        0,
        getOrAddFuncID(curr->name),
        createGlobal(getModule(), Type::i32, Literal(0)),
        createGlobal(getModule(), Type::f64, Literal(0.0))
    };
    arcs.push_back(arc);

    Block *defaultBlock = new Block(getModule()->allocator);
    defaultBlock->name = Name("defaultSwitch"); //TODO unique name
    defaultBlock->list.push_back(lookupCaller);
    curBlock->list.push_back(defaultBlock);
    curBlock->list.push_back(createArcTimeAccum(arc.globalTimeInTarget, startTimeLocalIndex));
    curBlock->list.push_back(createArcCounter(arc.globalCounter));
    curBlock->list.push_back(ret);

    lookupCaller->targets[0] = defaultBlock->name;
    lookupCaller->default_ = defaultBlock->name;


    //add decorated function to module
    getModule()->addFunction(decorator);
}

void ProfVisitor::visitFunction(Function *curr)
{
    //add this function to the list and assign it an id
    getOrAddFuncID(curr->name);

    if(curr->imported()){
        //std::cout << "import: " << curr->base << " " << curr->module << std::endl;
        return;
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

    //adds a decorator (if required) for this function and replaces any 
    //instances where the decorator should be used instead of the original
    addDecorator(curr);

    //if exported(and our top level function), add data export at the end
    if(getModule()->getExportOrNull(curr->name) && !strcmp(curr->name.c_str(), "_main")){ //TODO figure out which function to make top level
        std::cout << "This function is exported: " << curr->name << std::endl;
        //add call to result function at the end
        Call *result_call = createCall(getModule()->allocator, Name("_profPrintResult"), 0);
        
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

//adds instructions to block to save call's return value if necessary
//returns what the call should be replace with (getLocal if it has a return value and nop if it does not have a return value)
//Expression * ProfVisitor::saveReturn(Block *parentBlk, Expression* currCall, Type returnType)
Expression * ProfVisitor::saveReturn(Block *parentBlk, struct GenericCall *call)
{
    Expression *callReplacement;
    //if return type is none or the call will be dropped anyway, then don't save return
    if(call->returnType == Type::none || getParent()->is<Drop>()){
        parentBlk->list.push_back(call->expression); //add to hoist block
        callReplacement = new Nop();;
    }
    //else save the return value
    else{
        getFunction()->vars.push_back(call->returnType); 
        Index ind = getFunction()->params.size() + getFunction()->vars.size() - 1; //locals are both params and vars
        parentBlk->list.push_back(createSetLocal(ind, call->expression)); //add to hoist block

        callReplacement = createGetLocal(ind);
    }
    return callReplacement;
}

//void ProfVisitor::hoistCallNewBlock(Expression* currCall, Name & target, Type returnType, struct CallPath& arc, Expression **newBlockPtr, int newBlockStackLevel)
void ProfVisitor::hoistCallNewBlock(struct GenericCall *call, Expression **newBlockPtr, int newBlockStackLevel)
{
    Block *newBlk = new Block(getModule()->allocator);

    // //if calling an imported function, set the lastCaller var
    // if(getModule()->getFunction(target)->imported()){
    //     newBlk->list.push_back(createSetGlobal(lastCaller, createConst(Literal(getOrAddFuncID(target)))));
    // }

    // //tracking before function
    // getFunction()->vars.push_back(Type::f64); //add new local for start time
    // Index startTimeLocalIndex = getFunction()->params.size() + getFunction()->vars.size() - 1;
    // newBlk->list.push_back(createStartTime(startTimeLocalIndex));

    //everything that should go before the call
    for(Expression *exp : call->beforeCall){
        newBlk->list.push_back(exp);
    }

    //save call return in a local if not void and add call into block
    Expression *callReplacement = saveReturn(newBlk, call);

    //everything that should go after the call
    for(Expression *exp : call->afterCall){
        newBlk->list.push_back(exp);
    }

    // //tracking after function
    // newBlk->list.push_back(createArcTimeAccum(arc.globalTimeInTarget, startTimeLocalIndex));
    // newBlk->list.push_back(createArcCounter(arc.globalCounter));

    if(*newBlockPtr == call->expression){
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
    pushTask(s.func, &newBlk->list.back()); //push back task with new address of old pointer
    expressionStack.insert(expressionStack.begin()+newBlockStackLevel, newBlk); //fix expression stack
    expressionStack.pop_back(); //must keep stack consitent with number of tasks in queue

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
//void ProfVisitor::hoistCallExistingBlock(Expression* currCall, Name & target, Type returnType, struct CallPath& arc, Block *blk, Expression *firstChild)
void ProfVisitor::hoistCallExistingBlock(struct GenericCall *call, 
    Block *blk, Expression *firstChild)
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

    // //if calling an imported function, set the lastCaller var
    // if(getModule()->getFunction(target)->imported()){
    //     newBlk->list.push_back(createSetGlobal(lastCaller, createConst(Literal(getOrAddFuncID(target)))));
    // }

    // //tracking before function
    // getFunction()->vars.push_back(Type::f64);
    // Index startTimeLocalIndex = getFunction()->params.size() + getFunction()->vars.size() - 1;
    // newBlk->list.push_back(createStartTime(startTimeLocalIndex));

    //everything that should go before the call
    for(Expression *exp : call->beforeCall){
        newBlk->list.push_back(exp);
    }

    //save call return in a local if not void, and add call to block
    Expression *callReplacement = saveReturn(newBlk, call);

    //everything that should go after the call
    for(Expression *exp : call->afterCall){
        newBlk->list.push_back(exp);
    }

    // //tracking after function
    // newBlk->list.push_back(createArcTimeAccum(arc.globalTimeInTarget, startTimeLocalIndex));
    // newBlk->list.push_back(createArcCounter(arc.globalCounter));

    //replace this call with something else since it was moved
    replaceCurrent(callReplacement);

    if(firstChild == call->expression){
        blkLoc++;
    }

    //fill in rest of the block
    for(; blkLoc < blk->list.size(); blkLoc++){
        newBlk->list.push_back(blk->list[blkLoc]);
    }

    blk->list.swap(newBlk->list);
}

//this handles instrumenting both Call and CallIndirect instructions once the target is determined
void ProfVisitor::handleCall(struct GenericCall *genericCall)
{
    // struct CallPath arc = 
    //     {
    //         getOrAddFuncID(getFunction()->name),
    //         getOrAddFuncID(target),
    //         createGlobal(getModule(), Type::i32, Literal(0)),
    //         createGlobal(getModule(), Type::f64, Literal(0.0))
    //     };
    // arcs.push_back(arc);

    //search down expression stack to find a parent that is a block (size-2 is call's immediate parent)
    int expStackLevel;
    for(expStackLevel = expressionStack.size() - 2; expStackLevel >= 0; expStackLevel--){
        //look for control flow blocks
        if(expressionStack[expStackLevel]->is<Block>()){
            hoistCallExistingBlock(genericCall, expressionStack[expStackLevel]->dynCast<Block>(), expressionStack[expStackLevel + 1]);
            return;
        }
        else if(expressionStack[expStackLevel]->is<If>()){
            If *ifExp = expressionStack[expStackLevel]->dynCast<If>();
            //check where in the if
            if(expressionStack[expStackLevel + 1] == ifExp->ifTrue){
                hoistCallNewBlock(genericCall, &ifExp->ifTrue, expStackLevel + 1);
                return;
            }
            else if(expressionStack[expStackLevel + 1] == ifExp->ifFalse){
                hoistCallNewBlock(genericCall, &ifExp->ifFalse, expStackLevel + 1);
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
            hoistCallNewBlock(genericCall, &loopExp->body, expStackLevel + 1);
            return;
        }
    }
    if(expStackLevel < 0){
        //failed to find suitable block or control flow location
        //must replace function body with new block
        hoistCallNewBlock(genericCall, &(getFunction()->body), 0);
    }
}

void ProfVisitor::visitCall(Call *curr)
{
    struct GenericCall genericCall;
    genericCall.expression = curr;
    genericCall.returnType = getModule()->getFunction(curr->target)->result;

    struct CallPath arc = 
        {
            getOrAddFuncID(getFunction()->name),
            getOrAddFuncID(curr->target),
            createGlobal(getModule(), Type::i32, Literal(0)),
            createGlobal(getModule(), Type::f64, Literal(0.0))
        };
    arcs.push_back(arc);

    //setup the timing needed before the call
    if(getModule()->getFunction(curr->target)->imported()){
        genericCall.beforeCall.push_back(createSetGlobal(lastCaller, createConst(Literal(getOrAddFuncID(curr->target)))));
    }
    getFunction()->vars.push_back(Type::f64);
    Index startTimeLocalIndex = getFunction()->params.size() + getFunction()->vars.size() - 1;
    genericCall.beforeCall.push_back(createStartTime(startTimeLocalIndex));

    //setup the timing needed after the call
    genericCall.afterCall.push_back(createArcTimeAccum(arc.globalTimeInTarget, startTimeLocalIndex));
    genericCall.afterCall.push_back(createArcCounter(arc.globalCounter));

    handleCall(&genericCall);
}

void ProfVisitor::visitCallIndirect(CallIndirect *curr)
{
    //TODO determine target of indirect call
    //Name tmp = Name("Indirect");
    //handleCall(curr, tmp, getModule()->getFunctionType(curr->fullType)->result);
}


//want list of indirect functions
void ProfVisitor::visitTable(Table *curr)
{
    std::cout << "Visit table" << std::endl;
    for(auto & s : curr->segments){
        std::cout << "Found segment with data: ";
        for(auto & name : s.data){
            std::cout << name << " ";
        } 
        std::cout << std::endl;
    }
}