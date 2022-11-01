#ifdef __cplusplus
#include <cstring>
#include <cstdio>
#include <iostream>
#define TEHSSL_DEBUG
#else
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#undef TEHSSL_DEBUG
#endif

// Config options
#ifndef TEHSSL_MIN_HEAP_SIZE
#define TEHSSL_MIN_HEAP_SIZE 64
#endif

#ifndef TEHSSL_CHUNK_SIZE
#define TEHSSL_CHUNK_SIZE 128
#endif

// Polyfills
char* mystrdup(const char* str) {
    size_t len = strlen(str) + 1;
    char* dup = (char *)malloc(len);
    strncpy(dup, str, len);
    return dup;
}

// Datatypes
enum tehssl_result {
    OK,
    ERROR,
    RETURN,
    BREAK,
    CONTINUE,
    OUT_OF_MEMORY
};

#define TEHSSL_RETURN_ON_ERROR(r) do { if ((r) == ERROR || (r) == OUT_OF_MEMORY) return (r); } while (false)
#define TEHSSL_POP_AND_RETURN_ON_ERROR(r, stack) do { if ((r) == ERROR || (r) == OUT_OF_MEMORY) tehssl_pop(&(stack)); return (r); } while (false)

enum tehssl_flag {
    GC_MARK,
    PRINTER_MARK,
    MACRO_FUNCTION,
    LITERAL_SYMBOL
};

// Different types
enum tehssl_typeid {
//  Type      Cell--> A            B            C
    LIST,        //                (value)      (next)
    DICT,        //   (key)        (value)      (next)
    LINE,        //                (item)       (next)
    BLOCK,       //   (parent)     (code)       (next)
    NUMBER,      //   double
    SYMBOL,      //   char*
    STRING,      //   char*
    STREAM,      //   char*        FILE*                     char* is name
    USERTYPE,    //   char*        (ptr1)       (ptr2)
    // Special internal types
    SCOPE,       //   (functions)  (variables)  (parent)
    UFUNCTION,   //   char*        (block)      (next)
    CFUNCTION,   //   char*        cfun_t       (next)
    TFUNCTION,   //   char*        typefun_t    (next)
    VARIABLE,    //   char*        (value)      (next)
    TOKEN        //   char*
};
// N.B. the char* pointers are "owned" by the object and MUST be strcpy()'d if the object is duplicated.

enum tehssl_type_action {
    CTYPE_INIT,
    CTYPE_PRINT,
    CTYPE_MARK,
    CTYPE_COMPARE,
    CTYPE_DESTROY
};

// Typedefs
typedef enum tehssl_typeid tehssl_typeid_t;
typedef enum tehssl_flag tehssl_flag_t;
typedef enum tehssl_result tehssl_result_t;
typedef enum tehssl_type_action tehssl_type_action_t;
typedef struct tehssl_object *tehssl_object_t;
typedef struct tehssl_vm *tehssl_vm_t;
typedef tehssl_result_t (*tehssl_cfun_t)(tehssl_vm_t, tehssl_object_t);
typedef int (*tehssl_typefun_t)(tehssl_vm_t, tehssl_type_action_t, tehssl_object_t, void*);
typedef uint16_t tehssl_flags_t;

// Main OBJECT type
struct tehssl_object {
    tehssl_typeid_t type;
    tehssl_flags_t flags;
    tehssl_object_t next_object;
    union {
        double number;
        struct {
            union {
                tehssl_object_t key;
                tehssl_object_t scope;
                tehssl_object_t functions;
                char* name;
            };
            union {
                tehssl_object_t value;
                tehssl_object_t item;
                tehssl_object_t code;
                tehssl_object_t variables;
                tehssl_object_t block;
                tehssl_object_t ptr1;
                FILE* file;
                tehssl_cfun_t c_function;
                tehssl_typefun_t type_function;
            };
            union {
                tehssl_object_t next;
                tehssl_object_t parent;
                tehssl_object_t ptr2;
            };
        };
    };
};

// TEHSSL VM type
struct tehssl_vm {
    tehssl_object_t stack;
    tehssl_object_t return_value;
    tehssl_object_t global_scope;
    tehssl_object_t gc_stack;
    tehssl_result_t result_code;
    tehssl_object_t first_object;
    tehssl_object_t type_functions;
    size_t num_objects;
    size_t next_gc;
    char last_char;
};

// Forward references
size_t tehssl_gc(tehssl_vm_t);
bool tehssl_test_flag(tehssl_object_t object, tehssl_flag_t f);

#ifdef TEHSSL_DEBUG
void debug_print_type(tehssl_typeid_t t) {
    switch (t) {
        case LIST: printf("LIST"); break;
        case DICT: printf("DICT"); break;
        case LINE: printf("LINE"); break;
        case BLOCK: printf("BLOCK"); break;
        case NUMBER: printf("NUMBER"); break;
        case SYMBOL: printf("SYMBOL"); break;
        case STRING: printf("STRING"); break;
        case STREAM: printf("STREAM"); break;
        case USERTYPE: printf("USERTYPE"); break;
        case SCOPE: printf("SCOPE"); break;
        case UFUNCTION: printf("UFUNCTION"); break;
        case CFUNCTION: printf("CFUNCTION"); break;
        case TFUNCTION: printf("TFUNCTION"); break;
        case VARIABLE: printf("VARIABLE"); break;
        case TOKEN: printf("TOKEN"); break;
    }
}
#endif

inline bool tehssl_has_name(tehssl_object_t object) {
    switch (object->type) {
        case SYMBOL:
        case STRING:
        case STREAM:
        case USERTYPE:
        case UFUNCTION:
        case CFUNCTION:
        case VARIABLE:
        case TOKEN:
            return true;
        default:
            return false;
    }
}

inline bool tehssl_is_literal(tehssl_object_t object) {
    switch (object->type) {
        case SYMBOL:
            return tehssl_test_flag(object, LITERAL_SYMBOL);
        case LIST:
        case DICT:
        case STRING:
        case STREAM:
        case NUMBER:
            return true;
        default:
            return false;
    }
}

// Flags test
void tehssl_set_flag(tehssl_object_t object, tehssl_flag_t f) { object->flags |= (1 << f); }
void tehssl_clear_flag(tehssl_object_t object, tehssl_flag_t f) { object->flags &= ~(1 << f); }
bool tehssl_test_flag(tehssl_object_t object, tehssl_flag_t f) { return object->flags & (1 << f); }

// Alloc
tehssl_vm_t tehssl_new_vm() {
    tehssl_vm_t vm = (tehssl_vm_t)malloc(sizeof(struct tehssl_vm));
    vm->stack = NULL;
    vm->return_value = NULL;
    vm->global_scope = NULL;
    vm->gc_stack = NULL;
    vm->type_functions = NULL;
    vm->first_object = NULL;
    vm->result_code = OK;
    vm->num_objects = 0;
    vm->next_gc = TEHSSL_MIN_HEAP_SIZE;
    return vm;
}

tehssl_object_t tehssl_alloc(tehssl_vm_t vm, tehssl_typeid_t type) {
    if (vm->num_objects == vm->next_gc) tehssl_gc(vm);
    tehssl_object_t object = (tehssl_object_t)malloc(sizeof(struct tehssl_object));
    if (!object) {
        vm->result_code = OUT_OF_MEMORY;
        return NULL;
    }
    memset(object, 0, sizeof(struct tehssl_object));
    object->type = type;
    object->next_object = vm->first_object;
    vm->first_object = object;
    vm->num_objects++;
    return object;
}

// Garbage collection
tehssl_object_t tehssl_get_type_handle(tehssl_vm_t vm, char* type) {
    tehssl_object_t type_handle = vm->type_functions;
    while (type_handle != NULL) {
        if (strcmp(type, type_handle->name) == 0) break;
        type_handle = type_handle->next;
    }
    return type_handle;
}

void tehssl_markobject(tehssl_vm_t vm, tehssl_object_t object) {
    MARK:
    // already marked? abort
    if (object == NULL) return;
    if (tehssl_test_flag(object, GC_MARK)) return;
    tehssl_set_flag(object, GC_MARK);
    switch (object->type) {
        case DICT:
        case SCOPE:
        // case LINE:
        case BLOCK:
            tehssl_markobject(vm, object->key);
            // fallthrough
        case LIST:
        case LINE:
            tehssl_markobject(vm, object->value);
            object = object->next;
            goto MARK;
        case NUMBER:
        case SYMBOL:
        case STRING:
        case STREAM:
        case TOKEN:
            break; // noop
        case UFUNCTION:
        case VARIABLE:
            tehssl_markobject(vm, object->value);
            // fallthrough
        case CFUNCTION:
        case TFUNCTION:
            object = object->next;
            goto MARK;
        case USERTYPE:
            // find type handle function
            tehssl_object_t type_handle = tehssl_get_type_handle(vm, object->name);
            if (type_handle == NULL) {
                #ifdef TEHSSL_DEBUG
                printf("\nWARNING: marking a custom type '%s' with no registered handler function... may cause dangling pointers\n", object->name);
                #endif
            } else {
                type_handle->type_function(vm, CTYPE_MARK, object, NULL);
            }
    }
}

void tehssl_markall(tehssl_vm_t vm) {
    tehssl_markobject(vm, vm->stack);
    tehssl_markobject(vm, vm->return_value);
    tehssl_markobject(vm, vm->global_scope);
    tehssl_markobject(vm, vm->gc_stack);
    tehssl_markobject(vm, vm->type_functions);
}

void tehssl_sweep(tehssl_vm_t vm) {
    tehssl_object_t* object = &vm->first_object;
    while (*object) {
        if (!tehssl_test_flag(*object, GC_MARK)) {
            tehssl_object_t unreached = *object;
            *object = unreached->next_object;
            #ifdef TEHSSL_DEBUG
            printf("Freeing a "); debug_print_type(unreached->type);
            #endif
            if (unreached->type == STREAM) {
                #ifdef TEHSSL_DEBUG
                printf(" +FILE");
                #endif
                if (unreached->file != NULL) fclose(unreached->file);
                free(unreached->file);
                unreached->file = NULL;
            }
            else if (unreached->type == USERTYPE) {
                // find type handle function
                tehssl_object_t type_handle = tehssl_get_type_handle(vm, unreached->name);
                if (type_handle == NULL) {
                    #ifdef TEHSSL_DEBUG
                    printf("\nWARNING: freeing a custom type '%s' with no registered handler function... may cause memory leaks\n", unreached->name);
                    #endif
                } else {
                    free(unreached->name);
                    unreached->name = NULL;
                    type_handle->type_function(vm, CTYPE_DESTROY, unreached, NULL);
                }
            }
            if (tehssl_has_name(unreached)) {
                #ifdef TEHSSL_DEBUG
                printf(" name-> \"%s\"", unreached->name);
                #endif
                free(unreached->name);
                unreached->name = NULL;
            }
            #ifdef TEHSSL_DEBUG
            if (unreached->type == NUMBER) printf(" number-> %g", unreached->number);
            #endif
            free(unreached);
            putchar('\n');
            vm->num_objects--;
        } else {
            tehssl_clear_flag(*object, GC_MARK);
            object = &(*object)->next_object;
        }
    }
}

size_t tehssl_gc(tehssl_vm_t vm) {
    size_t n = vm->num_objects;
    tehssl_markall(vm);
    tehssl_sweep(vm);
    vm->next_gc = vm->num_objects == 0 ? TEHSSL_MIN_HEAP_SIZE : vm->num_objects * 2;
    return n - vm->num_objects;
}

void tehssl_destroy(tehssl_vm_t vm) {
    vm->stack = NULL;
    vm->return_value = NULL;
    vm->global_scope = NULL;
    vm->gc_stack = NULL;
    tehssl_gc(vm);
    free(vm);
}

// Push / Pop (for stacks)
inline void tehssl_push(tehssl_vm_t vm, tehssl_object_t* stack, tehssl_object_t item, tehssl_typeid_t t) {
    tehssl_object_t cell = tehssl_alloc(vm, t);
    cell->value = item;
    cell->next = *stack;
    *stack = cell;
}

inline void tehssl_push(tehssl_vm_t vm, tehssl_object_t* stack, tehssl_object_t item) {
    tehssl_push(vm, stack, item, LIST);
}

inline tehssl_object_t tehssl_pop(tehssl_object_t* stack) {
    if (*stack == NULL) return NULL;
    tehssl_object_t item = (*stack)->value;
    *stack = (*stack)->next;
    return item;
}

// Make objects
tehssl_object_t tehssl_make_string(tehssl_vm_t vm, const char* string) {
    tehssl_object_t object = vm->first_object;
    while (object != NULL) {
        if (object->type == STRING && strcmp(object->name, string) == 0) return object;
        object = object->next_object;
    }
    tehssl_object_t sobj = tehssl_alloc(vm, STRING);
    sobj->name = mystrdup(string);
    return sobj;
}

#define SYMBOL_LITERAL true
#define SYMBOL_WORD false
tehssl_object_t tehssl_make_symbol(tehssl_vm_t vm, const char* name, bool is_literal) {
    tehssl_object_t object = vm->first_object;
    while (object != NULL) {
        if (object->type == SYMBOL && strcmp(object->name, name) == 0 && tehssl_test_flag(object, LITERAL_SYMBOL) == is_literal) return object;
        object = object->next_object;
    }
    tehssl_object_t sobj = tehssl_alloc(vm, SYMBOL);
    sobj->name = mystrdup(name);
    if (is_literal) tehssl_set_flag(sobj, LITERAL_SYMBOL);
    return sobj;
}

tehssl_object_t tehssl_make_number(tehssl_vm_t vm, double n) {
    tehssl_object_t object = vm->first_object;
    while (object != NULL) {
        if (object->type == NUMBER && (uint64_t)n == (uint64_t)object->number) return object;
        object = object->next_object;
    }
    tehssl_object_t sobj = tehssl_alloc(vm, NUMBER);
    sobj->number = n;
    return sobj;
}

tehssl_object_t tehssl_make_stream(tehssl_vm_t vm, char* name, FILE* file) {
    tehssl_object_t sobj = tehssl_alloc(vm, STREAM);
    sobj->name = mystrdup(name);
    sobj->file = file;
    return sobj;
}

tehssl_object_t tehssl_make_custom(tehssl_vm_t vm, char* type, void* data) {
    tehssl_object_t obj = tehssl_alloc(vm, USERTYPE);
    tehssl_object_t type_handle = tehssl_get_type_handle(vm, type);
    if (type_handle == NULL) {
        #ifdef TEHSSL_DEBUG
        printf("\nWARNING: allocating a custom type '%s' with no registered type function\n", type);
        #endif
    }
    else type_handle->type_function(vm, CTYPE_INIT, obj, data);
    return obj;
}

// Lookup values in scope
#define LOOKUP_FUNCTION 0
#define LOOKUP_MACRO 1
#define LOOKUP_VARIABLE 2
tehssl_object_t tehssl_lookup(tehssl_object_t scope, char* name, uint8_t where) {
    LOOKUP:
    if (scope == NULL || scope->type != SCOPE) return NULL;
    tehssl_object_t result = NULL;
    if (where == LOOKUP_VARIABLE) result = scope->variables;
    else result = scope->functions;
    while (result != NULL) {
        if (strcmp(result->name, name) == 0 && (where != LOOKUP_MACRO || tehssl_test_flag(result, MACRO_FUNCTION))) return result;
        result = result->next;
    }
    scope = scope->parent;
    goto LOOKUP;
}

// Helper functions
tehssl_result_t tehssl_error(tehssl_vm_t vm, const char* message) {
    vm->return_value = tehssl_make_string(vm, message);
    return ERROR;
}

tehssl_result_t tehssl_error(tehssl_vm_t vm, const char* message, char* detail) {
    char* buf;
    asprintf(&buf, "%s: %s", message, detail);
    vm->return_value = tehssl_alloc(vm, STRING);
    vm->return_value->name = buf;
    return ERROR;
}

bool tehssl_compare_numbers(double a, double b, bool lt, bool eq, bool gt) {
    if (a < b) return lt;
    if (a == b) return eq;
    if (a > b) return gt;
    return false; // unreachable, just to satisfy compiler
}

bool tehssl_compare_strings(char* a, char* b, bool lt, bool eq, bool gt) {
    int r = strcmp(a, b);
    if (r < 0) return lt;
    if (r == 0) return eq;
    if (r > 0) return gt;
    return false; // unreachable, just to satisfy compiler
}

bool tehssl_equal(tehssl_vm_t vm, tehssl_object_t a, tehssl_object_t b) {
    CMP:
    if (a == b) return true; // Same object
    if (a == NULL || b == NULL) return false; // Null
    if (a->type != b->type) return false; // Different type
    switch (a->type) {
        case DICT:
        case SCOPE:
        case UFUNCTION:
        case VARIABLE:
        // case LINE:
        case BLOCK:
            if (!tehssl_equal(vm, a->key, b->key)) return false;
            // fallthrough
        case LIST:
        case LINE:
        case CFUNCTION:
        case TFUNCTION:
            if (!tehssl_equal(vm, a->value, b->value)) return false;
            a = a->next;
            b = b->next;
            goto CMP;
        case NUMBER:
            return tehssl_compare_numbers(a->number, b->number, false, true, false);
        case SYMBOL:
        case STRING:
        case STREAM:
            return tehssl_compare_strings(a->name, b->name, false, true, false);
        case USERTYPE:
            if (strcmp(a->name, b->name) != 0) return false;
            tehssl_object_t type_handle = tehssl_get_type_handle(vm, a->name);
            if (type_handle == NULL) {
                #ifdef TEHSSL_DEBUG
                printf("\nWARNING: comparing a custom type '%s' with no registered type function\n", a->name);
                #endif
                return true;
            }
            return type_handle->type_function(vm, CTYPE_COMPARE, a, (void*)b) == 0;
    }
}

int tehssl_list_length(tehssl_object_t list) {
    int sz = 0;
    while (list != NULL) {
        sz++;
        list = list->next;
    }
    return sz;
}

tehssl_object_t tehssl_list_get(tehssl_object_t list, int i) {
    if (i < 0) return tehssl_list_get(list, tehssl_list_length(list) + i);
    while (list != NULL && i > 0) {
        i--;
        list = list->next;
    }
    if (list == NULL) return NULL;
    return list->value;
}

void tehssl_list_set(tehssl_object_t list, int i, tehssl_object_t new_value) {
    if (i < 0) {
        tehssl_list_set(list, tehssl_list_length(list) + i, new_value);
        return;
    }
    while (list != NULL && i > 0) {
        i--;
        list = list->next;
    }
    if (list != NULL) list->value = new_value;
}

tehssl_object_t tehssl_dict_get(tehssl_vm_t vm, tehssl_object_t dict, tehssl_object_t key) {
    while (dict != NULL) {
        if (tehssl_equal(vm, dict->key, key)) break;
        dict = dict->next;
    }
    if (dict == NULL) return NULL;
    return dict->value;
}

void tehssl_dict_set(tehssl_vm_t vm, tehssl_object_t dict, tehssl_object_t key, tehssl_object_t new_value) {
    while (true) {
        if (tehssl_equal(vm, dict->key, key)) break;
        if (dict->next == NULL) { // Exhausted all entries
            tehssl_object_t newentry = tehssl_alloc(vm, DICT);
            newentry->key = key;
            dict->next = newentry;
        }
        dict = dict->next;
    }
    dict->value = new_value;
}

// C functions


// Evaluator
tehssl_result_t tehssl_macro_preprocess(tehssl_vm_t vm, tehssl_object_t line, tehssl_object_t scope) {
    // This also reverses the line, so it can be evaluated right-to-left, but read in and stored left-to-right
    tehssl_push(vm, &vm->gc_stack, NULL);
    while (line != NULL) {
        tehssl_object_t item = line->value;
        if (!tehssl_has_name(item) || tehssl_is_literal(item)) {
            #ifdef TEHSSL_DEBUG
            printf("Preprocess -> Got a literal: ");
            debug_print_type(item->type);
            putchar('\n');
            #endif
            tehssl_push(vm, &vm->gc_stack->value, item, LINE);
            line = line->next;
            continue;
        }
        tehssl_object_t macro = tehssl_lookup(scope, item->name, LOOKUP_MACRO);
        if (macro == NULL) {
            #ifdef TEHSSL_DEBUG
            printf("Preprocess -> Not macro: %s\n", item->name);
            #endif
            tehssl_push(vm, &vm->gc_stack->value, item, LINE);
            line = line->next;
            continue;
        }
        if (macro->type == UFUNCTION) {
            // not implemented
            tehssl_pop(&vm->gc_stack);
            return tehssl_error(vm, "todo: user-defined macros");
        } else if (macro->type == CFUNCTION) {
            tehssl_push(vm, &vm->stack, line);
            macro->c_function(vm, scope);
            line = tehssl_pop(&vm->stack);
            tehssl_push(vm, &vm->gc_stack->value, line->value, LINE);
            line = line->next;
        } else {
            // something's wrong
            return tehssl_error(vm, "defined non-function as a macro");
        }
    }
    vm->return_value = tehssl_pop(&vm->gc_stack);
    return OK;
}

// void tehssl_fix_line_links(tehssl_object_t line_head) {
//     // Assuming the ->next fields are all correct, and it is a LINE
//     // fill in the ->prev fields
//     while (line_head != NULL) {
//         if (line_head->next != NULL) line_head->next->prev = line_head;
//         line_head = line_head->next;
//     }
// }

#ifndef yield
#define yield()
#endif

tehssl_result_t tehssl_eval(tehssl_vm_t vm, tehssl_object_t block) {
    EVAL:
    yield();
    tehssl_push(vm, &vm->gc_stack, block);
    if (tehssl_is_literal(block)) {
        #ifdef TEHSSL_DEBUG
        printf("Got a literal: ");
        debug_print_type(block->type);
        putchar('\n');
        #endif
        tehssl_push(vm, &vm->stack, block);
        block = NULL;
    }
    if (block == NULL) {
        #ifdef TEHSSL_DEBUG
        printf("Block is null, returning\n");
        #endif
        tehssl_pop(&vm->gc_stack);
        return OK;
    }
    tehssl_object_t scope = block->scope;
    tehssl_result_t r = tehssl_macro_preprocess(vm, block->code, scope);
    TEHSSL_POP_AND_RETURN_ON_ERROR(r, vm->gc_stack);
    tehssl_object_t ll = vm->return_value;
    // tehssl_fix_line_links(ll);
    while (ll != NULL) {
        // eval the line
        tehssl_object_t item = ll->value;
        if (tehssl_is_literal(item)) {
            tehssl_push(vm, &vm->stack, item);
        } else {
            if (item->type == SYMBOL) { // literals will have been caught by tehssl_is_literal()
                tehssl_object_t var = tehssl_lookup(scope, item->name, LOOKUP_VARIABLE);
                if (var != NULL) {
                    #ifdef TEHSSL_DEBUG
                    printf("Variable: %s\n", item->name);
                    #endif
                    tehssl_push(vm, &vm->stack, var->value);
                } else {
                    tehssl_object_t fun = tehssl_lookup(scope, item->name, LOOKUP_FUNCTION);
                    if (fun != NULL) {
                        #ifdef TEHSSL_DEBUG
                        printf("Nested function: %s\n", item->name);
                        #endif
                        if (fun->type == CFUNCTION) {
                            r = fun->c_function(vm, scope);
                        } else {
                            // reader handles scope links on lambda blocks
                            r = tehssl_eval(vm, fun->block);
                        }
                    } else {
                        #ifdef TEHSSL_DEBUG
                        printf("Undefined word: %s\n", item->name);
                        #endif
                        return tehssl_error(vm, "undefined word", item->name);
                    }
                }
            } else {
                #ifdef TEHSSL_DEBUG
                printf("Invalid eval: ");
                debug_print_type(block->type);
                putchar('\n');
                #endif
                return tehssl_error(vm, "not valid here");
            }
        }
        ll = ll->next;
        TEHSSL_POP_AND_RETURN_ON_ERROR(vm->result_code, vm->gc_stack);
        TEHSSL_POP_AND_RETURN_ON_ERROR(r, vm->gc_stack);
    }
    block = block->next;
    goto EVAL;
}

// Tokenizer
// Returns empty string on eof, NULL on error
char* tehssl_next_token(FILE* file) {
    char* buffer = (char*)malloc(TEHSSL_CHUNK_SIZE);
    size_t buffersz = TEHSSL_CHUNK_SIZE;
    size_t i = 0;
    bool comment = false;
    bool informal = false;
    bool string = false;
    NEXTCHAR:
    char ch = fgetc(file);
    // spaces only matter in strings, but they delimit other things
    if (!string && isspace(ch)) {
        if (i == 0) goto NEXTCHAR;
        else return buffer;
    }
    // toggle stringmode
    if (ch == '"') string = !string;
    // everything is in a string
    if (string) goto BUFPUT;
    if (comment || informal) {
        // exit comment at EOL
        if (comment && ch == '\n') comment = false;
        // exit informal at space
        else if (informal && isspace(ch)) informal = false;
        // dicard comment and informal
        else goto NEXTCHAR;
    }
    if (i == 0 && 'a' <= ch && ch <= 'z') {
        // informal tokens start with lowercase letter
        informal = true;
        goto NEXTCHAR;
    }
    if (ch == EOF) {
        if (!string) return buffer;
        else return NULL;
    }
    // Parens and ; are their own token
    if (strchr("{}[]();", ch) != NULL) {
        // no other char -> return paren as token
        if (i == 0) buffer[0] = ch;
        // other chars -> back up, stop, return that
        else ungetc(ch, file);
        return buffer;
    }
    BUFPUT:
    // allocate more memory as needed
    if (i >= buffersz) {
        buffersz += TEHSSL_CHUNK_SIZE;
        buffer = (char*)realloc(buffer, buffersz);
    }
    buffer[i] = ch;
    i++;
    if (i == 2 && buffer[0] == '~' && ch == '~') {
        // go into comment mode
        comment = true;
        // discard ~~
        free(buffer);
        buffer = (char*)malloc(TEHSSL_CHUNK_SIZE);
    }
    goto NEXTCHAR;
}

// Compiler
tehssl_result_t tehssl_compile_until(tehssl_vm_t vm, tehssl_object_t stream, tehssl_object_t scope, char stop) {
    if (stream == NULL) return OK;
    if (stream->type != STREAM) return tehssl_error(vm, "can't compile from a non-stream");
    tehssl_push(vm, &vm->gc_stack, stream);
    char* buffer = (char*)malloc(TEHSSL_CHUNK_SIZE);
    size_t buffersz = TEHSSL_CHUNK_SIZE;
    char ch;
    tehssl_object_t out_block = NULL;
    tehssl_object_t out_block_tail = NULL;
    tehssl_object_t current_line = NULL;
    tehssl_object_t current_line_tail = NULL;
    NEXTTOKEN:
    bool done = false;
    do {
        ch = fgetc(stream->file);
        if (ch == stop) {
            #ifdef TEHSSL_DEBUG
            printf("Hit end: %c\n", stop);
            #endif
            free(buffer);
            tehssl_pop(&vm->gc_stack);
            vm->return_value = out_block;
            return OK;
        }
        if (ch == EOF) {
            #ifdef TEHSSL_DEBUG
            printf("Unexpected EOF\n");
            #endif
            free(buffer);
            tehssl_pop(&vm->gc_stack);
            return tehssl_error(vm, "unexpected EOF");
        }
    } while (isspace(ch));
    // Get one token
    tehssl_object_t item = NULL;
    bool is_literal_symbol = false;
    if (ch == '{') {
        #ifdef TEHSSL_DEBUG
        printf("{ ");
        #endif
        tehssl_object_t newscope = tehssl_alloc(vm, SCOPE);
        newscope->parent = scope;
        tehssl_result_t r = tehssl_compile_until(vm, stream, scope, '}');
        TEHSSL_POP_AND_RETURN_ON_ERROR(r, vm->gc_stack);
        #ifdef TEHSSL_DEBUG
        printf("}\n");
        #endif
        item = vm->return_value;
    }
    // Handle ; at end of line
    else if (ch == ';') {
        FINISHED:
        #ifdef TEHSSL_DEBUG
        printf("; ");
        #endif
        tehssl_object_t newln = tehssl_alloc(vm, BLOCK);
        newln->code = current_line;
        current_line = NULL;
        current_line_tail = NULL;
        if (out_block) {
            out_block_tail->next = newln;
            out_block_tail = newln;
        }
        else {
            out_block = newln;
            out_block_tail = newln;
        }
        if (done) {
            vm->return_value = out_block;
            tehssl_pop(&vm->gc_stack);
            return OK;
        }
    }
    // TODO:  [  and  (  syntactic sugar for List and Point
    // Handle extraneous close braces
    else if (ch == '}') {
        #ifdef TEHSSL_DEBUG
        printf("Unexpected }\n");
        #endif
        free(buffer);
        tehssl_pop(&vm->gc_stack);
        return tehssl_error(vm, "unexpected '}'");
    }
    else if (ch == ']') {
        #ifdef TEHSSL_DEBUG
        printf("Unexpected ]\n");
        #endif
        free(buffer);
        tehssl_pop(&vm->gc_stack);
        return tehssl_error(vm, "unexpected ']'");
    }
    else if (ch == ')') {
        #ifdef TEHSSL_DEBUG
        printf("Unexpected )\n");
        #endif
        free(buffer);
        tehssl_pop(&vm->gc_stack);
        return tehssl_error(vm, "unexpected ')'");
    }
    // Informal syntax: lowercase word
    else if ('a' <= ch && ch <= 'z') {
        #ifdef TEHSSL_DEBUG
        printf("Informal word\n");
        #endif
        do {
            ch = fgetc(stream->file);
            #ifdef TEHSSL_DEBUG
            printf("ichar: %c\n", ch);
            #endif
            if (ch == stop) {
                #ifdef TEHSSL_DEBUG
                printf("Hit end: %d\n", stop);
                #endif
                free(buffer);
                vm->return_value = out_block;
                tehssl_pop(&vm->gc_stack);
                return OK;
            }
            if (ch == EOF) {
                #ifdef TEHSSL_DEBUG
                printf("Unexpected EOF\n");
                #endif
                free(buffer);
                tehssl_pop(&vm->gc_stack);
                return tehssl_error(vm, "unexpected EOF");
            }
        } while (isalpha(ch));
        goto NEXTTOKEN;
    }
    // Symbol and normal word
    else {
        size_t i = 0;
        size_t tildes = 0;
        if (ch == '\\') is_literal_symbol = true;
        else {
            i = 1;
            buffer[0] = ch;
        }
        if (ch == '~') tildes++;
        // fill buffer, expand as needed
        while (true) {
            if (i == buffersz) {
                buffersz += TEHSSL_CHUNK_SIZE;
                buffer = (char*)realloc(buffer, buffersz);
            }
            ch = fgetc(stream->file);
            if (ch == '~') tildes++;
            if (tildes == 2) {
                // Lop comment until end of line
                do {
                    ch = fgetc(stream->file);
                    if (ch == EOF) {
                        #ifdef TEHSSL_DEBUG
                        printf("Hit eof\n");
                        #endif
                        done = true;
                        goto BUFFERFULL;
                    }
                    if (ch == EOF) {
                        #ifdef TEHSSL_DEBUG
                        printf("Unexpected EOF\n");
                        #endif
                        free(buffer);
                        tehssl_pop(&vm->gc_stack);
                        return tehssl_error(vm, "unexpected EOF");
                    }
                } while (strchr("\r\n", ch) == NULL);
                free(buffer);
                buffer = (char*)malloc(TEHSSL_CHUNK_SIZE);
                goto NEXTTOKEN;
            }
            if (ch == stop) {
                #ifdef TEHSSL_DEBUG
                printf("Hit end: %c\n", stop);
                #endif
                done = true;
                goto BUFFERFULL;
            }
            if (ch == EOF) {
                #ifdef TEHSSL_DEBUG
                printf("Unexpected EOF\n");
                #endif
                free(buffer);
                tehssl_pop(&vm->gc_stack);
                return tehssl_error(vm, "unexpected EOF");
            }
            if (isspace(ch)) {
                buffer[i] = 0;
                break;
            }
            buffer[i] = ch;
            i++;
        }
    }
    BUFFERFULL:
    // Try number
    double n;
    int good = sscanf(buffer, "%lg", &n);
    if (good == 1) {
        #ifdef TEHSSL_DEBUG
        printf("Number: %lg\n", n);
        #endif
        item = tehssl_alloc(vm, NUMBER);
        item->number = n;
    }
    // It's a symbol
    else {
        #ifdef TEHSSL_DEBUG
        printf("Got symbol: %s\n", buffer);
        #endif
        if (is_literal_symbol) item = tehssl_make_symbol(vm, (const char*)buffer, SYMBOL_LITERAL);
        else item = tehssl_make_symbol(vm, (const char*)buffer, SYMBOL_WORD);
    }
    buffer = (char*)malloc(TEHSSL_CHUNK_SIZE);
    tehssl_object_t newb = tehssl_alloc(vm, LINE);
    newb->item = item;
    if (current_line) {
        current_line_tail->next = newb;
        current_line_tail = newb;
    }
    else {
        current_line = newb;
        current_line_tail = newb;
    }
    if (done) {
        free(buffer);
        goto FINISHED;
    }
    else goto NEXTTOKEN;
}

tehssl_result_t tehssl_run_string(tehssl_vm_t vm, const char* string) {
    tehssl_object_t ss = tehssl_make_stream(vm, "stringstream", fmemopen((void*)string, strlen(string), "r"));
    tehssl_result_t r = tehssl_compile_until(vm, ss, vm->global_scope, EOF);
    TEHSSL_RETURN_ON_ERROR(r);
    tehssl_object_t rv = vm->return_value;
    #ifdef TEHSSL_DEBUG
    if (rv == NULL) {
        printf("compile returned NULL!!\n");
        return ERROR;
    }
    printf("Finished compile, running a ");
    debug_print_type(rv->type);
    putchar('\n');
    #endif
    return tehssl_eval(vm, rv);
}


// Register C functions
#define IS_MACRO true
#define NOT_MACRO false
void tehssl_register_word(tehssl_vm_t vm, const char* name, tehssl_cfun_t fun, bool is_macro) {
    if (vm->global_scope == NULL) {
        vm->global_scope = tehssl_alloc(vm, SCOPE);
    }
    tehssl_object_t fobj = tehssl_alloc(vm, CFUNCTION);
    fobj->name = mystrdup(name);
    fobj->c_function = fun;
    fobj->next = vm->global_scope->functions;
    if (is_macro) tehssl_set_flag(fobj, MACRO_FUNCTION);
    vm->global_scope->functions = fobj;
}

// Register C types
void tehssl_register_type(tehssl_vm_t vm, const char* name, tehssl_typefun_t fun) {
    tehssl_object_t t = tehssl_alloc(vm, TFUNCTION);
    t->name = mystrdup(name);
    t->type_function = fun;
    t->next = vm->type_functions;
    vm->type_functions = t;
}

void tehssl_init_builtins(tehssl_vm_t vm) {
    // TODO
}

#ifdef TEHSSL_DEBUG
// Test code
// for pasting into https://cpp.sh/
tehssl_result_t myfunction(tehssl_vm_t vm, tehssl_object_t scope) { printf("myfunction called!\n"); return OK; }
int main() {
    // test 1
    printf("\n\n-----test 1: garbage collector----\n\n");
    tehssl_vm_t vm = tehssl_new_vm();
    // Make some garbage
    for (int i = 0; i < 5; i++) {
        tehssl_make_number(vm, 123);
        tehssl_make_number(vm, 456.789123);
        tehssl_make_string(vm, "i am cow hear me moo");
        tehssl_make_symbol(vm, "Symbol!", SYMBOL_WORD);
        // This is not garbage, it is on the stack now
        tehssl_push(vm, &vm->stack, tehssl_make_number(vm, 1.7E+123));
        tehssl_push(vm, &vm->stack, tehssl_make_string(vm, "Foo123"));
    }
    tehssl_register_word(vm, "MyFunction", myfunction, NOT_MACRO);
    printf("%lu objects\n", vm->num_objects);
    tehssl_gc(vm);
    printf("%lu objects after gc\n", vm->num_objects);

    // test 2
    printf("\n\n-----test 2: tokenizer----\n\n");
    const char* str = "Hello world! {}(){}()}{)}aa}Hell\"O;Hell O\"!;";
    FILE* s = fmemopen((void*)str, strlen(str), "r");
    char* token = NULL;
    do {
        token = tehssl_next_token(s);
        printf("token-> %s\n", token);
    } while (token != NULL);

    // test 3
    printf("\n\n-----test 3: parser----\n\n");
    tehssl_result_t r = tehssl_run_string(vm, "Hello world; 123; 456; { { { {MyFunction} i am a cow hear me moo Yikes Yikes Yikes 0x667 }}}");
    printf("Returned %d: ", r);
    debug_print_type(vm->return_value->type);
    putchar('\n');
    tehssl_destroy(vm);
}
#endif
