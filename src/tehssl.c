#ifdef __cplusplus
#include <cstring>
#include <iostream>
#define TEHSSL_DEBUG 1
#else
#include <stdlib.h>
#include <string.h>
#define TEHSSL_DEBUG 0
#endif

// Config options
#ifndef TEHSSL_MIN_HEAP_SIZE
#define TEHSSL_MIN_HEAP_SIZE 64
#endif

// Polyfills
char* mystrdup(const char* str) {
    size_t len = strlen(str) + 1;
    char* dup = (char *)malloc(len);
    strncpy(dup, str, len);
    return dup;
}

// Forward references
struct tehssl_vm_t;
size_t tehssl_gc(struct tehssl_vm_t*);

// Datatypes

enum tehssl_result_t {
    OK,
    ERROR,
    RETURN,
    BREAK,
    CONTINUE,
    OUT_OF_MEMORY
};

typedef uint16_t tehssl_flags_t;
enum tehssl_flag_t {
    GC_MARK,
    PRINTER_MARK,
    MACRO_FUNCTION,
    LITERAL_SYMBOL
};

typedef void (*tehssl_pfun_t)(char);
typedef char (*tehssl_gfun_t)(void);
typedef tehssl_result_t (*tehssl_fun_t)(struct tehssl_vm_t*);

// Different types
enum tehssl_typeid_t {
//  NAME              CAR          CDR          NEXT
    LIST,        //                (value)      (next)
    DICT,        //   (key)        (value)      (next)
    LINE,        //                (item)       (next)
    LAMBDA,      //                (code)       (next)
    CLOSURE,     //   (scope)      (lambda)
    NUMBER,      //   double
    SYMBOL,      //   char*
    STRING,      //   char*
    STREAM,      //   char*        pfun_t       gfun_t              char* is name
    // Special internal types
    SCOPE,       //   (functions)  (variables)  (parent)
    UFUNCTION,   //   char*        (lambda)     (next)
    CFUNCTION,   //   char*        tehssl_fun_t (next)
    VARIABLE     //   char*        (value)      (next)
};
// N.B. the char* pointers are "owned" by the object and MUST be strcpy()'d if the object is duplicated.

#if TEHSSL_DEBUG == 1
void debug_print_type(tehssl_typeid_t t) {
    switch (t) {
        case LIST: printf("LIST"); break;
        case DICT: printf("DICT"); break;
        case LINE: printf("LINE"); break;
        case LAMBDA: printf("LAMBDA"); break;
        case CLOSURE: printf("CLOSURE"); break;
        case NUMBER: printf("NUMBER"); break;
        case SYMBOL: printf("SYMBOL"); break;
        case STRING: printf("STRING"); break;
        case STREAM: printf("STREAM"); break;
        case SCOPE: printf("SCOPE"); break;
        case UFUNCTION: printf("UFUNCTION"); break;
        case CFUNCTION: printf("CFUNCTION"); break;
        case VARIABLE: printf("VARIABLE"); break;
    }
}
#endif

// Main OBJECT type
struct tehssl_object_t {
    tehssl_typeid_t type;
    tehssl_flags_t flags;
    struct tehssl_object_t* next_object;
    union {
        double number;
        struct {
            union {
                struct tehssl_object_t* key;
                struct tehssl_object_t* scope;
                struct tehssl_object_t* functions;
                char* name;
            };
            union {
                struct tehssl_object_t* value;
                struct tehssl_object_t* item;
                struct tehssl_object_t* code;
                struct tehssl_object_t* variables;
                struct tehssl_object_t* lambda;
                tehssl_pfun_t pfun;
                tehssl_fun_t fun;
            };
            union {
                struct tehssl_object_t* next;
                struct tehssl_object_t* parent;
                tehssl_gfun_t gfun;
            };
        };
    };
};

inline bool tehssl_has_name(struct tehssl_object_t* object) {
    switch (object->type) {
        case SYMBOL:
        case STRING:
        case STREAM:
        case UFUNCTION:
        case CFUNCTION:
        case VARIABLE:
            return true;
        default:
            return false;
    }
}

inline bool tehssl_is_literal(struct tehssl_object_t* object) {
    switch (object->type) {
        case SYMBOL:
            return tehssl_test_flag(object, LITERAL_SYMBOL);
        case LIST:
        case DICT:
        case LINE:
        case LAMBDA:
        case STRING:
        case STREAM:
        case NUMBER:
            return true;
        default:
            return false;
    }
}


// TEHSSL VM type
struct tehssl_vm_t {
    struct tehssl_object_t* stack;
    struct tehssl_object_t* return_value;
    struct tehssl_object_t* global_scope;
    struct tehssl_object_t* gc_stack;
    tehssl_result_t result_code;
    struct tehssl_object_t* first_object;
    size_t num_objects;
    size_t next_gc;
    char last_char;
};

// Flags test
void tehssl_set_flag(struct tehssl_object_t* object, tehssl_flag_t f) { object->flags |= (1 << f); }
void tehssl_clear_flag(struct tehssl_object_t* object, tehssl_flag_t f) { object->flags &= ~(1 << f); }
bool tehssl_test_flag(struct tehssl_object_t* object, tehssl_flag_t f) { return object->flags & (1 << f); }


// Alloc
struct tehssl_vm_t* tehssl_new_vm() {
    struct tehssl_vm_t* vm = (struct tehssl_vm_t*)malloc(sizeof(struct tehssl_vm_t));
    vm->stack = NULL;
    vm->return_value = NULL;
    vm->global_scope = NULL;
    vm->gc_stack = NULL;
    vm->first_object = NULL;
    vm->result_code = OK;
    vm->num_objects = 0;
    vm->next_gc = TEHSSL_MIN_HEAP_SIZE;
    return vm;
}

struct tehssl_object_t* tehssl_alloc(struct tehssl_vm_t* vm, tehssl_typeid_t type) {
    if (vm->num_objects == vm->next_gc) tehssl_gc(vm);
    struct tehssl_object_t* object = (struct tehssl_object_t*)malloc(sizeof(struct tehssl_object_t));
    if (!object) {
        vm->result_code = OUT_OF_MEMORY;
        return NULL;
    }
    memset(object, 0, sizeof(struct tehssl_object_t));
    object->type = type;
    object->next_object = vm->first_object;
    vm->first_object = object;
    vm->num_objects++;
    return object;
}

// Garbage collection
void tehssl_markobject(struct tehssl_object_t* object) {
    MARK:
    // already marked? abort
    if (object == NULL) return;
    if (tehssl_test_flag(object, GC_MARK)) return;
    tehssl_set_flag(object, GC_MARK);
    switch (object->type) {
        case DICT:
        case SCOPE:
        case LINE:
            tehssl_markobject(object->key);
            // fallthrough
        case LIST:
        case LAMBDA:
            tehssl_markobject(object->value);
            object = object->next;
            goto MARK;
        case CLOSURE:
            tehssl_markobject(object->scope);
            object = object->lambda;
            goto MARK;
        case NUMBER:
        case SYMBOL:
        case STRING:
        case STREAM:
            break; // noop
        case UFUNCTION:
        case VARIABLE:
            tehssl_markobject(object->value);
            // fallthrough
        case CFUNCTION:
            object = object->next;
            goto MARK;
    }
}

void tehssl_markall(struct tehssl_vm_t* vm) {
    tehssl_markobject(vm->stack);
    tehssl_markobject(vm->return_value);
    tehssl_markobject(vm->global_scope);
    tehssl_markobject(vm->gc_stack);
}

void tehssl_sweep(struct tehssl_vm_t* vm) {
    tehssl_object_t** object = &vm->first_object;
    while (*object) {
        if (!tehssl_test_flag(*object, GC_MARK)) {
            tehssl_object_t* unreached = *object;
            *object = unreached->next_object;
            #if TEHSSL_DEBUG == 1
            printf("Freeing a "); debug_print_type(unreached->type);
            #endif
            if (tehssl_has_name(unreached)) {
                #if TEHSSL_DEBUG == 1
                printf(" string: %s", unreached->name);
                #endif
                free(unreached->name);
            }
            #if TEHSSL_DEBUG == 1
            putchar('\n');
            #endif
            free(unreached);
            vm->num_objects--;
        } else {
            tehssl_clear_flag(*object, GC_MARK);
            object = &(*object)->next_object;
        }
    }
}

size_t tehssl_gc(struct tehssl_vm_t* vm) {
    size_t n = vm->num_objects;
    tehssl_markall(vm);
    tehssl_sweep(vm);
    vm->next_gc = vm->num_objects == 0 ? TEHSSL_MIN_HEAP_SIZE : vm->num_objects * 2;
    return n - vm->num_objects;
}

void tehssl_destroy(struct tehssl_vm_t* vm) {
    vm->stack = NULL;
    vm->return_value = NULL;
    vm->global_scope = NULL;
    vm->gc_stack = NULL;
    tehssl_gc(vm);
    free(vm);
}

// Push / Pop (for stacks)

inline void tehssl_push(struct tehssl_vm_t* vm, struct tehssl_object_t** stack, struct tehssl_object_t* item) {
    struct tehssl_object_t* cell = tehssl_alloc(vm, LIST);
    cell->value = item;
    cell->next = *stack;
    *stack = cell;
}

inline struct tehssl_object_t* tehssl_pop(struct tehssl_object_t** stack) {
    if (*stack == NULL) return NULL;
    struct tehssl_object_t* item = (*stack)->value;
    *stack = (*stack)->next;
    return item;
}

// Stream read and write functions

char tehssl_getchar(struct tehssl_vm_t* vm, tehssl_gfun_t gfun) {
    if (vm->last_char) {
        char ch = vm->last_char;
        vm->last_char = 0;
        return ch;
    }
    return gfun();
}

void tehssl_putchar(char ch, tehssl_pfun_t pfun) {
    pfun(ch);
}

char tehssl_peekchar(struct tehssl_vm_t* vm, tehssl_gfun_t gfun) {
    if (vm->last_char) return vm->last_char;
    vm->last_char = gfun();
    return vm->last_char;
}

// Make objects

struct tehssl_object_t* tehssl_make_string(struct tehssl_vm_t* vm, const char* string) {
    struct tehssl_object_t* sobj = tehssl_alloc(vm, STRING);
    sobj->name = mystrdup(string);
    return sobj;
}

#define SYMBOL_LITERAL true
#define SYMBOL_WORD false
struct tehssl_object_t* tehssl_make_symbol(struct tehssl_vm_t* vm, const char* name, bool is_literal) {
    struct tehssl_object_t* symbol = tehssl_make_string(vm, name);
    symbol->type = SYMBOL;
    if (is_literal) tehssl_set_flag(symbol, LITERAL_SYMBOL);
    return symbol;
}

struct tehssl_object_t* tehssl_make_number(struct tehssl_vm_t* vm, double n) {
    struct tehssl_object_t* sobj = tehssl_alloc(vm, NUMBER);
    sobj->number = n;
    return sobj;
}


// Lookup values in scope

#define LOOKUP_FUNCTION true
#define LOOKUP_VARIABLE false
struct tehssl_object_t* tehssl_lookup(struct tehssl_object_t* scope, char* name, bool where) {
    if (scope == NULL || scope->type != SCOPE) return NULL;
    struct tehssl_object_t* result = NULL;
    if (where == LOOKUP_FUNCTION) result = scope->functions;
    else result = scope->variables;
    while (result != NULL) {
        if (strcmp(result->name, name) == 0) break;
        result = result->next;
    }
    return result;
}


// Evaluator

// This also reverses the line, so it can be evaluated right-to-left, but read in and stored left-to-right
tehssl_result_t tehssl_macro_preprocess(struct tehssl_vm_t* vm, struct tehssl_object_t* scope, struct tehssl_object_t* line) {
    struct tehssl_object_t* processed_line = NULL;
    while (line != NULL) {
        struct tehssl_object_t* item = line->value;
        if (!tehssl_has_name(item) || tehssl_is_literal(item)) {
            tehssl_push(vm, &processed_line, item);
            line = line->next;
            continue;
        }
        struct tehssl_object_t* macro = tehssl_lookup(item, item->name, LOOKUP_FUNCTION);
        if (macro == NULL || !tehssl_test_flag(macro, MACRO_FUNCTION)) {
            tehssl_push(vm, &processed_line, item);
            line = line->next;
            continue;
        }
        if (macro->type == UFUNCTION) {
            // abort
            vm->result = tehssl_make_string(vm, "todo: user-defined macros");
            return ERROR;
        } else if (macro->type == CFUNCTION) {
            tehssl_push(vm, &vm->stack, line);
            macro->fun(vm);
            line = tehssl_pop(&vm->stack);
            tehssl_push(vm, &processed_line, line->value);
            line = line->next;
        } else {
            // something's wrong
            vm->result = tehssl_make_string(vm, "error: defined non-function as a macro");
            return ERROR;
        }
    }
    return processed_line;
}

// Register C functions

#define IS_MACRO true
#define NOT_MACRO false
void tehssl_register(struct tehssl_vm_t* vm, const char* name, tehssl_fun_t fun, bool is_macro) {
    if (vm->global_scope == NULL) {
        vm->global_scope = tehssl_alloc(vm, SCOPE);
    }
    struct tehssl_object_t* fobj = tehssl_alloc(vm, CFUNCTION);
    fobj->name = mystrdup(name);
    fobj->fun = fun;
    fobj->next = vm->global_scope->functions;
    if (is_macro) tehssl_set_flag(fobj, MACRO_FUNCTION);
    vm->global_scope->functions = fobj;
}

void tehssl_init_builtins(struct tehssl_vm_t* vm) {
    // TODO
}

#if TEHSSL_DEBUG == 1
// Test code
// for pasting into https://cpp.sh/
tehssl_result_t myfunction(struct tehssl_vm_t*) { return OK; }
int main() {
    struct tehssl_vm_t* vm = tehssl_new_vm();
    // Make some garbage
    tehssl_alloc(vm, NUMBER);
    tehssl_alloc(vm, NUMBER);
    tehssl_alloc(vm, NUMBER);
    tehssl_alloc(vm, NUMBER);
    // This is not garbage, it is on the stack now
    tehssl_push(vm, &vm->stack, tehssl_alloc(vm, NUMBER));
    tehssl_push(vm, &vm->stack, tehssl_alloc(vm, NUMBER));
    tehssl_register(vm, "MyFunction", myfunction);
    printf("%lu objects\n", vm->num_objects);
    tehssl_gc(vm);
    printf("%lu objects after gc\n", vm->num_objects);
    tehssl_destroy(vm);
}
#endif
