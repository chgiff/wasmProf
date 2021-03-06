#include "visitor.h"
#include "ast_gen.h"

//TODO DEBUG
#include "wasm-io.h"

using namespace wasm;

//debug flags to control certain features
#define DEBUG_SKIP_DECORATOR 0
#define DEBUG_SKIP_CALL_TRACKING 0
#define DEBUG_SKIP_INDIRECT_CALL_TRACKING 0
#define DEBUG_SKIP_NEW_BLOCKS 0
#define DEBUG_AVOID_HOISTING_NEW_BLOCKS 0
#define DEBUG_AVOID_HOISTING_EXISTING_BLOCKS 1

//global which tracks which function most recently returned (used for tracking indirect call arcs)
Name lastReturn;
Name lastCaller;

int curFuncID = 1;

//binary function name -> internal function id
std::map<wasm::Name, int> funcIDs;

//internal name -> external name (there can be more than one external name and it just chooses one)
std::map<wasm::Name, wasm::Name> exportMap; 

//list of all arcs, hash is (src << 32)+dest which makes it unique for every arc
std::map<unsigned long, struct CallPath> arcs;
// std::vector<struct CallPath> arcs; //DEBUG

std::vector<Name> functionImports;

std::vector<Function *> functionsToAdd;

//for each block, store it's replacement which will be swapped in
std::map<Block *, Block*> newBlockMap;

void ProfVisitor::instrument(Module* module)
{
    //prework that needs to happen to each function
    for(auto & func : module->functions){
        //populate functionImports
        if(func->imported()){
            getOrAddFuncID(func->name);
            functionImports.push_back(func->name);

            //check if this file was already instrumented by looking for prof imports
            if(func->module == "prof"){
                std::cerr << "It appears this file has already been instrumented" << std::endl;
                exit(1);
            }
        }

        //weird case where function body is just nop
        if(!func->body){
            Block *newBody = new Block(module->allocator);
            func->body = newBody;
        }
        //get rid of implicit returns since they cause problems
        else if(!func->body->is<Block>()){
            Expression *replace = func->body;
            if(func->result != Type::none && !func->body->is<Return>()){
                Return *ret = new Return();
                ret->value = replace;
                replace = ret;
            }
            Block *newBody = new Block(module->allocator);
            newBody->list.push_back(replace);
            func->body = newBody;
        }
    }

    lastCaller = createGlobal(module, Type::i32, Literal(0));
    lastReturn = createGlobal(module, Type::i32, Literal(0));

    walkModule(module);

    for(Function *f : functionsToAdd){
        module->addFunction(f);
    }

    //if there is no names section, use export names for the functions we can
    
    bool hasNames = false;
    for(UserSection & us : module->userSections){
        if(us.name == "name"){
            hasNames = true;
            break;
        }
    }
    if(!hasNames){
        for(const auto &exportName : exportMap){
            std::map<Name, int>::iterator it;
            it = funcIDs.find(exportName.first);
            if(it == funcIDs.end()){
                std::cerr << "Export value error, should not happen" << std::endl;
                exit(1);
            }
            int idTmp = it->second;
            funcIDs.erase(it);
            funcIDs[exportName.second] = idTmp;
        }
    }
    
}

void ProfVisitor::setDynamicIndirectUpdate(bool dynamic)
{
    dynamicIndirectUpdate = dynamic;
}
void ProfVisitor::setDynamicExportUpdate(bool dynamic)
{
    dynamicExportUpdate = dynamic;
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

struct CallPath & ProfVisitor::getOrAddArc(unsigned int srcID, unsigned int destID, unsigned int *globalCounter)
{
    unsigned long hash = srcID;
    hash = (hash << 32) | destID;

    std::map<unsigned long, struct CallPath>::iterator iter = arcs.find(hash);
    if(iter == arcs.end()){
        struct CallPath arc = 
        {
            srcID,
            destID,
            createGlobal(getModule(), Type::i32, Literal(0)),
            createGlobal(getModule(), Type::f64, Literal(0.0))
        };
        *globalCounter += 2;
        arcs[hash] = arc;
    }
    return arcs[hash];
}

void ProfVisitor::addExportDecorator(Function *originalFunc)
{
    if(DEBUG_SKIP_DECORATOR) return;

    bool exported = false;
    //generate unique name
    std::string tmpStr = std::string(originalFunc->name.str) + "_export";
    while(getModule()->getFunctionOrNull(Name(tmpStr))){
        tmpStr += "_";
    }
    Name decoratorName = Name(tmpStr);

    //check all exports to see if they export the original function
    for(auto & e : getModule()->exports){
        if(e->value == originalFunc->name)
        {
           //update export to use decorated function call
            e->value = decoratorName;
            exported = true;
        }
    }

    //if exported at least once, then build decorator function
    if(exported){
        std::vector<int> possibleSrcIDs;
        for(Name & import : functionImports){
            possibleSrcIDs.push_back(getOrAddFuncID(import));
        }
        if(dynamicExportUpdate){
            addDynamicDecorator(originalFunc, decoratorName);
        }
        else{
            addDecorator(originalFunc, decoratorName, possibleSrcIDs, &exportedCallGlobalsUsed);
        }
        exportedCallDecoratorsUsed++; 
    }
}

/*
 * adds decorator for functions that need it
 * both exported functions (called from JS) and 
 * functions in the function table (called with call_indirect)
 */
Function * ProfVisitor::addDecorator(Function *originalFunc, Name decoratorName, std::vector<int> possibleSrcIDs, unsigned int *globalCounter)
{
    Function *decorator = new Function();
    decorator->name = decoratorName;

    //set type same as original
    decorator->result = originalFunc->result;
    decorator->params = originalFunc->params;
    //vars must be a copy because we are going to add vars
    for(int i = 0; i < originalFunc->vars.size(); i++){
        decorator->vars.push_back(originalFunc->vars[i]);
    }

    //create body
    Block *body  = new Block(getModule()->allocator);
    decorator->body = body;

    //save lastCaller locally so it doesn't get overwritten
    decorator->vars.push_back(Type::i32);
    Index lastCallerLocalIndex = decorator->params.size() + decorator->vars.size() - 1;
    SetLocal* setLastCallerLocal = createSetLocal(lastCallerLocalIndex, createGetGlobal(lastCaller));

    //if the last Caller was 0, this was an entry and we may want to clear previous results
    if(!accumulateResults){
        //create if with equality condition lastCaller==0
        Binary *zeroCheck = new Binary();
        zeroCheck->op = EqInt32;
        setLastCallerLocal->type = Type::i32; //this makes it a tee instead of set
        zeroCheck->left = setLastCallerLocal; 
        zeroCheck->right = createConst(Literal(0));

        If* lastCallerIf = new If();
        lastCallerIf->condition = zeroCheck;
        Call *clearResults = new Call(getModule()->allocator);
        clearResults->target = Name("clearResults");
        lastCallerIf->ifTrue = clearResults;
        body->list.push_back(lastCallerIf);
    }
    else{
        body->list.push_back(setLastCallerLocal);
    }

    //add start time tracking
    decorator->vars.push_back(Type::f64);
    Index startTimeLocalIndex = decorator->params.size() + decorator->vars.size() - 1;
    body->list.push_back(createStartTime(startTimeLocalIndex));

    //add call back to original function
    Call *call = createCall(getModule()->allocator, originalFunc->name, 0);
    for(int i = 0; i < decorator->params.size(); i++){
        call->operands.push_back(createGetLocal(i));
    }

    //save call return if not void
    Return *ret = new Return();
    if(originalFunc->result == Type::none){
        body->list.push_back(call);
    }
    else{
        decorator->vars.push_back(originalFunc->result);
        Index retIndex = decorator->params.size() + decorator->vars.size() - 1;
        body->list.push_back(createSetLocal(retIndex, call));

        ret->value = createGetLocal(retIndex);
    }

    //need to figure out where this call came from
    GetLocal *getLastCaller = createGetLocal(lastCallerLocalIndex);

    //TODO DEBUG
    //body->list.push_back(createCall(getModule()->allocator, Name("printInt"), 1, getLastCaller));
    //body->list.push_back(createCall(getModule()->allocator, Name("printInt"), 1, createConst(Literal(getOrAddFuncID(originalFunc->name)))));

    Switch *lookupCaller = new Switch(getModule()->allocator);
    if(possibleSrcIDs.size() > 0) lookupCaller->targets.resize(1 + *std::max_element(possibleSrcIDs.begin(), possibleSrcIDs.end()));
    else lookupCaller->targets.resize(1);
    Name defaultName = Name("defaultSwitch");
    for(int i = 0; i < lookupCaller->targets.size(); i++){
        lookupCaller->targets[i] = defaultName;
    }
    lookupCaller->condition = getLastCaller;

    Block *curBlock = body;
    for(int i = 0; i < possibleSrcIDs.size(); i++){
        struct CallPath & arc = getOrAddArc(possibleSrcIDs[i], getOrAddFuncID(originalFunc->name), globalCounter);

        Block *nextBlock = new Block(getModule()->allocator);
        nextBlock->name = Name::fromInt(i); //TODO unique name
        curBlock->list.push_back(nextBlock);
        curBlock->list.push_back(createArcTimeAccum(arc.globalTimeInTarget, startTimeLocalIndex));
        curBlock->list.push_back(createArcCounter(arc.globalCounter));
        curBlock->list.push_back(ret);

        lookupCaller->targets[possibleSrcIDs[i]] = nextBlock->name;

        curBlock = nextBlock;
    }

    //special case where this is the first call into wasm (lastCaller will be 0)
    //TODO for now I'll lump the default (unknown in with this)
    struct CallPath & arc = getOrAddArc(0, getOrAddFuncID(originalFunc->name), globalCounter);


    Block *externalBlock = new Block(getModule()->allocator);
    externalBlock->name = Name("externalArc"); //TODO unique name
    curBlock->list.push_back(externalBlock);
    curBlock->list.push_back(createArcTimeAccum(arc.globalTimeInTarget, startTimeLocalIndex));
    curBlock->list.push_back(createArcCounter(arc.globalCounter));
    
    //if the src was zero this was an entry, so print results and reset lastCaller
    if(forceDataExport){
        Call *result_call = createCall(getModule()->allocator, Name("exportData"), 0);
        curBlock->list.push_back(result_call);
    }
    curBlock->list.push_back(createSetGlobal(lastCaller, createConst(Literal(0))));
    curBlock->list.push_back(ret);

    lookupCaller->targets[0] = externalBlock->name;

    curBlock = externalBlock;

    //error case where we fall through to the default
    Block *defaultBlock = new Block(getModule()->allocator);
    defaultBlock->name = defaultName; //TODO unique name
    defaultBlock->list.push_back(lookupCaller);
    curBlock->list.push_back(defaultBlock);

    //TODO DEBUG
    //curBlock->list.push_back(createCall(getModule()->allocator, Name("printInt"), 1, createConst(Literal(0))));
    //curBlock->list.push_back(createCall(getModule()->allocator, Name("printInt"), 1, createConst(Literal(0))));
    //curBlock->list.push_back(createCall(getModule()->allocator, Name("printInt"), 1, createConst(Literal(0))));
    //curBlock->list.push_back(createCall(getModule()->allocator, Name("printInt"), 1, createConst(Literal(0))));

    curBlock->list.push_back(ret);

    lookupCaller->default_ = defaultBlock->name;

    //add decorated function to module
    if(decorator == NULL){
        std::cerr << "Error building decorator" << std::endl;
    }
    functionsToAdd.push_back(decorator);

    return decorator;
}

/*
* this decorator calls into the host using addArcData and a count value of one
* instead of doing the lookup through all posible call sources
* should reduce binary bloat on large applications
*/
Function * ProfVisitor::addDynamicDecorator(Function *originalFunc, Name decoratorName)
{
    Function *decorator = new Function();
    decorator->name = decoratorName;

    //set type same as original
    decorator->result = originalFunc->result;
    decorator->params = originalFunc->params;
    //vars must be a copy because we are going to add vars
    for(int i = 0; i < originalFunc->vars.size(); i++){
        decorator->vars.push_back(originalFunc->vars[i]);
    }

    //create body
    Block *body  = new Block(getModule()->allocator);
    decorator->body = body;

    //save lastCaller locally so it doesn't get overwritten
    decorator->vars.push_back(Type::i32);
    Index lastCallerLocalIndex = decorator->params.size() + decorator->vars.size() - 1;
    body->list.push_back(createSetLocal(lastCallerLocalIndex, createGetGlobal(lastCaller)));

    //add start time tracking
    decorator->vars.push_back(Type::f64);
    Index startTimeLocalIndex = decorator->params.size() + decorator->vars.size() - 1;
    body->list.push_back(createStartTime(startTimeLocalIndex));

    //add call back to original function
    Call *call = createCall(getModule()->allocator, originalFunc->name, 0);
    for(int i = 0; i < decorator->params.size(); i++){
        call->operands.push_back(createGetLocal(i));
    }

    //save call return if not void
    Return *ret = new Return();
    if(originalFunc->result == Type::none){
        body->list.push_back(call);
    }
    else{
        decorator->vars.push_back(originalFunc->result);
        Index retIndex = decorator->params.size() + decorator->vars.size() - 1;
        body->list.push_back(createSetLocal(retIndex, call));

        ret->value = createGetLocal(retIndex);
    }

    //add timing code at end
    GetLocal *getLastCaller = createGetLocal(lastCallerLocalIndex);
    Binary *timeDiff = new Binary();
    timeDiff->op = SubFloat64;
    timeDiff->left = createCall(getModule()->allocator, Name("getTime"), 0);
    timeDiff->right = createGetLocal(startTimeLocalIndex);
    body->list.push_back(createCall(getModule()->allocator, Name("addArcData"), 4, getLastCaller, createConst(Literal(getOrAddFuncID(originalFunc->name))), createConst(Literal(1)), timeDiff));


    //TODO DEBUG
    //body->list.push_back(createCall(getModule()->allocator, Name("printInt"), 1, getLastCaller));
    //body->list.push_back(createCall(getModule()->allocator, Name("printInt"), 1, createConst(Literal(getOrAddFuncID(originalFunc->name)))));

    //TODO check for lastCaller==0 but probably only in the exported function case

    body->list.push_back(ret);

    //add decorated function to module
    if(decorator == NULL){
        std::cerr << "Error building decorator" << std::endl;
    }
    //getModule()->addFunction(decorator);
    functionsToAdd.push_back(decorator);

    return decorator;
}

void ProfVisitor::visitFunction(Function *curr)
{
    //add this function to the list and assign it an id
    getOrAddFuncID(curr->name);

    if(curr->imported()){
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

    //DEBUG shift to make room for debug
    // funcBlock->list.push_back(NULL);
    // for(unsigned i = funcBlock->list.size()-1; i > 0; i--){
    //     funcBlock->list[i] = funcBlock->list[i-1];
    // }
    // funcBlock->list[0] = createCall(getModule()->allocator, Name("printInt"), 1, createConst(Literal(getOrAddFuncID(curr->name))));
    //END DEBUG

    //reset the start time index since this is the end of this function
    currFuncStartTimeIndex = -1;

    //adds a decorator (if required) for this function and replaces any 
    //instances where the decorator should be used instead of the original
    addExportDecorator(curr);

    //see how many locals this function uses now
    if(curr->getNumLocals() > maxLocalsUsedInAFunction) maxLocalsUsedInAFunction = curr->getNumLocals();
}

//adds instructions to block to save call's return value if necessary
//returns what the call should be replace with (getLocal if it has a return value and nop if it does not have a return value)
Expression * ProfVisitor::saveReturn(Block *parentBlk, struct GenericCall *call)
{
    Expression *callReplacement;
    //if return type is none or the call will be dropped anyway, then don't save return
    if(call->returnType == Type::none){
        parentBlk->list.push_back(call->expression); //add to hoist block
        callReplacement = new Nop();
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

void ProfVisitor::hoistCallNewBlock(struct GenericCall *call, Expression **newBlockPtr, int newBlockStackLevel)
{
    if(DEBUG_AVOID_HOISTING_NEW_BLOCKS){
        Block *insideBlk = new Block(getModule()->allocator);
        //everything that should go before the call
        for(Expression *exp : call->beforeCall){
            insideBlk->list.push_back(exp);
        }

        Index ind;
        if(call->returnType == wasm::none){
            insideBlk->list.push_back(call->expression);
        }
        else{
            getFunction()->vars.push_back(call->returnType); 
            ind = getFunction()->params.size() + getFunction()->vars.size() - 1; //locals are both params and vars
            insideBlk->list.push_back(createSetLocal(ind, call->expression)); //add to hoist block
        }
        //everything that should go after the call
        for(Expression *exp : call->afterCall){
            insideBlk->list.push_back(exp);
        }
        if(call->returnType != wasm::none){
            insideBlk->list.push_back(createGetLocal(ind));
            insideBlk->type = call->returnType;
        }

        replaceCurrent(insideBlk);

        return;
    }

    Block *newBlk = new Block(getModule()->allocator);
    newBlk->type = (*newBlockPtr)->type;

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

    
    pushTask(doPostVisit, (Expression **) newBlockPtr); //push a task for the new block
    pushTask(s.func, &newBlk->list.back()); //push back task with new address of old pointer
    expressionStack.insert(expressionStack.begin()+newBlockStackLevel, newBlk); //fix expression stack
    //expressionStack.pop_back(); //must keep stack consitent with number of tasks in queue
    //TODO still not sure about this ^^

    //push back the other tasks
    while(tmpStack.size() > 0){
        s = tmpStack.back();
        tmpStack.pop_back();
        pushTask(s.func, s.currp);
    }
    
    replaceCurrent(callReplacement);
}

//origBlock is the block to hoist the call into
//first child is the exprssion that contains the call but is a direct child of the block
void ProfVisitor::hoistCallExistingBlock(struct GenericCall *call, 
    Block *origBlock, Expression *firstChild)
{
    Block *curReplBlock;
    //see if the block already has a potential replacement
    std::map<Block *, Block *>::iterator it;
    it = newBlockMap.find(origBlock);
    if(it == newBlockMap.end()){
        curReplBlock = new Block(getModule()->allocator);
        curReplBlock->name = origBlock->name;
        curReplBlock->type = origBlock->type;
        for(Expression * exp : origBlock->list){
            curReplBlock->list.push_back(exp);
        }
    }
    else{
        curReplBlock = it->second;
    }

    Block *newReplBlock = new Block(getModule()->allocator);
    //locate where the call is in the block
    int blkLoc;
    for(blkLoc = 0; blkLoc < curReplBlock->list.size(); blkLoc++){
        if(curReplBlock->list[blkLoc] == firstChild){
            break;
        }
        newReplBlock->list.push_back(curReplBlock->list[blkLoc]);
    }
    if(blkLoc == curReplBlock->list.size()){
        std::cerr << "Could not locate call in block" << std::endl;
    }

    //case where call is a direct child of block
    if(firstChild == call->expression){
        //stuff that goes before call
        for(Expression *exp : call->beforeCall){
            newReplBlock->list.push_back(exp);
        }

        //call itself
        newReplBlock->list.push_back(firstChild);

        //stuff that goes after call
        for(Expression *exp : call->afterCall){
            newReplBlock->list.push_back(exp);
        }
        blkLoc++; //so call doesn't get added twice
    }
    //debug case
    else if(DEBUG_AVOID_HOISTING_EXISTING_BLOCKS){
        Block *insideBlk = new Block(getModule()->allocator);

        //DEBUG
        // insideBlk->list.push_back(createCall(getModule()->allocator, Name("printInt"), 1, createConst(Literal(10000 + getOrAddFuncID(getFunction()->name)))));

        //everything that should go before the call
        for(Expression *exp : call->beforeCall){
            insideBlk->list.push_back(exp);
        }
        getFunction()->vars.push_back(call->returnType); 
        Index ind = getFunction()->params.size() + getFunction()->vars.size() - 1; //locals are both params and vars
        insideBlk->list.push_back(createSetLocal(ind, call->expression)); //add to hoist block

        //everything that should go after the call
        for(Expression *exp : call->afterCall){
            insideBlk->list.push_back(exp);
        }
        insideBlk->list.push_back(createGetLocal(ind));
        insideBlk->type = call->returnType;
        replaceCurrent(insideBlk);
    }
    //case where call is nested in another expression
    else{
        //DEBUG
        // newReplBlock->list.push_back(createCall(getModule()->allocator, Name("printInt"), 1, createConst(Literal(10000 + getOrAddFuncID(getFunction()->name)))));

        //stuff that goes before call
        for(Expression *exp : call->beforeCall){
            newReplBlock->list.push_back(exp);
        }

        //call itself (with optional save of result)
        Expression *callReplacement = saveReturn(newReplBlock, call);

        //stuff that goes after call
        for(Expression *exp : call->afterCall){
            newReplBlock->list.push_back(exp);
        }
    
        replaceCurrent(callReplacement);
    }

    //fill in the rest of the block
    for(; blkLoc < curReplBlock->list.size(); blkLoc++){
        newReplBlock->list.push_back(curReplBlock->list[blkLoc]);
    }
    newReplBlock->name = curReplBlock->name;
    newReplBlock->type = curReplBlock->type;

    newBlockMap[origBlock] = newReplBlock;
    delete curReplBlock;
}

//this handles instrumenting both Call and CallIndirect instructions once the target is determined
void ProfVisitor::handleCall(struct GenericCall *genericCall)
{
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
                if(DEBUG_SKIP_NEW_BLOCKS) return;
                hoistCallNewBlock(genericCall, &ifExp->ifTrue, expStackLevel + 1);
                return;
            }
            else if(expressionStack[expStackLevel + 1] == ifExp->ifFalse){
                if(DEBUG_SKIP_NEW_BLOCKS) return;
                hoistCallNewBlock(genericCall, &ifExp->ifFalse, expStackLevel + 1);
                return;
            }
            else if(expressionStack[expStackLevel + 1] == ifExp->condition){
                continue;
            }
            else{
                std::cerr << "Error matching expression in If" << std::endl;
                WasmPrinter::printExpression(expressionStack[expStackLevel], std::cout);
                exit(1);
            }
        }
        else if(expressionStack[expStackLevel]->is<Select>()){
            Select *selExp = expressionStack[expStackLevel]->dynCast<Select>();
            //check where in the if
            if(expressionStack[expStackLevel + 1] == selExp->ifTrue){
                if(DEBUG_SKIP_NEW_BLOCKS) return;
                hoistCallNewBlock(genericCall, &selExp->ifTrue, expStackLevel + 1);
                return;
            }
            else if(expressionStack[expStackLevel + 1] == selExp->ifFalse){
                if(DEBUG_SKIP_NEW_BLOCKS) return;
                hoistCallNewBlock(genericCall, &selExp->ifFalse, expStackLevel + 1);
                return;
            }
            else if(expressionStack[expStackLevel + 1] == selExp->condition){
                continue;
            }
            else{
                std::cerr << "Error matching expression in Select" << std::endl;
                WasmPrinter::printExpression(expressionStack[expStackLevel], std::cout);
                exit(1);
            }
        }
        else if(expressionStack[expStackLevel]->is<Loop>()){
            if(DEBUG_SKIP_NEW_BLOCKS) return;
            Loop *loopExp = expressionStack[expStackLevel]->dynCast<Loop>();
            hoistCallNewBlock(genericCall, &loopExp->body, expStackLevel + 1);
            return;
        }
    }
    if(expStackLevel < 0){
        //failed to find suitable block or control flow location
        //must replace function body with new block
        if(DEBUG_SKIP_NEW_BLOCKS) return;
        hoistCallNewBlock(genericCall, &(getFunction()->body), 0);
    }
}

void ProfVisitor::visitCall(Call *curr)
{
    if(DEBUG_SKIP_CALL_TRACKING) return;

    struct GenericCall genericCall;
    genericCall.expression = curr;
    genericCall.returnType = getModule()->getFunction(curr->target)->result;

    struct CallPath & arc = getOrAddArc(getOrAddFuncID(getFunction()->name), getOrAddFuncID(curr->target), &regularCallGlobalsUsed);


    //setup the timing needed before the call
    if(getModule()->getFunction(curr->target)->imported()){
        genericCall.beforeCall.push_back(createSetGlobal(lastCaller, createConst(Literal(getOrAddFuncID(curr->target)))));
    }
    // getFunction()->vars.push_back(Type::f64);
    // Index startTimeLocalIndex = getFunction()->params.size() + getFunction()->vars.size() - 1;
    // genericCall.beforeCall.push_back(createStartTime(startTimeLocalIndex));
    if(currFuncStartTimeIndex == -1){
        getFunction()->vars.push_back(Type::f64);
        currFuncStartTimeIndex = getFunction()->params.size() + getFunction()->vars.size() - 1;
    }
    genericCall.beforeCall.push_back(createStartTime(currFuncStartTimeIndex));

    //setup the timing needed after the call
    // genericCall.afterCall.push_back(createArcTimeAccum(arc.globalTimeInTarget, startTimeLocalIndex));
    genericCall.afterCall.push_back(createArcTimeAccum(arc.globalTimeInTarget, currFuncStartTimeIndex)); //DEBUG
    genericCall.afterCall.push_back(createArcCounter(arc.globalCounter));

    handleCall(&genericCall);
}

//maps the target type to the possible src functions
std::map<Name, std::vector<int>> indirectSrcIDs;

void ProfVisitor::visitCallIndirect(CallIndirect *curr)
{
    if(DEBUG_SKIP_INDIRECT_CALL_TRACKING) return;

    int thisFunctionID = getOrAddFuncID(getFunction()->name);
    struct GenericCall genericCall;
    genericCall.expression = curr;
    genericCall.returnType = getModule()->getFunctionType(curr->fullType)->result; 
    
    genericCall.beforeCall.push_back(createSetGlobal(lastCaller, createConst(Literal(thisFunctionID))));
    
    handleCall(&genericCall);

    //keep track of all call indirects
    indirectSrcIDs[curr->fullType].push_back(thisFunctionID);
}

void ProfVisitor::visitBlock(Block *curr)
{
    std::map<Block *, Block *>::iterator it;
    it = newBlockMap.find(curr);
    if(it == newBlockMap.end()){
        return;
    }

    replaceCurrent(newBlockMap[curr]);
    return;

    // Block *newBlock = new Block(getModule()->allocator);
    // unsigned curPos = 0;
    // for(Block *addBlk : blkList[curr]){
    //     while(curPos < curr->list.size() && addBlk->list.back() != curr->list[curPos]){
    //         newBlock->list.push_back(curr->list[curPos]);
    //         curPos++;
    //     }
    //     // for(unsigned i = 0; i < addBlk->list.size()-1; i++){
    //     //     newBlock->list.push_back(addBlk->list[i]);
    //     // }
    //     addBlk->list.pop_back();
    //     newBlock->list.push_back(addBlk);
    // }

    // while(curPos < curr->list.size()){
    //     newBlock->list.push_back(curr->list[curPos]);
    //     curPos ++;
    // }

    // newBlock->name = curr->name;
    // newBlock->type = curr->type;
    // replaceCurrent(newBlock);
    // //swap instead of replace to maintain other properties of original block
    // // curr->list.swap(newBlock->list);

    // //clear block list
    // blkList.erase(curr);
}

bool typesEqual(FunctionType *type1, FunctionType *type2)
{
    if(type1->result != type2->result) return false;

    if(type1->params.size() != type2->params.size()) return false;

    for(int i = 0; i < type1->params.size(); i++){
        if(type1->params[i] != type2->params[i]) return false;
    }

    return true;
}

bool typesEqual(FunctionType *type1, Function *type2)
{
    if(type1->result != type2->result) return false;

    if(type1->params.size() != type2->params.size()) return false;

    for(int i = 0; i < type1->params.size(); i++){
        if(type1->params[i] != type2->params[i]) return false;
    }

    return true;
}

//want list of indirect functions
void ProfVisitor::visitTable(Table *curr)
{
    if(DEBUG_SKIP_DECORATOR) return;

    //map from a function in table to it's decorator
    std::map<Name, Function *> decoratorMap;
    for(auto & s : curr->segments){
        for(int i = 0; i < s.data.size(); i++){
            Name originalName = s.data[i];
            std::map<Name, Function *>::iterator iter = decoratorMap.find(originalName);
            if(iter == decoratorMap.end()){
                //not in map, create new decorator
                //figure out which function could call this decorator (ie keep track of the call_indirects and do this last)
                Function *originalFunc = getModule()->getFunction(originalName);
                std::vector<int> possibleSrcFunctions;
                for(std::map<Name, std::vector<int>>::iterator it = indirectSrcIDs.begin(); it != indirectSrcIDs.end(); ++it){
                    FunctionType *indirectType = getModule()->getFunctionType(it->first);
                    if(originalFunc->type.str){
                        //function defined with named type
                        FunctionType *origFuncType = getModule()->getFunctionType(originalFunc->type);
                        if(typesEqual(indirectType, origFuncType)) possibleSrcFunctions.insert(possibleSrcFunctions.end(), it->second.begin(), it->second.end());
                    }
                    else{
                        //function defined with implicit type
                        if(typesEqual(indirectType, originalFunc)) possibleSrcFunctions.insert(possibleSrcFunctions.end(), it->second.begin(), it->second.end());
                    }
                }

                //generate unique name
                std::string tmpStr = std::string(originalFunc->name.str) + "_indirect";
                while(getModule()->getFunctionOrNull(Name(tmpStr))){
                    tmpStr += "_";
                }
                Name decoratorName = Name(tmpStr);

                //create actual decorator
                if(dynamicIndirectUpdate){
                    decoratorMap[originalName] = addDynamicDecorator(originalFunc, decoratorName);
                } else{
                    decoratorMap[originalName] = addDecorator(originalFunc, decoratorName, possibleSrcFunctions, &indirectCallGlobalsUsed);
                }
                indirectCallDecoratorsUsed ++;

                //change out the name
                s.data[i] = decoratorName;
            }
            else{
                //already have a decorator, just change out the name
                s.data[i] = iter->second->name;
            }
        } 
    }
}

void ProfVisitor::visitExport(Export *curr)
{
    if(curr->kind == ExternalKind::Function) exportMap[curr->value] = curr->name;
}

void ProfVisitor::report()
{
    std::cout << "Usage report:" << std::endl;
    std::cout << "Globals used for regular calls: " << regularCallGlobalsUsed << std::endl;
    std::cout << "Globals used for calls to exports: " << exportedCallGlobalsUsed << std::endl;
    std::cout << "Globals used for indirect calls: " << indirectCallGlobalsUsed << std::endl;
    std::cout << "Decorators added for exported functions: " << exportedCallDecoratorsUsed << std::endl;
    std::cout << "Decorators added for indirect functions: " << indirectCallDecoratorsUsed << std::endl;
    std::cout << "The max number of locals used in a function: " << maxLocalsUsedInAFunction << std::endl;
}