#ifndef VISITOR_H
#define VISITOR_H

#include "wasm-traversal.h"

#define MAX_GLOBALS 1000000 //set based on v8
#define MAX_LOCALS 50000 //set based on v8

struct CallPath{
    unsigned int srcFuncID;
    unsigned int targetFuncID;
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

//list of all arcs, hash is (src << 32)+dest which makes it unique for every arc
extern std::map<unsigned long, struct CallPath> arcs;
// extern std::vector<struct CallPath> arcs; //DEBUG

//list of all imported functions
extern std::vector<wasm::Name> functionImports;

struct ProfVisitor : public wasm::ExpressionStackWalker<ProfVisitor>
{
    //report counters
    unsigned int regularCallGlobalsUsed = 0;
    unsigned int indirectCallGlobalsUsed = 0;
    unsigned int exportedCallGlobalsUsed = 0;
    unsigned int exportedCallDecoratorsUsed = 0;
    unsigned int indirectCallDecoratorsUsed = 0;
    unsigned int maxLocalsUsedInAFunction = 0;
    
    //flags
    bool dynamicIndirectUpdate = false; //calls to js instead of using LUT for resolving indirect call arcs
    bool dynamicExportUpdate = false; // calls to js instead of using LUT for resolving (js->exported func) arcs
    bool accumulateResults = false; //accumulate results through multiple top level calls into wasm (ie more than one main)
    bool forceDataExport = false;

    //keeps track of the local index used for startTime (there only needs to be one per function)
    //-1 means it has not been set yet
    int currFuncStartTimeIndex = -1;

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
    struct CallPath & getOrAddArc(unsigned int srcID, unsigned int destID, unsigned int *globalCounter);

    void addExportDecorator(wasm::Function *originalFunc);
    void setDynamicIndirectUpdate(bool dynamic);
    void setDynamicExportUpdate(bool dynamic);
    wasm::Function * addDecorator(wasm::Function *originalFunc, wasm::Name decoratorName, std::vector<int> possibleSrcIDs, unsigned int *globalCounter);
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
    void visitBlock(wasm::Block *curr);


    //want map: type -> list of functions
    void visitTable(wasm::Table *curr);

    void visitExport(wasm::Export *curr);

    //report what was added
    void report();
};

#endif