#include "qv4vme_moth_p.h"
#include "qv4instr_moth_p.h"
#include "qmljs_value.h"

#ifdef DO_TRACE_INSTR
#  define TRACE_INSTR(I) fprintf(stderr, "executing a %s\n", #I);
#  define TRACE(n, str, ...) { fprintf(stderr, "    %s : ", #n); fprintf(stderr, str, __VA_ARGS__); fprintf(stderr, "\n"); }
#else
#  define TRACE_INSTR(I)
#  define TRACE(n, str, ...)
#endif // DO_TRACE_INSTR

using namespace QQmlJS;
using namespace QQmlJS::Moth;

#define MOTH_BEGIN_INSTR_COMMON(I) { \
    const InstrMeta<(int)Instr::I>::DataType &instr = InstrMeta<(int)Instr::I>::data(*genericInstr); \
    code += InstrMeta<(int)Instr::I>::Size; \
    Q_UNUSED(instr); \
    TRACE_INSTR(I)

#ifdef MOTH_THREADED_INTERPRETER

#  define MOTH_BEGIN_INSTR(I) op_##I: \
    MOTH_BEGIN_INSTR_COMMON(I)

#  define MOTH_NEXT_INSTR(I) { \
    genericInstr = reinterpret_cast<const Instr *>(code); \
    goto *genericInstr->common.code; \
    }

#  define MOTH_END_INSTR(I) } \
    genericInstr = reinterpret_cast<const Instr *>(code); \
    goto *genericInstr->common.code; \

#else

#  define MOTH_BEGIN_INSTR(I) \
    case Instr::I: \
    MOTH_BEGIN_INSTR_COMMON(I)

#  define MOTH_NEXT_INSTR(I) { \
    break; \
    }

#  define MOTH_END_INSTR(I) } \
    break;

#endif

static inline VM::Value *tempValue(QQmlJS::VM::ExecutionContext *context, QVector<VM::Value> &stack, int index)
{
    VM::DeclarativeEnvironment *varEnv = context->variableEnvironment;

#ifdef DO_TRACE_INSTR
    const char *kind;
    int pos;
    if (index < 0) {
        kind = "arg";
        pos = -index - 1;
    } else if (index < (int) varEnv->varCount) {
        kind = "local";
        pos = index;
    } else {
        kind = "temp";
        pos = index - varEnv->varCount;
    }
    fprintf(stderr, "    tempValue: index = %d : %s = %d, stack size = %d\n",
          index, kind, pos, stack.size());
#endif // DO_TRACE_INSTR

    if (index < 0) {
        const int arg = -index - 1;

        Q_ASSERT(arg >= 0);
        Q_ASSERT((unsigned) arg < varEnv->argumentCount);
        Q_ASSERT(varEnv->arguments);

        return varEnv->arguments + arg;
    } else if (index < (int) varEnv->varCount) {
        Q_ASSERT(index >= 0);
        Q_ASSERT(varEnv->locals);

        return varEnv->locals + index;
    } else {
        int off = index - varEnv->varCount;

        Q_ASSERT(off >= 0);
        Q_ASSERT(off < stack.size());

        return stack.data() + off;
    }
}

#define TEMP(index) *tempValue(context, stack, index)

VM::Value VME::operator()(QQmlJS::VM::ExecutionContext *context, const uchar *code
#ifdef MOTH_THREADED_INTERPRETER
        , void ***storeJumpTable
#endif
        )
{

#ifdef MOTH_THREADED_INTERPRETER
    if (storeJumpTable) {
#define MOTH_INSTR_ADDR(I, FMT) &&op_##I,
        static void *jumpTable[] = {
            FOR_EACH_MOTH_INSTR(MOTH_INSTR_ADDR)
        };
#undef MOTH_INSTR_ADDR
        *storeJumpTable = jumpTable;
        return VM::Value::undefinedValue();
    }
#endif

    QVector<VM::Value> stack;

#ifdef MOTH_THREADED_INTERPRETER
    const Instr *genericInstr = reinterpret_cast<const Instr *>(code);
    goto *genericInstr->common.code;
#else
    for (;;) {
        const Instr *genericInstr = reinterpret_cast<const Instr *>(code);
        switch (genericInstr->common.instructionType) {
#endif

    MOTH_BEGIN_INSTR(MoveTemp)
        VM::Value tmp = TEMP(instr.fromTempIndex);
        TEMP(instr.toTempIndex) = tmp;
    MOTH_END_INSTR(MoveTemp)

    MOTH_BEGIN_INSTR(LoadUndefined)
        TEMP(instr.targetTempIndex) = VM::Value::undefinedValue();
    MOTH_END_INSTR(LoadUndefined)

    MOTH_BEGIN_INSTR(LoadNull)
        TEMP(instr.targetTempIndex) = VM::Value::nullValue();
    MOTH_END_INSTR(LoadNull)

    MOTH_BEGIN_INSTR(LoadTrue)
        TEMP(instr.targetTempIndex) = VM::Value::fromBoolean(true);
    MOTH_END_INSTR(LoadTrue)

    MOTH_BEGIN_INSTR(LoadFalse)
        TEMP(instr.targetTempIndex) = VM::Value::fromBoolean(false);
    MOTH_END_INSTR(LoadFalse)

    MOTH_BEGIN_INSTR(LoadNumber)
        TRACE(inline, "number = %f", instr.value);
        TEMP(instr.targetTempIndex) = VM::Value::fromDouble(instr.value);
    MOTH_END_INSTR(LoadNumber)

    MOTH_BEGIN_INSTR(LoadString)
        TEMP(instr.targetTempIndex) = VM::Value::fromString(instr.value);
    MOTH_END_INSTR(LoadString)

    MOTH_BEGIN_INSTR(LoadClosure)
        TEMP(instr.targetTempIndex) = __qmljs_init_closure(instr.value, context);
    MOTH_END_INSTR(LoadClosure)

    MOTH_BEGIN_INSTR(LoadName)
        TRACE(inline, "property name = %s", instr.name->toQString().toUtf8().constData());
        TEMP(instr.targetTempIndex) = __qmljs_get_activation_property(context, instr.name);
    MOTH_END_INSTR(LoadName)

    MOTH_BEGIN_INSTR(StoreName)
        TRACE(inline, "property name = %s", instr.name->toQString().toUtf8().constData());
        VM::Value source = instr.sourceIsTemp ? TEMP(instr.source.tempIndex) : instr.source.value;
        __qmljs_set_activation_property(context, instr.name, source);
    MOTH_END_INSTR(StoreName)

    MOTH_BEGIN_INSTR(LoadElement)
        TEMP(instr.targetTempIndex) = __qmljs_get_element(context, TEMP(instr.base), TEMP(instr.index));
    MOTH_END_INSTR(LoadElement)

    MOTH_BEGIN_INSTR(StoreElement)
        VM::Value source = instr.sourceIsTemp ? TEMP(instr.source.tempIndex) : instr.source.value;
        __qmljs_set_element(context, TEMP(instr.base), TEMP(instr.index), source);
    MOTH_END_INSTR(StoreElement)

    MOTH_BEGIN_INSTR(LoadProperty)
        TRACE(inline, "base temp = %d, property name = %s", instr.baseTemp, instr.name->toQString().toUtf8().constData());
        VM::Value base = TEMP(instr.baseTemp);
        TEMP(instr.targetTempIndex) = __qmljs_get_property(context, base, instr.name);
    MOTH_END_INSTR(LoadProperty)

    MOTH_BEGIN_INSTR(StoreProperty)
        TRACE(inline, "base temp = %d, property name = %s", instr.baseTemp, instr.name->toQString().toUtf8().constData());
        VM::Value base = TEMP(instr.baseTemp);
        VM::Value source = instr.sourceIsTemp ? TEMP(instr.source.tempIndex) : instr.source.value;
        __qmljs_set_property(context, base, instr.name, source);
    MOTH_END_INSTR(StoreProperty)

    MOTH_BEGIN_INSTR(Push)
        TRACE(inline, "stack size: %u", instr.value);
        stack.resize(instr.value);
    MOTH_END_INSTR(Push)

    MOTH_BEGIN_INSTR(CallValue)
        TRACE(Call, "argStart = %d, argc = %d, result temp index = %d", instr.args, instr.argc, instr.targetTempIndex);
        VM::Value *args = stack.data() + instr.args;
        TEMP(instr.targetTempIndex) = __qmljs_call_value(context, VM::Value::undefinedValue(), TEMP(instr.destIndex), args, instr.argc);
    MOTH_END_INSTR(CallValue)

    MOTH_BEGIN_INSTR(CallProperty)
        VM::Value *args = stack.data() + instr.args;
        TEMP(instr.targetTempIndex) = __qmljs_call_property(context, TEMP(instr.baseTemp), instr.name, args, instr.argc);
    MOTH_END_INSTR(CallProperty)

    MOTH_BEGIN_INSTR(CallBuiltin)
        VM::Value *args = stack.data() + instr.args;
        void *buf;
        switch (instr.builtin) {
        case Instr::instr_callBuiltin::builtin_typeof:
            TEMP(instr.targetTempIndex) = __qmljs_builtin_typeof(args[0], context);
            break;
        case Instr::instr_callBuiltin::builtin_throw:
            TRACE(builtin_throw, "Throwing now...%s", "");
            __qmljs_builtin_throw(args[0], context);
            break;
        case Instr::instr_callBuiltin::builtin_create_exception_handler: {
            TRACE(builtin_create_exception_handler, "%s", "");
            buf = __qmljs_create_exception_handler(context);
            // The targetTempIndex is the only value we need from the instr to
            // continue execution when an exception is caught.
            int targetTempIndex = instr.targetTempIndex;
            int didThrow = setjmp(* static_cast<jmp_buf *>(buf));
            // Two ways to come here: after a create, or after a throw.
            if (didThrow)
                // At this point, the interpreter state can be anything but
                // valid, so first restore the state. This includes all relevant
                // locals.
                restoreState(context, targetTempIndex, code);
            else
                // Save the state and any variables we need when catching an
                // exception, so we can restore the state at that point.
                saveState(context, targetTempIndex, code);
            TEMP(targetTempIndex) = VM::Value::fromInt32(didThrow);
        } break;
        case Instr::instr_callBuiltin::builtin_delete_exception_handler:
            TRACE(builtin_delete_exception_handler, "%s", "");
            __qmljs_delete_exception_handler(context);
            break;
        case Instr::instr_callBuiltin::builtin_get_exception:
            TEMP(instr.targetTempIndex) = __qmljs_get_exception(context);
            break;
        case Instr::instr_callBuiltin::builtin_foreach_iterator_object:
            TEMP(instr.targetTempIndex) = __qmljs_foreach_iterator_object(args[0], context);
            break;
        case Instr::instr_callBuiltin::builtin_foreach_next_property_name:
            TEMP(instr.targetTempIndex) = __qmljs_foreach_next_property_name(args[0]);
            break;
        }
    MOTH_END_INSTR(CallBuiltin)

    MOTH_BEGIN_INSTR(CallBuiltinDeleteMember)
        TEMP(instr.targetTempIndex) = __qmljs_delete_member(context, TEMP(instr.base), instr.member);
    MOTH_END_INSTR(CallBuiltinDeleteMember)

    MOTH_BEGIN_INSTR(CallBuiltinDeleteSubscript)
        TEMP(instr.targetTempIndex) = __qmljs_delete_subscript(context, TEMP(instr.base), TEMP(instr.index));
    MOTH_END_INSTR(CallBuiltinDeleteSubscript)

    MOTH_BEGIN_INSTR(CallBuiltinDeleteName)
        TEMP(instr.targetTempIndex) = __qmljs_delete_name(context, instr.name);
    MOTH_END_INSTR(CallBuiltinDeleteName)

    MOTH_BEGIN_INSTR(CallBuiltinDeleteValue)
        TEMP(instr.targetTempIndex) = VM::Value::fromBoolean(false);
    MOTH_END_INSTR(CallBuiltinDeleteValue)

    MOTH_BEGIN_INSTR(CreateValue)
        VM::Value *args = stack.data() + instr.args;
        TEMP(instr.targetTempIndex) = __qmljs_construct_value(context, TEMP(instr.func), args, instr.argc);
    MOTH_END_INSTR(CreateValue)

    MOTH_BEGIN_INSTR(CreateProperty)
        VM::Value *args = stack.data() + instr.args;
        TEMP(instr.targetTempIndex) = __qmljs_construct_property(context, TEMP(instr.base), instr.name, args, instr.argc);
    MOTH_END_INSTR(CreateProperty)

    MOTH_BEGIN_INSTR(CreateActivationProperty)
        TRACE(inline, "property name = %s, argc = %d", instr.name->toQString().toUtf8().constData(), instr.argc);
        VM::Value *args = stack.data() + instr.args;
        TEMP(instr.targetTempIndex) = __qmljs_construct_activation_property(context, instr.name, args, instr.argc);
    MOTH_END_INSTR(CreateActivationProperty)

    MOTH_BEGIN_INSTR(Jump)
        code = ((uchar *)&instr.offset) + instr.offset;
    MOTH_END_INSTR(Jump)

    MOTH_BEGIN_INSTR(CJump)
        if (__qmljs_to_boolean(TEMP(instr.tempIndex), context))
            code = ((uchar *)&instr.offset) + instr.offset;
    MOTH_END_INSTR(CJump)

    MOTH_BEGIN_INSTR(Unop)
        TEMP(instr.targetTempIndex) = instr.alu(TEMP(instr.e), context);
    MOTH_END_INSTR(Unop)

    MOTH_BEGIN_INSTR(Binop)
        VM::Value lhs = instr.lhsIsTemp ? TEMP(instr.lhs.tempIndex) : instr.lhs.value;
        VM::Value rhs = instr.rhsIsTemp ? TEMP(instr.rhs.tempIndex) : instr.rhs.value;
        TEMP(instr.targetTempIndex) = instr.alu(lhs, rhs, context);
    MOTH_END_INSTR(Binop)

    MOTH_BEGIN_INSTR(Ret)
        VM::Value result = TEMP(instr.tempIndex);
        TRACE(Ret, "returning value %s", result.toString(context)->toQString().toUtf8().constData());
        return result;
    MOTH_END_INSTR(Ret)

    MOTH_BEGIN_INSTR(LoadThis)
        TEMP(instr.targetTempIndex) = __qmljs_get_thisObject(context);
    MOTH_END_INSTR(LoadThis)

    MOTH_BEGIN_INSTR(InplaceElementOp)
        VM::Value source = instr.sourceIsTemp ? TEMP(instr.source.tempIndex) : instr.source.value;
        instr.alu(TEMP(instr.targetBase),
                  TEMP(instr.targetIndex),
                  source,
                  context);
    MOTH_END_INSTR(InplaceElementOp)

    MOTH_BEGIN_INSTR(InplaceMemberOp)
        VM::Value source = instr.sourceIsTemp ? TEMP(instr.source.tempIndex) : instr.source.value;
        instr.alu(source,
                  TEMP(instr.targetBase),
                  instr.targetMember,
                  context);
    MOTH_END_INSTR(InplaceMemberOp)

    MOTH_BEGIN_INSTR(InplaceNameOp)
        VM::Value source = instr.sourceIsTemp ? TEMP(instr.source.tempIndex) : instr.source.value;
        instr.alu(source,
                  instr.targetName,
                  context);
    MOTH_END_INSTR(InplaceNameOp)

#ifdef MOTH_THREADED_INTERPRETER
    // nothing to do
#else
        default:
            qFatal("QQmlJS::Moth::VME: Internal error - unknown instruction %d", genericInstr->common.instructionType);
            break;
        }
    }
#endif

}

#ifdef MOTH_THREADED_INTERPRETER
void **VME::instructionJumpTable()
{
    static void **jumpTable = 0;
    if (!jumpTable) {
        VME dummy;
        dummy(0, 0, &jumpTable);
    }
    return jumpTable;
}
#endif

VM::Value VME::exec(VM::ExecutionContext *ctxt, const uchar *code)
{
    VME vme;
    return vme(ctxt, code);
}

void VME::restoreState(VM::ExecutionContext *context, int &targetTempIndex, const uchar *&code)
{
    VM::ExecutionEngine::ExceptionHandler &handler = context->engine->unwindStack.last();
    targetTempIndex = handler.targetTempIndex;
    code = handler.code;
}

void VME::saveState(VM::ExecutionContext *context, int targetTempIndex, const uchar *code)
{
    VM::ExecutionEngine::ExceptionHandler &handler = context->engine->unwindStack.last();
    handler.targetTempIndex = targetTempIndex;
    handler.code = code;
}
