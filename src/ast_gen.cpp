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

Load *createLoad(Type type, char bytes, bool sign, int offset, Expression *ptr)
{
    Load *ld = new Load();
    ld->type = type;
    ld->bytes = bytes;
    ld->signed_ = sign;
    ld->isAtomic = false;
    ld->ptr = ptr;
    ld->offset = offset;
    return ld;
}

Function *wasi_getTime(Module *module)
{
    Function *getTimeFunc = new Function();
    getTimeFunc->result = Type::f64;
    Block *body = new Block(module->allocator);
    getTimeFunc->body = body;
    getTimeFunc->result = Type::f64;
    getTimeFunc->vars.push_back(Type::i64);
    getTimeFunc->vars.push_back(Type::i64);
    getTimeFunc->vars.push_back(Type::f64);

    //first save the memory we will use into globals
    body->list.push_back(createSetLocal(0, createLoad(Type::i64, 8, false, 0, createConst(Literal(0)))));
    body->list.push_back(createSetLocal(1, createLoad(Type::i64, 8, false, 8, createConst(Literal(0)))));

    //call the wasi_clock_time_get
    Drop *drop = new Drop();
    drop->value = createCall(module->allocator, Name("clock_time_get"), 3, 
        createConst(Literal(0)),
        createConst(Literal((uint64_t)0)),
        createConst(Literal(0)));
    body->list.push_back(drop);
    
    //convert ns to float and divide by 1000000000
    Unary *convertNs = new Unary();
    convertNs->op = ConvertUInt64ToFloat64;
    convertNs->value = createLoad(Type::i64, 8, false, 0, createConst(Literal(0)));
    Binary *divNs = new Binary();
    divNs->op = DivFloat64;
    divNs->left = convertNs;
    divNs->right = createConst(Literal(1000000000.0));

    //load seconds field
    Unary *convertSec = new Unary();
    convertSec->op = ConvertUInt32ToFloat64;
    convertSec->value = createLoad(Type::i32, 4, false, 8, createConst(Literal(0)));

    //add together, and save in local
    Binary *add = new Binary();
    add->op = AddFloat64;
    add->left = divNs;
    add->right = convertSec;
    body->list.push_back(createSetLocal(2, add));

    //put saved values back in memory
    Store *s1 = new Store();
    s1->bytes = 8;
    s1->isAtomic = false;
    s1->ptr = createConst(Literal(0));
    s1->value = createGetLocal(0);
    s1->valueType = Type::i64;
    Store *s2 = new Store();
    s2->bytes = 8;
    s2->isAtomic = false;
    s2->offset = 8;
    s2->ptr = createConst(Literal(0));
    s2->value = createGetLocal(1);
    s2->valueType = Type::i64;
    body->list.push_back(s1);
    body->list.push_back(s2);
    
    Return * ret = new Return();
    ret->value = createGetLocal(2);
    body->list.push_back(ret);

    return getTimeFunc;
}