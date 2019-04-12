#include "visitor.h"
#include "ast_gen.h"

//TODO DEBUG
#include "wasm-io.h"

using namespace wasm;

//global which tracks which function most recently returned (used for tracking indirect call arcs)
Name lastReturn;
Name lastCaller;

int curFuncID = 1 ;

std::map<wasm::Name, int> funcIDs;

//list of all arcs
std::vector<struct CallPath> arcs;

std::vector<Name> functionImports;

std::vector<Function *> functionsToAdd;

void ProfVisitor::instrument(Module* module)
{
    //prework that needs to happent to each function
    for(auto & func : module->functions){
        //populate functionImports
        if(func->imported()){
            getOrAddFuncID(func->name);
            functionImports.push_back(func->name);
        }

        //get rid of implicit returns since they cause problems
        if(!func->body->is<Block>()){
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

void ProfVisitor::addExportDecorator(Function *originalFunc)
{
    bool exported = false;
    //generate unique name
    std::string tmpStr = std::string(originalFunc->name.str);
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
            std::cout << "Updated export with decorator: " << decoratorName << std::endl;
        }
    }

    //if exported at least once, then build decorator function
    if(exported){
        std::vector<int> possibleSrcIDs;
        for(Name & import : functionImports){
            possibleSrcIDs.push_back(getOrAddFuncID(import));
        }
        addDecorator(originalFunc, decoratorName, possibleSrcIDs);
    }
}

/*
 * adds decorator for functions that need it
 * both exported functions (called from JS) and 
 * functions in the function table (called with call_indirect)
 */
Function * ProfVisitor::addDecorator(Function *originalFunc, Name decoratorName, std::vector<int> possibleSrcIDs)
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

    //need to figure out where this call came from
    GetLocal *getLastCaller = createGetLocal(lastCallerLocalIndex);

    //TODO DEBUG
    body->list.push_back(createCall(getModule()->allocator, Name("printInt"), 1, getLastCaller));
    body->list.push_back(createCall(getModule()->allocator, Name("printInt"), 1, createConst(Literal(getOrAddFuncID(originalFunc->name)))));

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
        struct CallPath arc = 
        {
            possibleSrcIDs[i],
            getOrAddFuncID(originalFunc->name),
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

        lookupCaller->targets[possibleSrcIDs[i]] = nextBlock->name;

        curBlock = nextBlock;
    }

    //special case where this is the first call into wasm (lastCaller will be 0)
    //TODO for now I'll lump the default (unknown in with this)
    struct CallPath arc = 
    {
        0,
        getOrAddFuncID(originalFunc->name),
        createGlobal(getModule(), Type::i32, Literal(0)),
        createGlobal(getModule(), Type::f64, Literal(0.0))
    };
    arcs.push_back(arc);

    Block *externalBlock = new Block(getModule()->allocator);
    externalBlock->name = Name("externalArc"); //TODO unique name
    curBlock->list.push_back(externalBlock);
    curBlock->list.push_back(createArcTimeAccum(arc.globalTimeInTarget, startTimeLocalIndex));
    curBlock->list.push_back(createArcCounter(arc.globalCounter));
    
    //if the src was zero this was an entry, so print results
    Call *result_call = createCall(getModule()->allocator, Name("_profPrintResultInternal"), 0);
    curBlock->list.push_back(result_call);
    curBlock->list.push_back(ret);

    lookupCaller->targets[0] = externalBlock->name;

    curBlock = externalBlock;

    //error case where we fall through to the default
    Block *defaultBlock = new Block(getModule()->allocator);
    defaultBlock->name = defaultName; //TODO unique name
    defaultBlock->list.push_back(lookupCaller);
    curBlock->list.push_back(defaultBlock);

    //TODO DEBUG
    curBlock->list.push_back(createCall(getModule()->allocator, Name("printInt"), 1, createConst(Literal(0))));
    curBlock->list.push_back(createCall(getModule()->allocator, Name("printInt"), 1, createConst(Literal(0))));
    curBlock->list.push_back(createCall(getModule()->allocator, Name("printInt"), 1, createConst(Literal(0))));
    curBlock->list.push_back(createCall(getModule()->allocator, Name("printInt"), 1, createConst(Literal(0))));

    curBlock->list.push_back(ret);

    lookupCaller->default_ = defaultBlock->name;

    //add decorated function to module
    if(decorator == NULL){
        std::cout << "Error building decorator" << std::endl;
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
    addExportDecorator(curr);
}

//adds instructions to block to save call's return value if necessary
//returns what the call should be replace with (getLocal if it has a return value and nop if it does not have a return value)
Expression * ProfVisitor::saveReturn(Block *parentBlk, struct GenericCall *call)
{
    Expression *callReplacement;
    //if return type is none or the call will be dropped anyway, then don't save return
    if(call->returnType == Type::none || (getParent() && getParent()->is<Drop>())){
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

void ProfVisitor::hoistCallNewBlock(struct GenericCall *call, Expression **newBlockPtr, int newBlockStackLevel)
{
    Block *newBlk = new Block(getModule()->allocator);

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
    //TODO DEBUG
    //WasmPrinter::printModule(getModule());

    //if there is an implicit return, make it explicit to avoid issues
    /*if(getFunction()->result != Type::none && !getFunction()->body->is<Block>()){
        Return *ret = new Return();
        ret->value = getFunction()->body;
        getFunction()->body = ret;
        expressionStack.insert(expressionStack.begin(), ret);
    }*/
    
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

//maps the target type to the possible src functions
std::map<Name, std::vector<int>> indirectSrcIDs;

void ProfVisitor::visitCallIndirect(CallIndirect *curr)
{
    int thisFunctionID = getOrAddFuncID(getFunction()->name);
    struct GenericCall genericCall;
    genericCall.expression = curr;
    genericCall.returnType = getModule()->getFunctionType(curr->fullType)->result; 
    
    genericCall.beforeCall.push_back(createSetGlobal(lastCaller, createConst(Literal(thisFunctionID))));
    
    handleCall(&genericCall);

    //keep track of all call indirects
    indirectSrcIDs[curr->fullType].push_back(thisFunctionID);
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
    //map from a function in table to it's decorator
    std::map<Name, Function *> decoratorMap;
    std::cout << "Visit table" << std::endl;
    for(auto & s : curr->segments){
        std::cout << "Found segment with data: ";
        for(int i = 0; i < s.data.size(); i++){
            Name originalName = s.data[i];
            std::cout << originalName << " ";
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
                std::string tmpStr = std::string(originalFunc->name.str);
                while(getModule()->getFunctionOrNull(Name(tmpStr))){
                    tmpStr += "_";
                }
                Name decoratorName = Name(tmpStr);

                //create actual decorator
                decoratorMap[originalName] = addDecorator(originalFunc, decoratorName, possibleSrcFunctions);

                //change out the name
                s.data[i] = decoratorName;
            }
            else{
                //already have a decorator, just change out the name
                s.data[i] = iter->second->name;
            }
        } 
        std::cout << std::endl;
    }
}