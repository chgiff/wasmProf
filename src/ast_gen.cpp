#include "ast_gen.h"
#include <cstdarg>

Const *createConst(Literal val)
{
    Const *c = new Const();
    c->set(val);
    return c;
}

Call *createCall(MixedArena& allocator, Name target, int numOperands, ...)
{
    va_list args;
    va_start(args, numOperands);

    Call *call = new Call(allocator);
    call->target = target;

    for(int i = 0; i < numOperands; i++){
        Expression *exp = va_arg(args, Expression *);
        call->operands.push_back(exp);
    }

    va_end(args);

    return call;
}

GetGlobal *createGetGlobal(Name name)
{
    GetGlobal *gg = new GetGlobal();
    gg->name = name;
    return gg;
}

SetGlobal *createSetGlobal(Name name, Expression *value)
{
    SetGlobal *sg = new SetGlobal();
    sg->name = name;
    sg->value = value;
    return sg;
}


GetLocal *createGetLocal(int index)
{
    GetLocal *gl = new GetLocal();
    gl->index = index;
    return gl;
}

SetLocal *createSetLocal(int index, Expression *value)
{
    SetLocal *sl = new SetLocal();
    sl->index = index;
    sl->value = value;
    return sl;
}

//creates a global with a unique name and adds it to the module
Name createGlobal(Module *module, Type type, Literal initialValue)
{
    static int uniqueGlobal = 1;
    Name globalName;
    do{
        uniqueGlobal++;
        globalName = Name(std::to_string(uniqueGlobal));
    } while(module->getGlobalOrNull(globalName));
    Global *newGlobal = new Global();
    newGlobal->name = globalName;
    newGlobal->mutable_ = true;
    newGlobal->type = type;
    Const *c = new Const();
    c->set(initialValue);
    newGlobal->init = c; 
    module->addGlobal(newGlobal);

    return globalName;
}