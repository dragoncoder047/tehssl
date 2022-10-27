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
    RETURN,
    BREAK,
    CONTINUE,
    OUT_OF_MEMORY
};

typedef uint16_t tehssl_flags_t;
enum tehssl_flag_t {
    GC_MARK,
    PRINTER_MARK,
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
    LINE,        //   (prev)       (item)       (next)
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
            switch (unreached->type) {
                case SYMBOL:
                case STRING:
                case STREAM:
                case UFUNCTION:
                case CFUNCTION:
                case VARIABLE:
                    #if TEHSSL_DEBUG == 1
                    printf(" string: %s", unreached->name);
                    #endif
                    free(unreached->name);
                default:
                    break;
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

inline void tehssl_pop(struct tehssl_object_t** stack) {
    *stack = (*stack)->next;
}

// Register C functions

void tehssl_register(struct tehssl_vm_t* vm, const char* name, tehssl_fun_t fun) {
    if (vm->global_scope == NULL) {
        vm->global_scope = tehssl_alloc(vm, SCOPE);
    }
    struct tehssl_object_t* fobj = tehssl_alloc(vm, CFUNCTION);
    fobj->name = mystrdup(name);
    fobj->fun = fun;
    fobj->next = vm->global_scope->functions;
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
