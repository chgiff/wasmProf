#ifndef VISITOR_H
#define VISITOR_H

#include "wasm-traversal.h"

struct CallPath{
    int srcFuncID;
    int targetFuncID;
    wasm::Name globalCounter;
    wasm::Name globalTimeInTarget; //accumulate amount of time spent in target function
};

//structure so call and call_indirect can be handled similarly
struct GenericCall {
    wasm::Expression *expression;
    wasm::Type returnType;
    std::vector <wasm::Expression *> beforeCall;
    std::vector <wasm::Expression *> afterCall;
};

//give each function a numberic id (zero will signify unknown)
extern std::map<wasm::Name, int> funcIDs;

//list of all arcs
extern std::vector<struct CallPath> arcs;

//list of all imported functions
extern std::vector<wasm::Name> functionImports;

struct ProfVisitor : public wasm::ExpressionStackWalker<ProfVisitor>
{
    void instrument(wasm::Module* module);



    //TODO when returning from an indirect call, will figure out which arc to increment by looking at the value of lastReturn
    wasm::Expression *createIndirectLookup(wasm::GetLocal *index);

    //create a ast subgraph to increment the given global by 1
    wasm::Expression *createArcCounter(wasm::Name globalName);

    //Creates a call to the time function and saves time in the local at the given index
    wasm::Expression *createStartTime(wasm::Index startTimeLocalIndex);

    //create AST subgraph to increment the time
    wasm::Expression *createArcTimeAccum(wasm::Name globalName, wasm::Index startTimeLocalIndex);

    int getOrAddFuncID(wasm::Name name);

    void addExportDecorator(wasm::Function *originalFunc);
    void setDynamicIndirectUpdate(void);
    wasm::Function * addDecorator(wasm::Function *originalFunc, wasm::Name decoratorName, std::vector<int> possibleSrcIDs);
    wasm::Function *addDynamicDecorator(wasm::Function *originalFunc, wasm::Name decoratorName);

    void visitFunction(wasm::Function *curr);

    //adds instructions to block to save call's return value if necessary
    //returns what the call should be replace with (getLocal if it has a return value and nop if it does not have a return value)
    wasm::Expression * saveReturn(wasm::Block *parentBlk, struct GenericCall *call);
    
    void hoistCallNewBlock(struct GenericCall *call, wasm::Expression **newBlockPtr, int newBlockStackLevel);

    //blk is the block to host the call into
    //first child is the exprssion that contains the call but is a direct child of the block
    void hoistCallExistingBlock(struct GenericCall *call, 
    wasm::Block *blk, wasm::Expression *firstChild);

    //this handles instrumenting both Call and CallIndirect instructions
    void handleCall(struct GenericCall *genericCall);

    void visitCall(wasm::Call *curr);

    void visitCallIndirect(wasm::CallIndirect *curr);


    //want map: type -> list of functions
    void visitTable(wasm::Table *curr);

    void visitExport(wasm::Export *curr);
};

#endif