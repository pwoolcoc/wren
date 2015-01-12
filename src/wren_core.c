#define _XOPEN_SOURCE 700
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wren_core.h"
#include "wren_value.h"

// Binds a native method named [name] (in Wren) implemented using C function
// [fn] to `ObjClass` [cls].
#define NATIVE(cls, name, function) \
    { \
      int symbol = wrenSymbolTableEnsure(vm, \
          &vm->methodNames, name, strlen(name)); \
      Method method; \
      method.type = METHOD_PRIMITIVE; \
      method.fn.primitive = native_##function; \
      wrenBindMethod(vm, cls, symbol, method); \
    }

// Defines a native method whose C function name is [native]. This abstracts
// the actual type signature of a native function and makes it clear which C
// functions are intended to be invoked as natives.
#define DEF_NATIVE(native) \
    static PrimitiveResult native_##native(WrenVM* vm, ObjFiber* fiber, Value* args)

#define RETURN_VAL(value)   do { args[0] = value; return PRIM_VALUE; } while (0)

#define RETURN_OBJ(obj)     RETURN_VAL(OBJ_VAL(obj))
#define RETURN_BOOL(value)  RETURN_VAL(BOOL_VAL(value))
#define RETURN_FALSE        RETURN_VAL(FALSE_VAL)
#define RETURN_NULL         RETURN_VAL(NULL_VAL)
#define RETURN_NUM(value)   RETURN_VAL(NUM_VAL(value))
#define RETURN_TRUE         RETURN_VAL(TRUE_VAL)

#define RETURN_ERROR(msg) \
    do { \
      args[0] = wrenNewString(vm, msg, strlen(msg)); \
      return PRIM_ERROR; \
    } while (0);

// This string literal is generated automatically from core. Do not edit.
static const char* libSource =
"class Sequence {\n"
"  map(f) {\n"
"    var result = []\n"
"    for (element in this) {\n"
"      result.add(f.call(element))\n"
"    }\n"
"    return result\n"
"  }\n"
"\n"
"  where(f) {\n"
"    var result = []\n"
"    for (element in this) {\n"
"      if (f.call(element)) result.add(element)\n"
"    }\n"
"    return result\n"
"  }\n"
"}\n"
"\n"
"class List is Sequence {\n"
"  addAll(other) {\n"
"    for (element in other) {\n"
"      add(element)\n"
"    }\n"
"    return other\n"
"  }\n"
"\n"
"  toString {\n"
"    var result = \"[\"\n"
"    for (i in 0...count) {\n"
"      if (i > 0) result = result + \", \"\n"
"      result = result + this[i].toString\n"
"    }\n"
"    result = result + \"]\"\n"
"    return result\n"
"  }\n"
"\n"
"  +(other) {\n"
"    var result = this[0..-1]\n"
"    for (element in other) {\n"
"      result.add(element)\n"
"    }\n"
"    return result\n"
"  }\n"
"\n"
"  contains(element) {\n"
"    for (item in this) {\n"
"      if (element == item) {\n"
"        return true\n"
"      }\n"
"    }\n"
"    return false\n"
"  }\n"
"}\n"
"\n"
"class Range is Sequence {}\n";

// Validates that the given argument in [args] is a function. Returns true if
// it is. If not, reports an error and returns false.
static bool validateFn(WrenVM* vm, Value* args, int index, const char* argName)
{
  if (IS_FN(args[index]) || IS_CLOSURE(args[index])) return true;

  args[0] = OBJ_VAL(wrenStringConcat(vm, argName, " must be a function."));
  return false;
}

// Validates that the given argument in [args] is a Num. Returns true if it is.
// If not, reports an error and returns false.
static bool validateNum(WrenVM* vm, Value* args, int index, const char* argName)
{
  if (IS_NUM(args[index])) return true;

  args[0] = OBJ_VAL(wrenStringConcat(vm, argName, " must be a number."));
  return false;
}

// Validates that [value] is an integer. Returns true if it is. If not, reports
// an error and returns false.
static bool validateIntValue(WrenVM* vm, Value* args, double value,
                             const char* argName)
{
  if (trunc(value) == value) return true;

  args[0] = OBJ_VAL(wrenStringConcat(vm, argName, " must be an integer."));
  return false;
}

// Validates that the given argument in [args] is an integer. Returns true if
// it is. If not, reports an error and returns false.
static bool validateInt(WrenVM* vm, Value* args, int index, const char* argName)
{
  // Make sure it's a number first.
  if (!validateNum(vm, args, index, argName)) return false;

  return validateIntValue(vm, args, AS_NUM(args[index]), argName);
}

// Validates that [value] is an integer within `[0, count)`. Also allows
// negative indices which map backwards from the end. Returns the valid positive
// index value. If invalid, reports an error and returns -1.
static int validateIndexValue(WrenVM* vm, Value* args, int count, double value,
                              const char* argName)
{
  if (!validateIntValue(vm, args, value, argName)) return -1;

  int index = (int)value;

  // Negative indices count from the end.
  if (index < 0) index = count + index;

  // Check bounds.
  if (index >= 0 && index < count) return index;

  args[0] = OBJ_VAL(wrenStringConcat(vm, argName, " out of bounds."));
  return -1;
}

// Validates that the argument at [argIndex] is an integer within `[0, count)`.
// Also allows negative indices which map backwards from the end. Returns the
// valid positive index value. If invalid, reports an error and returns -1.
static int validateIndex(WrenVM* vm, Value* args, int count, int argIndex,
                         const char* argName)
{
  if (!validateNum(vm, args, argIndex, argName)) return -1;

  return validateIndexValue(vm, args, count, AS_NUM(args[argIndex]), argName);
}

// Validates that the given argument in [args] is a String. Returns true if it
// is. If not, reports an error and returns false.
static bool validateString(WrenVM* vm, Value* args, int index,
                           const char* argName)
{
  if (IS_STRING(args[index])) return true;

  args[0] = OBJ_VAL(wrenStringConcat(vm, argName, " must be a string."));
  return false;
}

DEF_NATIVE(bool_not)
{
  RETURN_BOOL(!AS_BOOL(args[0]));
}

DEF_NATIVE(bool_toString)
{
  if (AS_BOOL(args[0]))
  {
    RETURN_VAL(wrenNewString(vm, "true", 4));
  }
  else
  {
    RETURN_VAL(wrenNewString(vm, "false", 5));
  }
}

DEF_NATIVE(class_instantiate)
{
  ObjClass* classObj = AS_CLASS(args[0]);
  RETURN_VAL(wrenNewInstance(vm, classObj));
}

DEF_NATIVE(class_name)
{
  ObjClass* classObj = AS_CLASS(args[0]);
  RETURN_OBJ(classObj->name);
}

DEF_NATIVE(fiber_instantiate)
{
  // Return the Fiber class itself. When we then call "new" on it, it will
  // create the fiber.
  RETURN_VAL(args[0]);
}

DEF_NATIVE(fiber_new)
{
  if (!validateFn(vm, args, 1, "Argument")) return PRIM_ERROR;

  ObjFiber* newFiber = wrenNewFiber(vm, AS_OBJ(args[1]));

  // The compiler expects the first slot of a function to hold the receiver.
  // Since a fiber's stack is invoked directly, it doesn't have one, so put it
  // in here.
  // TODO: Is there a cleaner solution?
  // TODO: If we make growable stacks, make sure this grows it.
  newFiber->stack[0] = NULL_VAL;
  newFiber->stackTop++;

  RETURN_OBJ(newFiber);
}

DEF_NATIVE(fiber_abort)
{
  if (!validateString(vm, args, 1, "Error message")) return PRIM_ERROR;

  // Move the error message to the return position.
  args[0] = args[1];
  return PRIM_ERROR;
}

DEF_NATIVE(fiber_call)
{
  ObjFiber* runFiber = AS_FIBER(args[0]);

  if (runFiber->numFrames == 0) RETURN_ERROR("Cannot call a finished fiber.");
  if (runFiber->caller != NULL) RETURN_ERROR("Fiber has already been called.");

  // Remember who ran it.
  runFiber->caller = fiber;

  // If the fiber was yielded, make the yield call return null.
  if (runFiber->stackTop > runFiber->stack)
  {
    *(runFiber->stackTop - 1) = NULL_VAL;
  }

  return PRIM_RUN_FIBER;
}

DEF_NATIVE(fiber_call1)
{
  ObjFiber* runFiber = AS_FIBER(args[0]);

  if (runFiber->numFrames == 0) RETURN_ERROR("Cannot call a finished fiber.");
  if (runFiber->caller != NULL) RETURN_ERROR("Fiber has already been called.");

  // Remember who ran it.
  runFiber->caller = fiber;

  // If the fiber was yielded, make the yield call return the value passed to
  // run.
  if (runFiber->stackTop > runFiber->stack)
  {
    *(runFiber->stackTop - 1) = args[1];
  }

  // When the calling fiber resumes, we'll store the result of the run call
  // in its stack. Since fiber.run(value) has two arguments (the fiber and the
  // value) and we only need one slot for the result, discard the other slot
  // now.
  fiber->stackTop--;

  return PRIM_RUN_FIBER;
}

DEF_NATIVE(fiber_error)
{
  ObjFiber* runFiber = AS_FIBER(args[0]);
  if (runFiber->error == NULL) RETURN_NULL;
  RETURN_OBJ(runFiber->error);
}

DEF_NATIVE(fiber_isDone)
{
  ObjFiber* runFiber = AS_FIBER(args[0]);
  RETURN_BOOL(runFiber->numFrames == 0 || runFiber->error != NULL);
}

DEF_NATIVE(fiber_run)
{
  ObjFiber* runFiber = AS_FIBER(args[0]);

  if (runFiber->numFrames == 0) RETURN_ERROR("Cannot run a finished fiber.");

  // If the fiber was yielded, make the yield call return null.
  if (runFiber->caller == NULL && runFiber->stackTop > runFiber->stack)
  {
    *(runFiber->stackTop - 1) = NULL_VAL;
  }

  // Unlike run, this does not remember the calling fiber. Instead, it
  // remember's *that* fiber's caller. You can think of it like tail call
  // elimination. The switched-from fiber is discarded and when the switched
  // to fiber completes or yields, control passes to the switched-from fiber's
  // caller.
  runFiber->caller = fiber->caller;

  return PRIM_RUN_FIBER;
}

DEF_NATIVE(fiber_run1)
{
  ObjFiber* runFiber = AS_FIBER(args[0]);

  if (runFiber->numFrames == 0) RETURN_ERROR("Cannot run a finished fiber.");

  // If the fiber was yielded, make the yield call return the value passed to
  // run.
  if (runFiber->caller == NULL && runFiber->stackTop > runFiber->stack)
  {
    *(runFiber->stackTop - 1) = args[1];
  }

  // Unlike run, this does not remember the calling fiber. Instead, it
  // remember's *that* fiber's caller. You can think of it like tail call
  // elimination. The switched-from fiber is discarded and when the switched
  // to fiber completes or yields, control passes to the switched-from fiber's
  // caller.
  runFiber->caller = fiber->caller;

  return PRIM_RUN_FIBER;
}

DEF_NATIVE(fiber_try)
{
  ObjFiber* runFiber = AS_FIBER(args[0]);

  if (runFiber->numFrames == 0) RETURN_ERROR("Cannot try a finished fiber.");
  if (runFiber->caller != NULL) RETURN_ERROR("Fiber has already been called.");

  // Remember who ran it.
  runFiber->caller = fiber;
  runFiber->callerIsTrying = true;

  // If the fiber was yielded, make the yield call return null.
  if (runFiber->stackTop > runFiber->stack)
  {
    *(runFiber->stackTop - 1) = NULL_VAL;
  }

  return PRIM_RUN_FIBER;
}

DEF_NATIVE(fiber_yield)
{
  if (fiber->caller == NULL) RETURN_ERROR("No fiber to yield to.");

  ObjFiber* caller = fiber->caller;
  fiber->caller = NULL;
  fiber->callerIsTrying = false;

  // Make the caller's run method return null.
  *(caller->stackTop - 1) = NULL_VAL;

  // Return the fiber to resume.
  args[0] = OBJ_VAL(caller);
  return PRIM_RUN_FIBER;
}

DEF_NATIVE(fiber_yield1)
{
  if (fiber->caller == NULL) RETURN_ERROR("No fiber to yield to.");

  ObjFiber* caller = fiber->caller;
  fiber->caller = NULL;
  fiber->callerIsTrying = false;

  // Make the caller's run method return the argument passed to yield.
  *(caller->stackTop - 1) = args[1];

  // When the yielding fiber resumes, we'll store the result of the yield call
  // in its stack. Since Fiber.yield(value) has two arguments (the Fiber class
  // and the value) and we only need one slot for the result, discard the other
  // slot now.
  fiber->stackTop--;

  // Return the fiber to resume.
  args[0] = OBJ_VAL(caller);
  return PRIM_RUN_FIBER;
}

static PrimitiveResult callFunction(WrenVM* vm, Value* args, int numArgs)
{
  ObjFn* fn;
  if (IS_CLOSURE(args[0]))
  {
    fn = AS_CLOSURE(args[0])->fn;
  }
  else
  {
    fn = AS_FN(args[0]);
  }

  if (numArgs < fn->numParams) RETURN_ERROR("Function expects more arguments.");

  return PRIM_CALL;
}

DEF_NATIVE(fn_instantiate)
{
  // Return the Fn class itself. When we then call "new" on it, it will
  // return the block.
  RETURN_VAL(args[0]);
}

DEF_NATIVE(fn_new)
{
  if (!validateFn(vm, args, 1, "Argument")) return PRIM_ERROR;

  // The block argument is already a function, so just return it.
  RETURN_VAL(args[1]);
}

DEF_NATIVE(fn_call0) { return callFunction(vm, args, 0); }
DEF_NATIVE(fn_call1) { return callFunction(vm, args, 1); }
DEF_NATIVE(fn_call2) { return callFunction(vm, args, 2); }
DEF_NATIVE(fn_call3) { return callFunction(vm, args, 3); }
DEF_NATIVE(fn_call4) { return callFunction(vm, args, 4); }
DEF_NATIVE(fn_call5) { return callFunction(vm, args, 5); }
DEF_NATIVE(fn_call6) { return callFunction(vm, args, 6); }
DEF_NATIVE(fn_call7) { return callFunction(vm, args, 7); }
DEF_NATIVE(fn_call8) { return callFunction(vm, args, 8); }
DEF_NATIVE(fn_call9) { return callFunction(vm, args, 9); }
DEF_NATIVE(fn_call10) { return callFunction(vm, args, 10); }
DEF_NATIVE(fn_call11) { return callFunction(vm, args, 11); }
DEF_NATIVE(fn_call12) { return callFunction(vm, args, 12); }
DEF_NATIVE(fn_call13) { return callFunction(vm, args, 13); }
DEF_NATIVE(fn_call14) { return callFunction(vm, args, 14); }
DEF_NATIVE(fn_call15) { return callFunction(vm, args, 15); }
DEF_NATIVE(fn_call16) { return callFunction(vm, args, 16); }

DEF_NATIVE(fn_toString)
{
  RETURN_VAL(wrenNewString(vm, "<fn>", 4));
}

DEF_NATIVE(list_instantiate)
{
  RETURN_OBJ(wrenNewList(vm, 0));
}

DEF_NATIVE(list_add)
{
  ObjList* list = AS_LIST(args[0]);
  wrenListAdd(vm, list, args[1]);
  RETURN_VAL(args[1]);
}

DEF_NATIVE(list_clear)
{
  ObjList* list = AS_LIST(args[0]);
  wrenReallocate(vm, list->elements, 0, 0);
  list->elements = NULL;
  list->capacity = 0;
  list->count = 0;
  RETURN_NULL;
}

DEF_NATIVE(list_count)
{
  ObjList* list = AS_LIST(args[0]);
  RETURN_NUM(list->count);
}

DEF_NATIVE(list_insert)
{
  ObjList* list = AS_LIST(args[0]);

  // count + 1 here so you can "insert" at the very end.
  int index = validateIndex(vm, args, list->count + 1, 2, "Index");
  if (index == -1) return PRIM_ERROR;

  wrenListInsert(vm, list, args[1], index);
  RETURN_VAL(args[1]);
}

DEF_NATIVE(list_iterate)
{
  ObjList* list = AS_LIST(args[0]);

  // If we're starting the iteration, return the first index.
  if (IS_NULL(args[1]))
  {
    if (list->count == 0) RETURN_FALSE;
    RETURN_NUM(0);
  }

  if (!validateInt(vm, args, 1, "Iterator")) return PRIM_ERROR;

  int index = (int)AS_NUM(args[1]);

  // Stop if we're out of bounds.
  if (index < 0 || index >= list->count - 1) RETURN_FALSE;

  // Otherwise, move to the next index.
  RETURN_NUM(index + 1);
}

DEF_NATIVE(list_iteratorValue)
{
  ObjList* list = AS_LIST(args[0]);
  int index = validateIndex(vm, args, list->count, 1, "Iterator");
  if (index == -1) return PRIM_ERROR;

  RETURN_VAL(list->elements[index]);
}

DEF_NATIVE(list_removeAt)
{
  ObjList* list = AS_LIST(args[0]);
  int index = validateIndex(vm, args, list->count, 1, "Index");
  if (index == -1) return PRIM_ERROR;

  RETURN_VAL(wrenListRemoveAt(vm, list, index));
}

DEF_NATIVE(list_subscript)
{
  ObjList* list = AS_LIST(args[0]);

  if (IS_NUM(args[1]))
  {
    int index = validateIndex(vm, args, list->count, 1, "Subscript");
    if (index == -1) return PRIM_ERROR;

    RETURN_VAL(list->elements[index]);
  }

  if (!IS_RANGE(args[1]))
  {
    RETURN_ERROR("Subscript must be a number or a range.");
  }

  ObjRange* range = AS_RANGE(args[1]);

  // Corner case: an empty range at zero is allowed on an empty list.
  // This way, list[0..-1] and list[0...list.count] can be used to copy a list
  // even when empty.
  if (list->count == 0 && range->from == 0 &&
      range->to == (range->isInclusive ? -1 : 0)) {
    RETURN_OBJ(wrenNewList(vm, 0));
  }

  int from = validateIndexValue(vm, args, list->count, range->from,
                                "Range start");
  if (from == -1) return PRIM_ERROR;

  int to;
  int count;

  if (range->isInclusive)
  {
    to = validateIndexValue(vm, args, list->count, range->to, "Range end");
    if (to == -1) return PRIM_ERROR;

    count = abs(from - to) + 1;
  }
  else
  {
    if (!validateIntValue(vm, args, range->to, "Range end")) return PRIM_ERROR;

    // Bounds check it manually here since the excusive range can hang over
    // the edge.
    to = (int)range->to;
    if (to < 0) to = list->count + to;

    if (to < -1 || to > list->count) RETURN_ERROR("Range end out of bounds.");

    count = abs(from - to);
  }

  int step = from < to ? 1 : -1;
  ObjList* result = wrenNewList(vm, count);
  for (int i = 0; i < count; i++)
  {
    result->elements[i] = list->elements[from + (i * step)];
  }

  RETURN_OBJ(result);
}

DEF_NATIVE(list_subscriptSetter)
{
  ObjList* list = AS_LIST(args[0]);
  int index = validateIndex(vm, args, list->count, 1, "Subscript");
  if (index == -1) return PRIM_ERROR;

  list->elements[index] = args[2];
  RETURN_VAL(args[2]);
}

DEF_NATIVE(null_toString)
{
  RETURN_VAL(wrenNewString(vm, "null", 4));
}

DEF_NATIVE(num_abs)
{
  RETURN_NUM(fabs(AS_NUM(args[0])));
}

DEF_NATIVE(num_ceil)
{
  RETURN_NUM(ceil(AS_NUM(args[0])));
}

DEF_NATIVE(num_cos)
{
  RETURN_NUM(cos(AS_NUM(args[0])));
}

DEF_NATIVE(num_floor)
{
  RETURN_NUM(floor(AS_NUM(args[0])));
}

DEF_NATIVE(num_isNan)
{
  RETURN_BOOL(isnan(AS_NUM(args[0])));
}

DEF_NATIVE(num_sin)
{
  RETURN_NUM(sin(AS_NUM(args[0])));
}

DEF_NATIVE(num_sqrt)
{
  RETURN_NUM(sqrt(AS_NUM(args[0])));
}

DEF_NATIVE(num_toString)
{
  double value = AS_NUM(args[0]);

  // Corner case: If the value is NaN, different versions of libc produce
  // different outputs (some will format it signed and some won't). To get
  // reliable output, handle that ourselves.
  if (value != value) RETURN_VAL(wrenNewString(vm, "nan", 3));

  // This is large enough to hold any double converted to a string using
  // "%.14g". Example:
  //
  //     -1.12345678901234e-1022
  //
  // So we have:
  //
  // + 1 char for sign
  // + 1 char for digit
  // + 1 char for "."
  // + 14 chars for decimal digits
  // + 1 char for "e"
  // + 1 char for "-" or "+"
  // + 4 chars for exponent
  // + 1 char for "\0"
  // = 24
  char buffer[24];
  int length = sprintf(buffer, "%.14g", value);
  RETURN_VAL(wrenNewString(vm, buffer, length));
}

DEF_NATIVE(num_negate)
{
  RETURN_NUM(-AS_NUM(args[0]));
}

DEF_NATIVE(num_minus)
{
  if (!validateNum(vm, args, 1, "Right operand")) return PRIM_ERROR;
  RETURN_NUM(AS_NUM(args[0]) - AS_NUM(args[1]));
}

DEF_NATIVE(num_plus)
{
  if (!validateNum(vm, args, 1, "Right operand")) return PRIM_ERROR;
  RETURN_NUM(AS_NUM(args[0]) + AS_NUM(args[1]));
}

DEF_NATIVE(num_multiply)
{
  if (!validateNum(vm, args, 1, "Right operand")) return PRIM_ERROR;
  RETURN_NUM(AS_NUM(args[0]) * AS_NUM(args[1]));
}

DEF_NATIVE(num_divide)
{
  if (!validateNum(vm, args, 1, "Right operand")) return PRIM_ERROR;
  RETURN_NUM(AS_NUM(args[0]) / AS_NUM(args[1]));
}

DEF_NATIVE(num_mod)
{
  if (!validateNum(vm, args, 1, "Right operand")) return PRIM_ERROR;
  RETURN_NUM(fmod(AS_NUM(args[0]), AS_NUM(args[1])));
}

DEF_NATIVE(num_lt)
{
  if (!validateNum(vm, args, 1, "Right operand")) return PRIM_ERROR;
  RETURN_BOOL(AS_NUM(args[0]) < AS_NUM(args[1]));
}

DEF_NATIVE(num_gt)
{
  if (!validateNum(vm, args, 1, "Right operand")) return PRIM_ERROR;
  RETURN_BOOL(AS_NUM(args[0]) > AS_NUM(args[1]));
}

DEF_NATIVE(num_lte)
{
  if (!validateNum(vm, args, 1, "Right operand")) return PRIM_ERROR;
  RETURN_BOOL(AS_NUM(args[0]) <= AS_NUM(args[1]));
}

DEF_NATIVE(num_gte)
{
  if (!validateNum(vm, args, 1, "Right operand")) return PRIM_ERROR;
  RETURN_BOOL(AS_NUM(args[0]) >= AS_NUM(args[1]));
}

DEF_NATIVE(num_eqeq)
{
  if (!IS_NUM(args[1])) RETURN_FALSE;
  RETURN_BOOL(AS_NUM(args[0]) == AS_NUM(args[1]));
}

DEF_NATIVE(num_bangeq)
{
  if (!IS_NUM(args[1])) RETURN_TRUE;
  RETURN_BOOL(AS_NUM(args[0]) != AS_NUM(args[1]));
}

DEF_NATIVE(num_bitwiseNot)
{
  // Bitwise operators always work on 32-bit unsigned ints.
  uint32_t value = (uint32_t)AS_NUM(args[0]);
  RETURN_NUM(~value);
}

DEF_NATIVE(num_bitwiseAnd)
{
  if (!validateNum(vm, args, 1, "Right operand")) return PRIM_ERROR;

  // Bitwise operators always work on 32-bit unsigned ints.
  uint32_t left = (uint32_t)AS_NUM(args[0]);
  uint32_t right = (uint32_t)AS_NUM(args[1]);
  RETURN_NUM(left & right);
}

DEF_NATIVE(num_bitwiseOr)
{
  if (!validateNum(vm, args, 1, "Right operand")) return PRIM_ERROR;

  // Bitwise operators always work on 32-bit unsigned ints.
  uint32_t left = (uint32_t)AS_NUM(args[0]);
  uint32_t right = (uint32_t)AS_NUM(args[1]);
  RETURN_NUM(left | right);
}

DEF_NATIVE(num_dotDot)
{
  if (!validateNum(vm, args, 1, "Right hand side of range")) return PRIM_ERROR;

  double from = AS_NUM(args[0]);
  double to = AS_NUM(args[1]);

  RETURN_VAL(wrenNewRange(vm, from, to, true));
}

DEF_NATIVE(num_dotDotDot)
{
  if (!validateNum(vm, args, 1, "Right hand side of range")) return PRIM_ERROR;

  double from = AS_NUM(args[0]);
  double to = AS_NUM(args[1]);

  RETURN_VAL(wrenNewRange(vm, from, to, false));
}

DEF_NATIVE(object_eqeq)
{
  RETURN_BOOL(wrenValuesEqual(args[0], args[1]));
}

DEF_NATIVE(object_bangeq)
{
  RETURN_BOOL(!wrenValuesEqual(args[0], args[1]));
}

DEF_NATIVE(object_new)
{
  // This is the default argument-less constructor that all objects inherit.
  // It just returns "this".
  RETURN_VAL(args[0]);
}

DEF_NATIVE(object_toString)
{
  if (IS_CLASS(args[0]))
  {
    RETURN_OBJ(AS_CLASS(args[0])->name);
  }
  else if (IS_INSTANCE(args[0]))
  {
    ObjInstance* instance = AS_INSTANCE(args[0]);
    RETURN_OBJ(wrenStringConcat(vm, "instance of ",
                                instance->obj.classObj->name->value));
  }

  RETURN_VAL(wrenNewString(vm, "<object>", 8));
}

DEF_NATIVE(object_type)
{
  RETURN_OBJ(wrenGetClass(vm, args[0]));
}

DEF_NATIVE(object_instantiate)
{
  RETURN_ERROR("Must provide a class to 'new' to construct.");
}

DEF_NATIVE(range_from)
{
  ObjRange* range = AS_RANGE(args[0]);
  RETURN_NUM(range->from);
}

DEF_NATIVE(range_to)
{
  ObjRange* range = AS_RANGE(args[0]);
  RETURN_NUM(range->to);
}

DEF_NATIVE(range_min)
{
  ObjRange* range = AS_RANGE(args[0]);
  RETURN_NUM(fmin(range->from, range->to));
}

DEF_NATIVE(range_max)
{
  ObjRange* range = AS_RANGE(args[0]);
  RETURN_NUM(fmax(range->from, range->to));
}

DEF_NATIVE(range_isInclusive)
{
  ObjRange* range = AS_RANGE(args[0]);
  RETURN_BOOL(range->isInclusive);
}

DEF_NATIVE(range_iterate)
{
  ObjRange* range = AS_RANGE(args[0]);

  // Special case: empty range.
  if (range->from == range->to && !range->isInclusive) RETURN_FALSE;

  // Start the iteration.
  if (IS_NULL(args[1])) RETURN_NUM(range->from);

  if (!validateNum(vm, args, 1, "Iterator")) return PRIM_ERROR;

  double iterator = AS_NUM(args[1]);

  // Iterate towards [to] from [from].
  if (range->from < range->to)
  {
    iterator++;
    if (iterator > range->to) RETURN_FALSE;
  }
  else
  {
    iterator--;
    if (iterator < range->to) RETURN_FALSE;
  }

  if (!range->isInclusive && iterator == range->to) RETURN_FALSE;

  RETURN_NUM(iterator);
}

DEF_NATIVE(range_iteratorValue)
{
  // Assume the iterator is a number so that is the value of the range.
  RETURN_VAL(args[1]);
}

DEF_NATIVE(range_toString)
{
  char buffer[51];
  ObjRange* range = AS_RANGE(args[0]);
  int length = sprintf(buffer, "%.14g%s%.14g", range->from,
                       range->isInclusive ? ".." : "...", range->to);
  RETURN_VAL(wrenNewString(vm, buffer, length));
}

DEF_NATIVE(string_contains)
{
  if (!validateString(vm, args, 1, "Argument")) return PRIM_ERROR;

  ObjString* string = AS_STRING(args[0]);
  ObjString* search = AS_STRING(args[1]);

  // Corner case, the empty string contains the empty string.
  if (string->length == 0 && search->length == 0) RETURN_TRUE;

  RETURN_BOOL(strstr(string->value, search->value) != NULL);
}

DEF_NATIVE(string_count)
{
  double count = AS_STRING(args[0])->length;
  RETURN_NUM(count);
}

DEF_NATIVE(string_endsWith)
{
  if (!validateString(vm, args, 1, "Argument")) return PRIM_ERROR;

  ObjString* string = AS_STRING(args[0]);
  ObjString* search = AS_STRING(args[1]);

  // Corner case, if the search string is longer than return false right away.
  if (search->length > string->length) RETURN_FALSE;

  int result = memcmp(string->value + string->length - search->length,
                       search->value, search->length);

  RETURN_BOOL(result == 0);
}

DEF_NATIVE(string_indexOf)
{
  if (!validateString(vm, args, 1, "Argument")) return PRIM_ERROR;

  ObjString* string = AS_STRING(args[0]);
  ObjString* search = AS_STRING(args[1]);

  char* firstOccurrence = strstr(string->value, search->value);

  RETURN_NUM(firstOccurrence ? firstOccurrence - string->value : -1);
}

DEF_NATIVE(string_startsWith)
{
  if (!validateString(vm, args, 1, "Argument")) return PRIM_ERROR;

  ObjString* string = AS_STRING(args[0]);
  ObjString* search = AS_STRING(args[1]);

  // Corner case, if the search string is longer than return false right away.
  if (search->length > string->length) RETURN_FALSE;

  RETURN_BOOL(memcmp(string->value, search->value, search->length) == 0);
}

DEF_NATIVE(string_toString)
{
  RETURN_VAL(args[0]);
}

DEF_NATIVE(string_strip)
{
  ObjString* string = AS_STRING(args[0]);
  const char* toStrip;
  if ((args[1]).bits == 0) {
    toStrip = " \n\t";
  } else {
    toStrip = AS_CSTRING(args[1]);
  }
  unsigned int MAX_ARG_LEN = 1024;
  unsigned int toStripLen = strnlen(toStrip, MAX_ARG_LEN);

  unsigned int start = 0;
  unsigned int end = strnlen(string->value, MAX_ARG_LEN) - 1;

  // strip `toStrip` off the beginning of the string
  while (1) {
      for (unsigned int i = 0; i < toStripLen; i++) {
        if (memcmp(&string->value[start], &toStrip[i], 1) == 0) {
          start++;
          break;
        }
      }
      break;
  }

  // strip `toString` off the end of the string
  while (1) {
    for (unsigned int i = 0; i < toStripLen; i++) {
      if (memcmp(&string->value[end], &toStrip[i], 1) == 0) {
        end--;
        break;
      }
    }
    break;
  }

  unsigned int newLen = end - start + 1;
  Value val = wrenNewUninitializedString(vm, newLen);
  ObjString* newString = AS_STRING(val);
  for (unsigned int i = 0; i < newLen; i++) {
    newString->value[i] = string->value[start + i];
  }
  newString->value[newLen] = '\0';
  RETURN_VAL(val);
}

DEF_NATIVE(string_plus)
{
  if (!validateString(vm, args, 1, "Right operand")) return PRIM_ERROR;
  RETURN_OBJ(wrenStringConcat(vm, AS_CSTRING(args[0]), AS_CSTRING(args[1])));
}

DEF_NATIVE(string_eqeq)
{
  if (!IS_STRING(args[1])) RETURN_FALSE;
  ObjString* a = AS_STRING(args[0]);
  ObjString* b = AS_STRING(args[1]);
  RETURN_BOOL(a->length == b->length &&
      memcmp(a->value, b->value, a->length) == 0);
}

DEF_NATIVE(string_bangeq)
{
  if (!IS_STRING(args[1])) RETURN_TRUE;
  ObjString* a = AS_STRING(args[0]);
  ObjString* b = AS_STRING(args[1]);
  RETURN_BOOL(a->length != b->length ||
      memcmp(a->value, b->value, a->length) != 0);
}

DEF_NATIVE(string_subscript)
{
  ObjString* string = AS_STRING(args[0]);

  int index = validateIndex(vm, args, string->length, 1, "Subscript");
  if (index == -1) return PRIM_ERROR;

  // The result is a one-character string.
  // TODO: Handle UTF-8.
  Value value = wrenNewUninitializedString(vm, 1);
  ObjString* result = AS_STRING(value);
  result->value[0] = AS_CSTRING(args[0])[index];
  result->value[1] = '\0';
  RETURN_VAL(value);
}

static ObjClass* defineSingleClass(WrenVM* vm, const char* name)
{
  size_t length = strlen(name);
  ObjString* nameString = AS_STRING(wrenNewString(vm, name, length));
  WREN_PIN(vm, nameString);

  ObjClass* classObj = wrenNewSingleClass(vm, 0, nameString);
  wrenDefineGlobal(vm, name, length, OBJ_VAL(classObj));

  WREN_UNPIN(vm);
  return classObj;
}

static ObjClass* defineClass(WrenVM* vm, const char* name)
{
  size_t length = strlen(name);
  ObjString* nameString = AS_STRING(wrenNewString(vm, name, length));
  WREN_PIN(vm, nameString);

  ObjClass* classObj = wrenNewClass(vm, vm->objectClass, 0, nameString);
  wrenDefineGlobal(vm, name, length, OBJ_VAL(classObj));

  WREN_UNPIN(vm);
  return classObj;
}

// Returns the global variable named [name].
static Value findGlobal(WrenVM* vm, const char* name)
{
  int symbol = wrenSymbolTableFind(&vm->globalNames, name, strlen(name));
  return vm->globals.data[symbol];
}

void wrenInitializeCore(WrenVM* vm)
{
  // Define the root Object class. This has to be done a little specially
  // because it has no superclass and an unusual metaclass (Class).
  vm->objectClass = defineSingleClass(vm, "Object");
  NATIVE(vm->objectClass, "== ", object_eqeq);
  NATIVE(vm->objectClass, "!= ", object_bangeq);
  NATIVE(vm->objectClass, "new", object_new);
  NATIVE(vm->objectClass, "toString", object_toString);
  NATIVE(vm->objectClass, "type", object_type);
  NATIVE(vm->objectClass, " instantiate", object_instantiate);

  // Now we can define Class, which is a subclass of Object, but Object's
  // metaclass.
  vm->classClass = defineSingleClass(vm, "Class");

  // Now that Object and Class are defined, we can wire them up to each other.
  wrenBindSuperclass(vm, vm->classClass, vm->objectClass);
  vm->objectClass->obj.classObj = vm->classClass;
  vm->classClass->obj.classObj = vm->classClass;

  // Define the methods specific to Class after wiring up its superclass to
  // prevent the inherited ones from overwriting them.
  NATIVE(vm->classClass, " instantiate", class_instantiate);
  NATIVE(vm->classClass, "name", class_name);

  // The core class diagram ends up looking like this, where single lines point
  // to a class's superclass, and double lines point to its metaclass:
  //
  //           .------------.    .========.
  //           |            |    ||      ||
  //           v            |    v       ||
  //     .---------.   .--------------.  ||
  //     | Object  |==>|    Class     |==='
  //     '---------'   '--------------'
  //          ^               ^
  //          |               |
  //     .---------.   .--------------.   -.
  //     |  Base   |==>|  Base.type   |    |
  //     '---------'   '--------------'    |
  //          ^               ^            | Hypothetical example classes
  //          |               |            |
  //     .---------.   .--------------.    |
  //     | Derived |==>| Derived.type |    |
  //     '---------'   '--------------'   -'

  // The rest of the classes can not be defined normally.
  vm->boolClass = defineClass(vm, "Bool");
  NATIVE(vm->boolClass, "toString", bool_toString);
  NATIVE(vm->boolClass, "!", bool_not);

  // TODO: Make fibers inherit Sequence and be iterable.
  vm->fiberClass = defineClass(vm, "Fiber");
  NATIVE(vm->fiberClass->obj.classObj, " instantiate", fiber_instantiate);
  NATIVE(vm->fiberClass->obj.classObj, "new ", fiber_new);
  NATIVE(vm->fiberClass->obj.classObj, "abort ", fiber_abort);
  NATIVE(vm->fiberClass->obj.classObj, "yield", fiber_yield);
  NATIVE(vm->fiberClass->obj.classObj, "yield ", fiber_yield1);
  NATIVE(vm->fiberClass, "call", fiber_call);
  NATIVE(vm->fiberClass, "call ", fiber_call1);
  NATIVE(vm->fiberClass, "error", fiber_error);
  NATIVE(vm->fiberClass, "isDone", fiber_isDone);
  NATIVE(vm->fiberClass, "run", fiber_run);
  NATIVE(vm->fiberClass, "run ", fiber_run1);
  NATIVE(vm->fiberClass, "try", fiber_try);

  vm->fnClass = defineClass(vm, "Fn");

  NATIVE(vm->fnClass->obj.classObj, " instantiate", fn_instantiate);
  NATIVE(vm->fnClass->obj.classObj, "new ", fn_new);

  NATIVE(vm->fnClass, "call", fn_call0);
  NATIVE(vm->fnClass, "call ", fn_call1);
  NATIVE(vm->fnClass, "call  ", fn_call2);
  NATIVE(vm->fnClass, "call   ", fn_call3);
  NATIVE(vm->fnClass, "call    ", fn_call4);
  NATIVE(vm->fnClass, "call     ", fn_call5);
  NATIVE(vm->fnClass, "call      ", fn_call6);
  NATIVE(vm->fnClass, "call       ", fn_call7);
  NATIVE(vm->fnClass, "call        ", fn_call8);
  NATIVE(vm->fnClass, "call         ", fn_call9);
  NATIVE(vm->fnClass, "call          ", fn_call10);
  NATIVE(vm->fnClass, "call           ", fn_call11);
  NATIVE(vm->fnClass, "call            ", fn_call12);
  NATIVE(vm->fnClass, "call             ", fn_call13);
  NATIVE(vm->fnClass, "call              ", fn_call14);
  NATIVE(vm->fnClass, "call               ", fn_call15);
  NATIVE(vm->fnClass, "call                ", fn_call16);
  // TODO: "arity" getter.
  NATIVE(vm->fnClass, "toString", fn_toString);

  vm->nullClass = defineClass(vm, "Null");
  NATIVE(vm->nullClass, "toString", null_toString);

  vm->numClass = defineClass(vm, "Num");
  NATIVE(vm->numClass, "abs", num_abs);
  NATIVE(vm->numClass, "ceil", num_ceil);
  NATIVE(vm->numClass, "cos", num_cos);
  NATIVE(vm->numClass, "floor", num_floor);
  NATIVE(vm->numClass, "isNan", num_isNan);
  NATIVE(vm->numClass, "sin", num_sin);
  NATIVE(vm->numClass, "sqrt", num_sqrt);
  NATIVE(vm->numClass, "toString", num_toString)
  NATIVE(vm->numClass, "-", num_negate);
  NATIVE(vm->numClass, "- ", num_minus);
  NATIVE(vm->numClass, "+ ", num_plus);
  NATIVE(vm->numClass, "* ", num_multiply);
  NATIVE(vm->numClass, "/ ", num_divide);
  NATIVE(vm->numClass, "% ", num_mod);
  NATIVE(vm->numClass, "< ", num_lt);
  NATIVE(vm->numClass, "> ", num_gt);
  NATIVE(vm->numClass, "<= ", num_lte);
  NATIVE(vm->numClass, ">= ", num_gte);
  NATIVE(vm->numClass, "~", num_bitwiseNot);
  NATIVE(vm->numClass, "& ", num_bitwiseAnd);
  NATIVE(vm->numClass, "| ", num_bitwiseOr);
  NATIVE(vm->numClass, ".. ", num_dotDot);
  NATIVE(vm->numClass, "... ", num_dotDotDot);

  vm->stringClass = defineClass(vm, "String");
  NATIVE(vm->stringClass, "contains ", string_contains);
  NATIVE(vm->stringClass, "count", string_count);
  NATIVE(vm->stringClass, "endsWith ", string_endsWith);
  NATIVE(vm->stringClass, "indexOf ", string_indexOf);
  NATIVE(vm->stringClass, "startsWith ", string_startsWith);
  NATIVE(vm->stringClass, "toString", string_toString);
  NATIVE(vm->stringClass, "strip ", string_strip);
  NATIVE(vm->stringClass, "strip", string_strip);
  NATIVE(vm->stringClass, "+ ", string_plus);
  NATIVE(vm->stringClass, "== ", string_eqeq);
  NATIVE(vm->stringClass, "!= ", string_bangeq);
  NATIVE(vm->stringClass, "[ ]", string_subscript);

  // When the base classes are defined, we allocate string objects for their
  // names. However, we haven't created the string class itself yet, so those
  // all have NULL class pointers. Now that we have a string class, go back and
  // fix them up.
  vm->objectClass->name->obj.classObj = vm->stringClass;
  vm->classClass->name->obj.classObj = vm->stringClass;
  vm->boolClass->name->obj.classObj = vm->stringClass;
  vm->fiberClass->name->obj.classObj = vm->stringClass;
  vm->fnClass->name->obj.classObj = vm->stringClass;
  vm->nullClass->name->obj.classObj = vm->stringClass;
  vm->numClass->name->obj.classObj = vm->stringClass;
  vm->stringClass->name->obj.classObj = vm->stringClass;

  wrenInterpret(vm, "", libSource);

  vm->listClass = AS_CLASS(findGlobal(vm, "List"));
  NATIVE(vm->listClass->obj.classObj, " instantiate", list_instantiate);
  NATIVE(vm->listClass, "add ", list_add);
  NATIVE(vm->listClass, "clear", list_clear);
  NATIVE(vm->listClass, "count", list_count);
  NATIVE(vm->listClass, "insert  ", list_insert);
  NATIVE(vm->listClass, "iterate ", list_iterate);
  NATIVE(vm->listClass, "iteratorValue ", list_iteratorValue);
  NATIVE(vm->listClass, "removeAt ", list_removeAt);
  NATIVE(vm->listClass, "[ ]", list_subscript);
  NATIVE(vm->listClass, "[ ]=", list_subscriptSetter);

  vm->rangeClass = AS_CLASS(findGlobal(vm, "Range"));
  NATIVE(vm->rangeClass, "from", range_from);
  NATIVE(vm->rangeClass, "to", range_to);
  NATIVE(vm->rangeClass, "min", range_min);
  NATIVE(vm->rangeClass, "max", range_max);
  NATIVE(vm->rangeClass, "isInclusive", range_isInclusive);
  NATIVE(vm->rangeClass, "iterate ", range_iterate);
  NATIVE(vm->rangeClass, "iteratorValue ", range_iteratorValue);
  NATIVE(vm->rangeClass, "toString", range_toString);

  // These are defined just so that 0 and -0 are equal, which is specified by
  // IEEE 754 even though they have different bit representations.
  NATIVE(vm->numClass, "== ", num_eqeq);
  NATIVE(vm->numClass, "!= ", num_bangeq);
}
