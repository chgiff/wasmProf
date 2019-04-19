#ifndef AST_GEN_H
#define AST_GEN_H

#include "wasm.h"

using namespace wasm;

extern Const *createConst(Literal val);

extern Call *createCall(MixedArena& allocator, Name target, int numOperands, ...);

extern GetGlobal *createGetGlobal(Name name);
extern SetGlobal *createSetGlobal(Name name, Expression *value);

extern GetLocal *createGetLocal(int index);
extern SetLocal *createSetLocal(int index, Expression *value);

extern Name createGlobal(Module *module, Type type, Literal initialValue);
#endif