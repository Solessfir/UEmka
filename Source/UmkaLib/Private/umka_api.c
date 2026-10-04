#define __USE_MINGW_ANSI_STDIO 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "umka_compiler.h"
#include "umka_api.h"

#define UMKA_VERSION    "1.5.7"


static void compileWarning(Umka *umka, const DebugInfo *debug, const char *format, ...)
{
    va_list args;
    va_start(args, format);

    UmkaError report = {0};
    errorReportInit(&report, &umka->storage,
                    debug ? debug->fileName : umka->lex.fileName,
                    debug ? debug->fnName : umka->debug.fnName,
                    debug ? debug->line : umka->lex.tok.line,
                    debug ? 1 : umka->lex.tok.pos,
                    0,
                    format, args);

    if (umka->error.warningCallback)
        ((UmkaWarningCallback)umka->error.warningCallback)(&report);

    va_end(args);
}


static void compileError(Umka *umka, const char *format, ...)
{
    va_list args;
    va_start(args, format);

    errorReportInit(&umka->error.report, &umka->storage, umka->lex.fileName, umka->debug.fnName, umka->lex.tok.line, umka->lex.tok.pos, 1, format, args);

    vmKill(&umka->vm);

    va_end(args);
    longjmp(umka->error.jumper, 1);
}


static void runtimeError(Umka *umka, int code, const char *format, ...)
{
    va_list args;
    va_start(args, format);

    const Fiber *fiber = umka->vm.fiber;
    const DebugInfo *debug = fiber && fiber->debugPerInstr && fiber->ip >= 0 && fiber->ip < umka->gen.ip ? &fiber->debugPerInstr[fiber->ip] : NULL;
    errorReportInit(&umka->error.report, &umka->storage, debug ? debug->fileName : umka->lex.fileName, debug ? debug->fnName : "<native API>", debug ? debug->line : 0, 1, code, format, args);

    vmKill(&umka->vm);

    va_end(args);
    longjmp(umka->error.jumper, 1);
}


// API functions

UMKA_API Umka *umkaAlloc(void)
{
    return malloc(sizeof(Umka));
}


UMKA_API bool umkaInit(Umka *umka, const char *fileName, const char *sourceString, int stackSize, void *reserved, int argc, char **argv, bool fileSystemEnabled, bool implLibsEnabled, UmkaWarningCallback warningCallback)
{
    memset(umka, 0, sizeof(Umka));

    // First set error handlers
    umka->error.handler = compileError;
    umka->error.runtimeHandler = runtimeError;
    umka->error.warningHandler = compileWarning;
    umka->error.warningCallback = warningCallback;
    umka->error.context = umka;

    if (setjmp(umka->error.jumper) == 0)
    {
        compilerInit(umka, fileName, sourceString, stackSize, argc, argv, fileSystemEnabled, implLibsEnabled);
        return true;
    }
    return false;
}


UMKA_API bool umkaCompile(Umka *umka)
{
    if (setjmp(umka->error.jumper) == 0)
    {
        compilerCompile(umka);
        return true;
    }
    return false;
}


UMKA_API int umkaRun(Umka *umka)
{
    const int previousNesting = umka->error.jumperNesting;
    if (previousNesting == 0)
    {
        umka->vm.instructionsRemaining = umka->vm.maxInstructions;
        umka->vm.cancelPoll = 0;
    }
    jmp_buf dummyJumper;
    jmp_buf *jumper = previousNesting == 0 ? &umka->error.jumper : &dummyJumper;
    if (setjmp(*jumper) == 0)
    {
        umka->error.jumperNesting++;
        compilerRun(umka);
        umka->error.jumperNesting = previousNesting;
        return 0;
    }

    umka->error.jumperNesting = previousNesting;
    if (previousNesting == 0)
        umka->vm.callNesting = 0;
    return umka->error.report.code;
}


UMKA_API int umkaCall(Umka *umka, UmkaFuncContext *fn)
{
    const int previousNesting = umka->error.jumperNesting;
    if (previousNesting == 0)
    {
        umka->vm.instructionsRemaining = umka->vm.maxInstructions;
        umka->vm.cancelPoll = 0;
    }
    // Nested calls to umkaCall() should not reset the error jumper
    jmp_buf dummyJumper;
    jmp_buf *jumper = previousNesting == 0 ? &umka->error.jumper : &dummyJumper;

    if (setjmp(*jumper) == 0)
    {
        umka->error.jumperNesting++;
        compilerCall(umka, fn);
        umka->error.jumperNesting = previousNesting;
        return 0;
    }

    umka->error.jumperNesting = previousNesting;
    if (previousNesting == 0)
        umka->vm.callNesting = 0;
    return umka->error.report.code;
}


UMKA_API void umkaSetExecutionBudget(Umka *umka, uint64_t maxInstructions, UmkaCancelCallback callback, void *userData)
{
    if (!umka)
        return;

    umka->vm.maxInstructions = maxInstructions;
    umka->vm.cancelCallback = callback;
    umka->vm.cancelUserData = userData;
}


UMKA_API void umkaFree(Umka *umka)
{
    compilerFree(umka);
    free(umka);
}


UMKA_API UmkaError *umkaGetError(Umka *umka)
{
    return &umka->error.report;
}


UMKA_API bool umkaAlive(Umka *umka)
{
    return vmAlive(&umka->vm);
}


UMKA_API char *umkaAsm(Umka *umka)
{
    return compilerAsm(umka);
}


UMKA_API bool umkaAddModule(Umka *umka, const char *fileName, const char *sourceString)
{
    if (!umka || !fileName || !sourceString)
        return false;
    if (setjmp(umka->error.jumper) == 0)
        return compilerAddModule(umka, fileName, sourceString);
    return false;
}


UMKA_API void umkaSetFileImportsEnabled(Umka *umka, bool enabled)
{
    if (umka)
        umka->modules.fileImportsEnabled = enabled;
}


UMKA_API bool umkaAddFunc(Umka *umka, const char *name, UmkaExternFunc func)
{
    return compilerAddClosure(umka, name, func, NULL);
}


UMKA_API bool umkaGetFunc(Umka *umka, const char *moduleName, const char *fnName, UmkaFuncContext *fn)
{
    return compilerGetFunc(umka, moduleName, fnName, fn);
}


UMKA_API bool umkaGetCallStack(Umka *umka, int depth, int nameSize, int *offset, char *fileName, char *fnName, int *line)
{
    if (!umka || depth < 0 || !umka->vm.fiber || !umka->vm.fiber->debugPerInstr || ((fileName || fnName) && nameSize <= 0))
        return false;

    const Fiber *fiber = umka->vm.fiber;
    const Slot *base = fiber->base;
    int ip = fiber->ip;
    if (ip < 0 || ip >= umka->gen.ip)
        return false;

    while (depth-- > 0)
        if (!vmUnwindCallStack(&umka->vm, &base, &ip))
            return false;

    if (ip < 0 || ip >= umka->gen.ip)
        return false;

    if (offset)
        *offset = ip;

    if (fileName)
        snprintf(fileName, nameSize, "%s", fiber->debugPerInstr[ip].fileName);

    if (fnName)
        snprintf(fnName, nameSize, "%s", fiber->debugPerInstr[ip].fnName);

    if (line)
        *line = fiber->debugPerInstr[ip].line;

    return true;
}


UMKA_API void umkaSetHook(Umka *umka, UmkaHookEvent event, UmkaHookFunc hook)
{
    vmSetHook(&umka->vm, event, hook);
}


UMKA_API void *umkaAllocData(Umka *umka, int size, UmkaExternFunc onFree)
{
    return vmAllocData(&umka->vm, size, onFree);
}


UMKA_API void umkaIncRef(Umka *umka, void *ptr)
{
    vmIncRef(&umka->vm, ptr, umka->types.predecl.ptrVoidType);    // We have no actual type info provided by the user, so we can only rely on the type info from the heap chunk header, if any
}


UMKA_API void umkaDecRef(Umka *umka, void *ptr)
{
    vmDecRef(&umka->vm, ptr, umka->types.predecl.ptrVoidType);    // We have no actual type info provided by the user, so we can only rely on the type info from the heap chunk header, if any
}


UMKA_API void *umkaGetMapItem(Umka *umka, UmkaMap *map, UmkaStackSlot key)
{
    const Slot *keyPtr = (Slot *)&key;
    return vmGetMapNodeData(&umka->vm, (Map *)map, *keyPtr);
}


UMKA_API char *umkaMakeStr(Umka *umka, const char *str)
{
    return vmMakeStr(&umka->vm, str);
}


UMKA_API int umkaGetStrLen(const char *str)
{
    if (!str)
        return 0;
    return getStrDims(str)->len;
}


UMKA_API void umkaMakeDynArray(Umka *umka, void *array, const UmkaType *type, int len)
{
    vmMakeDynArray(&umka->vm, (DynArray *)array, type, len);
}


UMKA_API int umkaGetDynArrayLen(const void *array)
{
    const DynArray *dynArray = (const DynArray *)array;
    if (!dynArray->data)
        return 0;
    return getDims(dynArray)->len;
}


UMKA_API const char *umkaGetVersion(void)
{
    if (sizeof(void *) == 8)
        return "Umka "UMKA_VERSION" ("__DATE__" "__TIME__" 64 bit)";
    else if (sizeof(void *) == 4)
        return "Umka "UMKA_VERSION" ("__DATE__" "__TIME__" 32 bit)";
    else
        return "Umka "UMKA_VERSION" ("__DATE__" "__TIME__")";
}


UMKA_API int64_t umkaGetMemUsage(Umka *umka)
{
    return vmGetMemUsage(&umka->vm);
}


UMKA_API void umkaMakeFuncContext(Umka *umka, const UmkaType *closureType, int entryOffset, UmkaFuncContext *fn)
{
    compilerMakeFuncContext(umka, closureType->field[0]->type, entryOffset, fn);
}


UMKA_API UmkaStackSlot *umkaGetParam(UmkaStackSlot *params, int index)
{
    const ParamLayout *paramLayout = getParamLayout(*vmGetStackFrameLayout(params));
    if (index < 0 || index >= paramLayout->numParams - paramLayout->numResultParams - 1)
        return NULL;
    return params + paramLayout->firstSlotIndex[index + 1];                                                 // + 1 to skip upvalues
}


UMKA_API UmkaAny *umkaGetUpvalue(UmkaStackSlot *params)
{
    const ParamLayout *paramLayout = getParamLayout(*vmGetStackFrameLayout(params));
    return (UmkaAny *)(params + paramLayout->firstSlotIndex[0]);
}


UMKA_API UmkaStackSlot *umkaGetResult(UmkaStackSlot *params, UmkaStackSlot *result)
{
    const ParamLayout *paramLayout = getParamLayout(*vmGetStackFrameLayout(params));
    if (paramLayout->numResultParams == 1)
        result->ptrVal = params[paramLayout->firstSlotIndex[paramLayout->numParams - 1]].ptrVal;
    return result;
}


UMKA_API void *umkaGetMetadata(Umka *umka)
{
    return umka->metadata;
}


UMKA_API void umkaSetMetadata(Umka *umka, void *metadata)
{
    umka->metadata = metadata;
}


UMKA_API void *umkaMakeStruct(Umka *umka, const UmkaType *type)
{
    return vmMakeStruct(&umka->vm, type);
}


UMKA_API const UmkaType *umkaGetBaseType(const UmkaType *type) 
{
    if (type->kind == TYPE_PTR || type->kind == TYPE_WEAKPTR || type->kind == TYPE_ARRAY || type->kind == TYPE_DYNARRAY)
        return type->base;
    return NULL;
}


UMKA_API const UmkaType *umkaGetParamType(UmkaStackSlot *params, int index)
{
    const StackFrameLayout *layout = *vmGetStackFrameLayout(params);
    const ParamLayout *paramLayout = getParamLayout(layout);
    if (index < 0 || index >= paramLayout->numParams - paramLayout->numResultParams - 1)
        return NULL;
    return getParamTypes(layout)->paramType[index + 1]; 
}


UMKA_API const UmkaType *umkaGetResultType(UmkaStackSlot *params, UmkaStackSlot *result)
{
    const StackFrameLayout *layout = *vmGetStackFrameLayout(params);
    return getParamTypes(layout)->resultType;
}


UMKA_API const UmkaType *umkaGetFieldType(const UmkaType *structType, const char *fieldName)
{
    if (structType->kind == TYPE_STRUCT)
    {
        const Field *field = typeFindField(structType, fieldName, NULL);
        if (field)
            return field->type;
    }
    return NULL;
}


UMKA_API const UmkaType *umkaGetMapKeyType(const UmkaType *mapType)
{
    if (mapType->kind == TYPE_MAP)
        return typeMapKey(mapType);
    return NULL;
}


UMKA_API const UmkaType *umkaGetMapItemType(const UmkaType *mapType)
{
    if (mapType->kind == TYPE_MAP)
        return typeMapItem(mapType);
    return NULL;
}


UMKA_API bool umkaIsStaticArrayType(const UmkaType *type)
{
    return type && type->kind == TYPE_ARRAY;
}


UMKA_API bool umkaIsDynArrayType(const UmkaType *type)
{
    return type && type->kind == TYPE_DYNARRAY;
}


UMKA_API int umkaGetTypeSize(const UmkaType *type)
{
    return type ? type->size : -1;
}


UMKA_API int umkaGetArrayLen(const UmkaType *arrayType)
{
    return arrayType && arrayType->kind == TYPE_ARRAY ? arrayType->numItems : -1;
}


UMKA_API int umkaGetFieldCount(const UmkaType *structType)
{
    return structType && structType->kind == TYPE_STRUCT ? structType->numItems : -1;
}


UMKA_API const UmkaType *umkaGetFieldTypeByIndex(const UmkaType *structType, int index)
{
    if (!structType || structType->kind != TYPE_STRUCT || index < 0 || index >= structType->numItems)
        return NULL;
    return structType->field[index]->type;
}


UMKA_API int umkaGetFieldOffsetByIndex(const UmkaType *structType, int index)
{
    if (!structType || structType->kind != TYPE_STRUCT || index < 0 || index >= structType->numItems)
        return -1;
    return structType->field[index]->offset;
}


UMKA_API const UmkaType *umkaGetFuncType(Umka *umka, const char *moduleName, const char *fnName)
{
    if (!umka || !fnName)
        return NULL;

    int module = 1;
    if (moduleName)
    {
        char modulePath[DEFAULT_STR_LEN + 1] = "";
        moduleAssertRegularizePath(&umka->modules, moduleName, umka->modules.curFolder, modulePath, DEFAULT_STR_LEN + 1);
        module = moduleFind(&umka->modules, modulePath);
    }

    const Ident *fnIdent = identFind(&umka->idents, &umka->modules, &umka->blocks, module, fnName, NULL, false);
    if (!fnIdent || fnIdent->kind != IDENT_CONST || fnIdent->type->kind != TYPE_FN)
        return NULL;
    return fnIdent->type;
}


static int getFuncParamCount(const UmkaType *fnType)
{
    if (!fnType || fnType->kind != TYPE_FN || !fnType->sig || !fnType->sig->resultType)
        return -1;
    const int count = fnType->sig->numParams - 1 - (typeStructured(fnType->sig->resultType) ? 1 : 0);
    return count >= 0 ? count : -1;
}


UMKA_API int umkaGetFuncParamCount(const UmkaType *fnType)
{
    return getFuncParamCount(fnType);
}


UMKA_API const char *umkaGetFuncParamNameByIndex(const UmkaType *fnType, int index)
{
    if (index < 0 || index >= getFuncParamCount(fnType))
        return NULL;
    return fnType->sig->param[index + 1]->name;
}


UMKA_API const UmkaType *umkaGetFuncParamTypeByIndex(const UmkaType *fnType, int index)
{
    if (index < 0 || index >= getFuncParamCount(fnType))
        return NULL;
    return fnType->sig->param[index + 1]->type;
}


UMKA_API const UmkaType *umkaGetFuncResultType(const UmkaType *fnType)
{
    return fnType && fnType->kind == TYPE_FN && fnType->sig ? fnType->sig->resultType : NULL;
}


UMKA_API const char *umkaGetTypeKindName(const UmkaType *type)
{
    return type ? typeKindSpelling(type->kind) : NULL;
}


UMKA_API const char *umkaGetTypeName(const UmkaType *type)
{
    return type && type->typeIdent ? type->typeIdent->name : NULL;
}


UMKA_API const char *umkaGetTypeNameInMainModule(Umka *umka, const UmkaType *type)
{
    if (!umka || !type || !type->typeIdent || umka->modules.numModules <= 1)
        return NULL;

    const Ident *ident = type->typeIdent;
    if (ident->block == 0 && ident->module <= 1)
        return ident->name;

    const char *alias = ident->module >= 0 && ident->module < umka->modules.numModules
        ? umka->modules.module[1]->importAlias[ident->module] : NULL;
    if (ident->block != 0 || !ident->isExported || !alias)
    {
        ident = NULL;
        for (const Ident *candidate = umka->idents.first; candidate; candidate = candidate->next)
        {
            if (candidate->kind != IDENT_TYPE || candidate->block != 0 || !typeSameExceptMaybeIdent(candidate->type, type))
                continue;
            if (candidate->module <= 1)
                return candidate->name;
            const char *candidateAlias = umka->modules.module[1]->importAlias[candidate->module];
            if (candidate->isExported && candidateAlias)
            {
                ident = candidate;
                alias = candidateAlias;
                break;
            }
        }
        if (!ident)
            return NULL;
    }

    const size_t len = strlen(alias) + strlen(ident->name) + 3;
    char *name = storageAdd(&umka->storage, len);
    snprintf(name, len, "%s::%s", alias, ident->name);
    return name;
}


UMKA_API const char *umkaGetTypeModulePath(Umka *umka, const UmkaType *type)
{
    if (!umka || !type || !type->typeIdent)
        return NULL;

    const int module = type->typeIdent->module;
    if (module < 0 || module >= umka->modules.numModules || !umka->modules.module[module])
        return NULL;

    return umka->modules.module[module]->path;
}


UMKA_API bool umkaTypeSameDeclaration(Umka *umka, const UmkaType *type, const char *modulePath, const char *typeName)
{
    if (!umka || !type || !type->sameAs || !modulePath || !typeName)
        return false;

    char path[DEFAULT_STR_LEN + 1] = "";
    if (!moduleRegularizePath(&umka->modules, modulePath, umka->modules.curFolder, path, sizeof(path)))
        return false;

    const int module = moduleFind(&umka->modules, path);
    if (module < 0)
        return false;

    for (const Ident *ident = umka->idents.first; ident; ident = ident->next)
        if (ident->kind == IDENT_TYPE && ident->block == 0 && ident->module == module && strcmp(ident->name, typeName) == 0)
            return typeSameExceptMaybeIdent(type, ident->type);

    return false;
}


UMKA_API bool umkaIsEnumType(const UmkaType *type)
{
    return type && typeEnum(type);
}


UMKA_API bool umkaIsExprListType(const UmkaType *type)
{
    return type && typeExprListStruct(type);
}


UMKA_API const char *umkaGetFieldNameByIndex(const UmkaType *structType, int index)
{
    if (!structType || structType->kind != TYPE_STRUCT || index < 0 || index >= structType->numItems)
        return NULL;
    return structType->field[index]->name;
}


UMKA_API bool umkaGetFuncParamDefaultValue(const UmkaType *fnType, int index, UmkaStackSlot *value)
{
    const int count = getFuncParamCount(fnType);
    if (!value || index < 0 || index >= count || index < count - fnType->sig->numDefaultParams)
        return false;

    memcpy(value, &fnType->sig->param[index + 1]->defaultVal, sizeof(*value));
    return true;
}


static void nativeMapRuntimeError(Umka *umka, int code, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    // Outside a VM call, the fiber instruction pointer may already be past its debug information.
    errorReportInit(&umka->error.report, &umka->storage, umka->lex.fileName, "<native map API>", 0, 0, code, format, args);
    vmKill(&umka->vm);
    va_end(args);
    longjmp(umka->error.jumper, 1);
}


UMKA_API void umkaMakeMap(Umka *umka, UmkaMap *map, const UmkaType *type)
{
    if (!umka || !map || !type || type->kind != TYPE_MAP ||
        (map->root && (!map->type || map->type->kind != TYPE_MAP)) || !umkaAlive(umka))
        return;

    const bool ownsJumper = umka->error.jumperNesting == 0;
    void (*runtimeHandler)(Umka *, int, const char *, ...) = umka->error.runtimeHandler;
    if (ownsJumper && setjmp(umka->error.jumper) != 0)
    {
        umka->error.jumperNesting = 0;
        umka->error.runtimeHandler = runtimeHandler;
        return;
    }

    if (ownsJumper)
    {
        umka->error.jumperNesting++;
        umka->error.runtimeHandler = nativeMapRuntimeError;
    }
    vmMakeMap(&umka->vm, (Map *)map, type);
    if (ownsJumper)
    {
        umka->error.jumperNesting--;
        umka->error.runtimeHandler = runtimeHandler;
    }
}


UMKA_API void *umkaEnsureMapItem(Umka *umka, UmkaMap *map, UmkaStackSlot key)
{
    if (!umka || !map || !map->type || map->type->kind != TYPE_MAP || !umkaAlive(umka))
        return NULL;

    const bool ownsJumper = umka->error.jumperNesting == 0;
    void (*runtimeHandler)(Umka *, int, const char *, ...) = umka->error.runtimeHandler;
    if (ownsJumper && setjmp(umka->error.jumper) != 0)
    {
        umka->error.jumperNesting = 0;
        umka->error.runtimeHandler = runtimeHandler;
        return NULL;
    }

    Slot keySlot;
    memcpy(&keySlot, &key, sizeof(keySlot));
    if (ownsJumper)
    {
        umka->error.jumperNesting++;
        umka->error.runtimeHandler = nativeMapRuntimeError;
    }
    void *value = vmEnsureMapNodeData(&umka->vm, (Map *)map, keySlot);
    if (ownsJumper)
    {
        umka->error.jumperNesting--;
        umka->error.runtimeHandler = runtimeHandler;
    }
    return value;
}


UMKA_API int umkaGetMapLen(const UmkaMap *map)
{
    if (!map || !map->root)
        return 0;
    if (!map->type || map->type->kind != TYPE_MAP)
        return -1;
    return map->root->len;
}


UMKA_API bool umkaVisitMap(const UmkaMap *map, UmkaMapVisitor visitor, void *user)
{
    if (!map || !visitor)
        return false;
    if (!map->root)
        return true;
    if (!map->type || map->type->kind != TYPE_MAP)
        return false;

    const Type *keyType = typeMapKey(map->type);
    const MapNode *node = map->root;
    const MapNode **stack = NULL;
    size_t count = 0, capacity = 0;
    bool complete = false;
    while (node || count)
    {
        while (node)
        {
            if (count == capacity)
            {
                if (capacity > SIZE_MAX / sizeof(*stack) / 2)
                    goto cleanup;
                const size_t newCapacity = capacity ? capacity * 2 : 64;
                const MapNode **newStack = realloc(stack, newCapacity * sizeof(*stack));
                if (!newStack)
                    goto cleanup;
                stack = newStack;
                capacity = newCapacity;
            }
            stack[count++] = node;
            node = node->left;
        }

        node = stack[--count];
        if (node->key && node->data)
        {
            Const key = {.ptrVal = node->key};
            if (!constDeref(NULL, &key, keyType->kind))
                goto cleanup;
            UmkaStackSlot keySlot;
            memcpy(&keySlot, &key, sizeof(keySlot));
            if (!visitor(keySlot, node->data, user))
                goto cleanup;
        }
        node = node->right;
    }
    complete = true;

cleanup:
    free(stack);
    return complete;
}


UMKA_API bool umkaAddClosure(Umka *umka, const char *name, UmkaExternFunc func, void *upvalue)
{
    return compilerAddClosure(umka, name, func, upvalue);
}
