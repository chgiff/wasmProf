#include "wasm-io.h"
#include "wasm-traversal.h"

using namespace wasm;

#define MEM_OFFSET_TEMP 16*1024

Function *createMemCopy()
{
    Function *cpy = new Function();
}

std::vector<Name> countGlobals;

struct CallPath{
    Name srcFunc;
    Name targetFunc;
    Name globalCounter;
};

std::map<Name, struct CallPath> arcs;

struct MyVisitor : public PostWalker<MyVisitor>
{
    //create a ast subgraph to increment the given global by 1
    Expression *createCounter(Name globalName)
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
            countGlobals.push_back(globalName); //internal state of which globals were added 
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

    void visitFunction(Function *curr)
    {
        if(curr->imported()){
            //std::cout << "import: " << curr->base << " " << curr->module << std::endl;
            return;
        }
        else{
            //std::cout << "not import: " << curr->base << " " << curr->module << std::endl;
        }

        if(!strcmp(curr->name.c_str(), "_memcpy")){
            std::cout << "Found memcpy" << std::endl;
        }

        //counter global name (fcnName + "Cnt")
        size_t nameLen = strlen(curr->name.c_str());
        char *globalStr = (char*)malloc(nameLen + 4);
        strcpy(globalStr, curr->name.c_str());
        strcpy(globalStr + nameLen, "Cnt");
        Name globalName = Name(globalStr);

        //if body is a block, just add to it
        Block *funcBlock = new Block(getModule()->allocator);
        funcBlock->list.push_back(createCounter(globalName));
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
        if(getModule()->getExportOrNull(curr->name)){
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
        Block *blk = new Block(getModule()->allocator);

        struct CallPath arc = 
            {
                getFunction()->name,
                curr->target,
                Name(std::to_string(id++))
            };
        arcs.insert(std::make_pair(arc.globalCounter, arc));

        //track this arc
        blk->list.push_back(createCounter(arc.globalCounter));
        blk->list.push_back(curr);

        replaceCurrent(blk);
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

    //print result function
    Function *printRes = new Function();
    printRes->name = Name("_profPrintResult");
    Block *body = new Block(mod->allocator);
    //print each of the function counts
    for(Name n : countGlobals){
        Call *c = new Call(mod->allocator);
        c->target = Name("printInt");
        GetGlobal *g = new GetGlobal();
        g->name = n;
        c->operands.push_back(g);
        body->list.push_back(c);
    }
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

int main(int argc, const char* argv[]) 
{
    if(argc < 2){
        std::cout << "Usage: parsey <wasm file>" << std::endl;
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
    setupProfMemory(&mod, MEM_OFFSET_TEMP);

    std::cout << mod.memory.initial << ", " << mod.memory.max << std::endl;
    std::cout << mod.memory.segments[0].offset << std::endl;

    //WasmPrinter::printModule(&mod);

    writer.write(mod, "prof_" + std::string(argv[1]));

    return 0;
}