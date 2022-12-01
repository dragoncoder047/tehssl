#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cstdint>
#include <cctype>

// Config options
#ifndef TEHSSL_MIN_HEAP_SIZE
#define TEHSSL_MIN_HEAP_SIZE 64
#endif

#ifndef TEHSSL_CHUNK_SIZE
#define TEHSSL_CHUNK_SIZE 128
#endif

// Datatypes
enum tehssl_status {
    OK,
    ERROR,
    RETURN,
    BREAK,
    CONTINUE,
    OUT_OF_MEMORY
};

enum tehssl_flag {
    GC_MARK_TEMP,
    GC_MARK_PERM,
    PR_MARK,
    VARIABLE,
    MACRO_FUNCTION
};

enum tehssl_symbol_type {
    NORMAL,
    LITERAL,
    KEYWORD_ADD,
    KEYWORD_LOOK,
    KEYWORD_POP,
    KEYWORD_FLAG
};

enum tehssl_function_type {
    NORMAL,
    BUILTIN,
    MACRO,
    BUILTIN_MACRO,
    TYPE_FUNCTION
};

// Different types
enum tehssl_typeid {
//  Type      Cell--> A            B
    CONS,        //   (value)      (next)
    LINE,        //   (item)       (next)
    BLOCK,       //   (code)       (next)
    CLOSURE,     //   (scope)      (block)
    FLOAT,       //   double
    INT,         //   int64_t
    SINGLETON,   //   int
    SYMBOL,      //   char*        flags
    STRING,      //   char*
    STREAM,      //   char*        FILE*
    // Special internal types
    SCOPE,       //   (bindings)   (parent)
    NAME,        //   char*        (value)
    FUNCTION     //   (value)      flags
    // USERTYPE
};
// N.B. the char* pointers are "owned" by the object and MUST be strcpy()'d if the object is duplicated.

enum tehssl_singleton {
    TRUE,
    FALSE,
    UNDEFINED,
    DNE
};

// Typedefs
typedef enum tehssl_typeid tehssl_typeid_t;
typedef enum tehssl_flag tehssl_flag_t;
typedef enum tehssl_status tehssl_status_t;
typedef enum tehssl_singleton tehssl_singleton_t;
typedef enum tehssl_symbol_type tehssl_symbol_type_t;
typedef enum tehssl_function_type tehssl_function_type_t;
typedef struct tehssl_object *tehssl_object_t;
typedef struct tehssl_vm *tehssl_vm_t;
typedef void (*tehssl_fun_t)(tehssl_vm_t, tehssl_object_t);
typedef uint16_t tehssl_flags_t;

// Main OBJECT type
struct tehssl_object {
    tehssl_typeid_t type;
    tehssl_flags_t flags;
    tehssl_object_t next_object;
    union {
        double float_number;
        int64_t int_number;
        tehssl_singleton_t singleton;
        struct {
            union {
                tehssl_object_t car;
                tehssl_object_t value;
                tehssl_object_t scope;
                char* chars;
            };
            union {
                tehssl_object_t cdr;
                tehssl_object_t next;
                tehssl_object_t parent;
                tehssl_object_t code;
                FILE* file;
                tehssl_fun_t c_function;
                tehssl_symbol_type_t symboltype;
                tehssl_function_type_t functiontype;
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
    tehssl_status_t status;
    tehssl_object_t first_object;
    tehssl_object_t type_functions;
    size_t num_objects;
    size_t next_gc;
    bool enable_gc;
};

// Forward references
size_t tehssl_gc(tehssl_vm_t);

#ifdef TEHSSL_DEBUG
void debug_print_type(tehssl_typeid_t t) {
    switch (t) {
        case CONS: printf("CONS"); break;
        case LINE: printf("LINE"); break;
        case BLOCK: printf("BLOCK"); break;
        case CLOSURE: printf("CLOSURE"); break;
        case FLOAT: printf("FLOAT"); break;
        case INT: printf("INT"); break;
        case SINGLETON: printf("SINGLETON"); break;
        case SYMBOL: printf("SYMBOL"); break;
        case STRING: printf("STRING"); break;
        case STREAM: printf("STREAM"); break;
        case SCOPE: printf("SCOPE"); break;
        case NAME: printf("NAME"); break;
        case FUNCTION: printf("FUNCTION"); break;
    }
}
#endif

inline uint8_t tehssl_get_cell_info(tehssl_object_t obj) {
    if (obj == NULL) return 0;
    switch (obj->type) {
        case CONS:
        case LINE:
        case BLOCK: 
        case CLOSURE: return 0b011;
        case FLOAT:
        case INT: 
        case SINGLETON: return 0b000;
        case SYMBOL:
        case STRING:
        case STREAM: return 0b100;
        case SCOPE: return 0b011;
        case NAME: return 0b101;
        case FUNCTION: return (obj->functiontype == NORMAL || obj->functiontype == MACRO) ? 0b010 : 0b000;
    }
}

// Flags test
#define tehssl_set_flag(x, f) ((x)->flags |= (1 << (f)))
#define tehssl_clear_flag(x, f) ((x)->flags &= ~(1 << (f)))
#define tehssl_test_flag(x, f) ((x)->flags & (1 << (f)))

// Alloc
tehssl_vm_t tehssl_new_vm() {
    tehssl_vm_t vm = (tehssl_vm_t)malloc(sizeof(struct tehssl_vm));
    vm->stack = NULL;
    vm->return_value = NULL;
    vm->global_scope = NULL;
    vm->gc_stack = NULL;
    vm->type_functions = NULL;
    vm->first_object = NULL;
    vm->status = OK;
    vm->num_objects = 0;
    vm->next_gc = TEHSSL_MIN_HEAP_SIZE;
    vm->enable_gc = true;
    return vm;
}

tehssl_object_t tehssl_alloc(tehssl_vm_t vm, tehssl_typeid_t type) {
    if (vm->num_objects >= vm->next_gc && vm->enable_gc) tehssl_gc(vm);
    tehssl_object_t object = (tehssl_object_t)malloc(sizeof(struct tehssl_object));
    if (!object) {
        vm->status = OUT_OF_MEMORY;
        return NULL;
    }
    memset(object, 0, sizeof(struct tehssl_object));
    object->type = type;
    object->next_object = vm->first_object;
    vm->first_object = object;
    vm->num_objects++;
    #ifdef TEHSSL_DEBUG
    printf("Allocating a ");
    debug_print_type(type);
    printf(": Now have %zu objects\n", vm->num_objects);
    #endif
    return object;
}

// Garbage collection
void tehssl_markobject(tehssl_vm_t vm, tehssl_object_t object, tehssl_flag_t flag = GC_MARK_TEMP) {
    MARK:
    // already marked? abort
    if (object == NULL) {
        #ifdef TEHSSL_DEBUG
        printf("Marking NULL\n");
        #endif
        return;
    }
    #ifdef TEHSSL_DEBUG
    printf("Marking a "); debug_print_type(object->type); putchar('\n');
    #endif
    if (tehssl_test_flag(object, flag)) {
        #ifdef TEHSSL_DEBUG
        printf("Already marked %i, returning\n", flag);
        #endif
        return;
    }
    tehssl_set_flag(object, flag);
    uint8_t usage = tehssl_get_cell_info(object);
    if (usage & 0b010) tehssl_markobject(vm, object->car, flag);
    if (usage & 0b001) {
        object = object->cdr;
        goto MARK;
    }
}

void tehssl_markall(tehssl_vm_t vm) {
    tehssl_markobject(vm, vm->stack);
    tehssl_markobject(vm, vm->return_value);
    tehssl_markobject(vm, vm->global_scope);
    #ifdef TEHSSL_DEBUG
    printf("Marking GC_STACK\n");
    #endif
    tehssl_markobject(vm, vm->gc_stack);
    #ifdef TEHSSL_DEBUG
    printf("Done marking GC_STACK\n");
    #endif
    tehssl_markobject(vm, vm->type_functions);
}

void tehssl_sweep(tehssl_vm_t vm) {
    tehssl_object_t* object = &vm->first_object;
    while (*object != NULL) {
        if (!tehssl_test_flag(*object, GC_MARK_TEMP) && !tehssl_test_flag(*object, GC_MARK_PERM)) {
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
                unreached->file = NULL;
            }
            if (tehssl_get_cell_info(unreached) & 0b100) {
                #ifdef TEHSSL_DEBUG
                printf(" name-> \"%s\"", unreached->chars);
                #endif
                free(unreached->chars);
                unreached->chars = NULL;
            }
            #ifdef TEHSSL_DEBUG
            if (unreached->type == FLOAT) printf(" number-> %g", unreached->float_number);
            if (unreached->type == SINGLETON) printf(" singleton-> %i", unreached->singleton);
            if (*object == NULL) printf(", No next");
            else { printf(", Next a "); debug_print_type((*object)->type); }
            printf(": Now have %u objects\n", vm->num_objects - 1);
            #endif
            free(unreached);
            vm->num_objects--;
        } else {
            #ifdef TEHSSL_DEBUG
            printf("Skipping marked "); debug_print_type((*object)->type); putchar('\n');
            #endif
            tehssl_clear_flag(*object, GC_MARK_TEMP);
            object = &(*object)->next_object;
        }
    }
}

size_t tehssl_gc(tehssl_vm_t vm) {
    if (!vm->enable_gc) {
        #ifdef TEHSSL_DEBUG
        printf("GC disabled, aborting GC\n");
        #endif
        return 0;
    }
    #ifdef TEHSSL_DEBUG
    printf("Entering GC\n");
    #endif
    size_t n = vm->num_objects;
    tehssl_markall(vm);
    tehssl_sweep(vm);
    vm->next_gc = vm->num_objects == 0 ? TEHSSL_MIN_HEAP_SIZE : vm->num_objects * 2;
    size_t freed = n - vm->num_objects;
    #ifdef TEHSSL_DEBUG
    printf("GC done, freed %zu objects\n", freed);
    #endif
    return freed;
}

void tehssl_destroy(tehssl_vm_t vm) {
    while (vm->first_object != NULL) {
        tehssl_object_t o = vm->first_object;
        vm->first_object = o->next_object;
        free(o);
    }
    free(vm);
}

// Push / Pop (for stacks)
#define tehssl_push_t(vm, stack, item, t) do { tehssl_object_t cell = tehssl_alloc((vm), (t)); cell->value = item; cell->next = (stack); (stack) = cell; } while (false)
#define tehssl_push(vm, stack, item) tehssl_push_t(vm, stack, item, CONS)
#define tehssl_pop(stack) do { if ((stack) != NULL) (stack) = (stack)->next; } while (false)

// Make objects
tehssl_object_t tehssl_make_string(tehssl_vm_t vm, char* string) {
    tehssl_object_t object = vm->first_object;
    while (object != NULL) {
        if (object->type == STRING && strcmp(object->chars, string) == 0) return object;
        object = object->next_object;
    }
    tehssl_object_t sobj = tehssl_alloc(vm, STRING);
    sobj->chars = strdup(string);
    return sobj;
}

#define SYMBOL_LITERAL true
#define SYMBOL_WORD false
tehssl_object_t tehssl_make_symbol(tehssl_vm_t vm, char* name, tehssl_symbol_type_t type) {
    tehssl_object_t object = vm->first_object;
    while (object != NULL) {
        if (object->type == SYMBOL && strcmp(object->chars, name) == 0 && object->symboltype == type) return object;
        object = object->next_object;
    }
    tehssl_object_t sobj = tehssl_alloc(vm, SYMBOL);
    sobj->chars = strdup(name);
    sobj->symboltype = type;
    return sobj;
}

tehssl_object_t tehssl_make_float(tehssl_vm_t vm, double n) {
    tehssl_object_t object = vm->first_object;
    while (object != NULL) {
        if (object->type == FLOAT && (uint64_t)n == (uint64_t)object->float_number) return object;
        object = object->next_object;
    }
    tehssl_object_t sobj = tehssl_alloc(vm, FLOAT);
    sobj->float_number = n;
    return sobj;
}

tehssl_object_t tehssl_make_singleton(tehssl_vm_t vm, tehssl_singleton_t s) {
    tehssl_object_t object = vm->first_object;
    while (object != NULL) {
        if (object->type == SINGLETON && object->singleton == s) return object;
        object = object->next_object;
    }
    tehssl_object_t sobj = tehssl_alloc(vm, SINGLETON);
    sobj->singleton = s;
    return sobj;
}

tehssl_object_t tehssl_make_stream(tehssl_vm_t vm, char* name, FILE* file) {
    tehssl_object_t sobj = tehssl_alloc(vm, STREAM);
    sobj->chars = strdup(name);
    sobj->file = file;
    return sobj;
}

// Lookup values in scope
#define FUN 0
#define VAR 1
#define MACRO 2
tehssl_object_t tehssl_lookup(tehssl_object_t scope, char* name, uint8_t what) {
    LOOKUP:
    if (scope == NULL || scope->type != SCOPE) return NULL;
    tehssl_object_t name = scope->value;
    while (name != NULL) {
        if (strcmp(name->chars, name) == 0) {
            if ((what == FUN || what == MACRO) && (name->value == NULL || name->value->type != FUNCTION)) goto NEXT;
            if (what == FUN && tehssl_test_flag(name, MACRO_FUNCTION)) goto NEXT;
            if (what == MACRO && !tehssl_test_flag(name, MACRO_FUNCTION)) goto NEXT;
            if (what == VAR && !tehssl_test_flag(name, VARIABLE)) goto NEXT;
            return name->value;
        }
        NEXT:
        name = name->next;
    }
    scope = scope->parent;
    goto LOOKUP;
}

// Helper functions
void tehssl_error(tehssl_vm_t vm, const char* message) {
    vm->return_value = tehssl_make_string(vm, (char*)message);
    vm->status = ERROR;
}

void tehssl_error(tehssl_vm_t vm, const char* message, char* detail) {
    char* buf;
    asprintf(&buf, "%s: %s", message, detail);
    vm->return_value = tehssl_alloc(vm, STRING);
    vm->return_value->chars = buf;
    vm->status = ERROR;
}

#define IFERR(vm) if ((vm)->status == ERROR || (vm)->status == OUT_OF_MEMORY)
#define RIE(vm) do { IFERR(vm) return; } while (false)
#define RNIE(vm) do { IFERR(vm) return NULL; } while (false)
#define PRIE(vm, stack) do { IFERR(vm) { tehssl_pop(&(stack)); return; } } while (false)
#define ERR(vm, msg) do { tehssl_error(vm, (msg)); return; } while (false) 
#define ERR2(vm, msg, detail) do { tehssl_error(vm, (msg), (detail)); return; } while (false)

bool tehssl_equal(tehssl_object_t a, tehssl_object_t b) {
    CMP:
    if (a == b) return true; // Same object
    if (a == NULL || b == NULL) return false; // Null
    if (a->type != b->type) return false; // Different type
    uint8_t info = tehssl_get_cell_info(a);
    if (info & 0b100 && strcmp(a->chars, b->chars) != 0) return false;
    if (info & 0b010 && !tehssl_equal(a->car, b->car)) return false;
    if (info & 0b001) {
        a = a->cdr;
        b = b->cdr;
        goto CMP;
    }
    return true;
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

// C functions

// Tokenizer
#define TEHSSL_SPECIAL_CHARS "{}[]();"
// Returns empty string on eof, NULL on error
char* tehssl_next_token(FILE* file) {
    char* buffer = (char*)malloc(TEHSSL_CHUNK_SIZE);
    memset(buffer, 0, TEHSSL_CHUNK_SIZE);
    size_t buffersz = TEHSSL_CHUNK_SIZE;
    size_t i = 0;
    bool comment = false;
    bool string = false;
    bool informal = false;
    NEXTCHAR:
    char ch = fgetc(file);
    // if (ch != EOF) printf("\ni=%d CH=%c: ", i, ch); else printf("ch=EOF, ");
    if (comment || informal) {
        // printf("c, ");
        // exit comment at EOL
        if (comment && (ch == '\n' || ch == EOF)) comment = false;
        else if (informal && (isspace(ch) || ch == EOF)) informal = false;
        if (comment || !strchr(TEHSSL_SPECIAL_CHARS, ch)) goto NEXTCHAR;
    }
    // spaces only matter in strings, but they delimit other things
    else if (!string && isspace(ch)) {
        // printf("space outside of string, i=%ld\n", i);
        informal = false;
        if (i == 0) goto NEXTCHAR;
        else goto DONE;
    }
    // toggle stringmode
    if (ch == '"') {
        string = !string;
        if (i > 0) {
            // printf("i>0, ");
            if (string) ungetc(ch, file);
            goto DONE;
        }
    }
    if (ch == EOF) {
        if (!string) goto DONE;
        else goto ERROR;
    }
    // Parens and ; are their own token
    // unless in a string
    if (!string && strchr(TEHSSL_SPECIAL_CHARS, ch)) {
        // no other char -> return paren as token
        if (i == 0) buffer[0] = ch;
        // other chars -> back up, stop, return what we've got so far
        else ungetc(ch, file);
        goto DONE;
    }
    if (i == 0 && 'a' <= ch && ch <= 'z') {
        // informal tokens start with lowercase letter
        informal = true;
        goto NEXTCHAR;
    }
    // printf("->buf, ");
    // allocate more memory as needed
    if (i >= buffersz) {
        buffersz += TEHSSL_CHUNK_SIZE;
        buffer = (char*)realloc(buffer, buffersz);
    }
    buffer[i] = ch;
    i++;
    if (i == 2 && buffer[0] == '~' && ch == '~') {
        // printf("got ~~ for comment, ");
        comment = true;
        // discard ~~
        memset(buffer, 0, buffersz);
        buffer = (char*)realloc(buffer, TEHSSL_CHUNK_SIZE);
        buffersz = TEHSSL_CHUNK_SIZE;
        i = 0;
    }
    goto NEXTCHAR;
    DONE:
    return buffer;
    ERROR:
    free(buffer);
    return NULL;
}

// Compiler
tehssl_object_t tehssl_compile_until(tehssl_vm_t vm, FILE* stream, char stop) {
    tehssl_gc(vm);
    bool oldenable = vm->enable_gc;
    vm->enable_gc = false;
    tehssl_object_t c_block = tehssl_alloc(vm, BLOCK);
    tehssl_object_t* block_tail = &c_block;
    IFERR(vm) goto ERROR;
    // Outer loop: Lines
    while (!feof(stream)) {
        #ifdef TEHSSL_DEBUG
        printf("Top of Outer loop\n");
        #endif
        tehssl_object_t c_line = tehssl_alloc(vm, LINE);
        tehssl_object_t* line_tail = &c_line;
        IFERR(vm) goto ERROR;
        char* token = NULL;
        // Inner loop: items on the line
        while (!feof(stream)) {
            #ifdef TEHSSL_DEBUG
            printf("Top of Inner loop\n");
            #endif
            token = tehssl_next_token(stream);
            if (token == NULL) {
                #ifdef TEHSSL_DEBUG
                printf("Unexpected EOF\n");
                #endif
                tehssl_error(vm, "unexpected EOF");
                vm->enable_gc = oldenable;
                return NULL;
            }
            if ((strlen(token) == 0 && stop == EOF) || token[0] == stop) {
                #ifdef TEHSSL_DEBUG
                printf("Hit Stop, returning\n");
                #endif
                free(token);
                vm->enable_gc = oldenable;
                return c_block;
            }
            tehssl_object_t item = NULL;
            if (token[0] == ';') {
                #ifdef TEHSSL_DEBUG
                printf("Semicolon\n");
                #endif
                free(token);
                break;
            }
            else if (token[0] == '{') {
                free(token);
                #ifdef TEHSSL_DEBUG
                printf("Bracket\n");
                #endif
                item = tehssl_compile_until(vm, stream, '}');
            }
            else {
                // literal
                double num;
                if (sscanf(token, "%lf", &num) == 1) {
                    #ifdef TEHSSL_DEBUG
                    printf("Number: %g\n", num);
                    #endif
                    item = tehssl_make_float(vm, num);
                } else if (strcmp(token, "True") == 0) {
                    #ifdef TEHSSL_DEBUG
                    printf("TRUE literal\n");
                    #endif
                    item = tehssl_make_singleton(vm, TRUE);
                } else if (strcmp(token, "False") == 0) {
                    #ifdef TEHSSL_DEBUG
                    printf("FALSE literal\n");
                    #endif
                    item = tehssl_make_singleton(vm, FALSE);
                } else if (strcmp(token, "Undefined") == 0) {
                    #ifdef TEHSSL_DEBUG
                    printf("UNDEFINED literal\n");
                    #endif
                    item = tehssl_make_singleton(vm, UNDEFINED);
                } else if (strcmp(token, "DNE") == 0) {
                    #ifdef TEHSSL_DEBUG
                    printf("DNE literal\n");
                    #endif
                    item = tehssl_make_singleton(vm, DNE);
                } else if (strcmp(token, "Null") == 0) {
                    #ifdef TEHSSL_DEBUG
                    printf("Null literal\n");
                    #endif
                    // item is already NULL
                } else if (token[0] == '"') {
                    #ifdef TEHSSL_DEBUG
                    printf("String: %s\n", token + 1);
                    #endif
                    item = tehssl_make_string(vm, token + 1);
                } else if (token[0]  == ':') {
                    #ifdef TEHSSL_DEBUG
                    printf("Literal symbol: %s\n", token + 1);
                    #endif
                    item = tehssl_make_symbol(vm, token + 1, SYMBOL_LITERAL);
                } else {
                    #ifdef TEHSSL_DEBUG
                    printf("Normal symbol: %s\n", token);
                    #endif
                    // TODO implement macros
                    item = tehssl_make_symbol(vm, token, SYMBOL_WORD);
                }
                free(token);
                *line_tail = tehssl_alloc(vm, LINE);
                IFERR(vm) goto ERROR;
                (*line_tail)->value = item;
                line_tail = &(*line_tail)->next;
            }
            #ifdef TEHSSL_DEBUG
            printf("Bottom of outer loop\n");
            #endif
            *block_tail = tehssl_alloc(vm, BLOCK);
            (*block_tail)->value = c_line;
            block_tail = &(*block_tail)->next;
        }
    }
    #ifdef TEHSSL_DEBUG
    printf("feof() false, returning\n");
    #endif
    ERROR:
    vm->enable_gc = oldenable;
    RNIE(vm);
    return c_block;
}

// Reverse a line
tehssl_object_t tehssl_reverse_line(tehssl_vm_t vm, tehssl_object_t line) {
    #ifdef TEHSSL_DEBUG
    printf("Reversing a line\n");
    #endif
    tehssl_object_t reversed = NULL;
    while (line != NULL) {
        tehssl_push(vm, reversed, line->value);
        RNIE(vm);
        line = line->next;
    }
    #ifdef TEHSSL_DEBUG
    printf("Done reversing a line\n");
    #endif
    return reversed;
}

#ifndef yield
#define yield()
#endif

// Evaluator
void tehssl_eval(tehssl_vm_t vm, tehssl_object_t block, tehssl_object_t scope) {
    #ifdef TEHSSL_DEBUG
    printf("Entering evaluator\n");
    #endif
    tehssl_markobject(vm, block);
    tehssl_markobject(vm, scope);
    tehssl_gc(vm);
    tehssl_push(vm, vm->gc_stack, block);
    tehssl_push(vm, vm->gc_stack, scope);
    while (block != NULL) {
        tehssl_object_t rl = tehssl_reverse_line(vm, block->item);
        tehssl_push(vm, vm->gc_stack, rl);
        RIE(vm);
        while (rl != NULL) {
            yield();
            tehssl_object_t item = rl->value;
            if (tehssl_is_literal(item)) {
                #ifdef TEHSSL_DEBUG
                printf("Pushing a "); if (item != NULL) debug_print_type(item->type); else printf("NULL"); putchar('\n');
                #endif
                tehssl_push(vm, vm->stack, item);
            } else {
                if (item->type == BLOCK) {
                    tehssl_object_t closure = tehssl_alloc(vm, CLOSURE);
                    closure->block = item;
                    closure->scope = scope;
                    tehssl_push(vm, vm->stack, closure);
                } else if (item->type == SYMBOL) {
                    tehssl_object_t fun = tehssl_lookup(scope, item->chars, FUN);
                    tehssl_object_t var = tehssl_lookup(scope, item->chars, VAR);
                    if (var != NULL) {
                        tehssl_push(vm, vm->stack, var->value);
                    } else if (fun != NULL) {
                        if (fun->type == CNFUNCTION) {
                            fun->c_function(vm, scope);
                        } else {
                            tehssl_object_t new_scope = tehssl_alloc(vm, SCOPE);
                            new_scope->parent = vm->global_scope;
                            tehssl_eval(vm, fun->block, new_scope);
                        }
                    } else {
                        tehssl_error(vm, "undefined", item->chars);
                        tehssl_pop(vm->gc_stack);
                        goto DONE;
                    }
                }
            }
            rl = rl->next;
        }
        #ifdef TEHSSL_DEBUG
        printf("Done with current line\n");
        #endif
        tehssl_pop(vm->gc_stack);
    }
    DONE:
    #ifdef TEHSSL_DEBUG
    printf("Leaving evaluator");
    IFERR(vm) printf(" in error state");
    putchar('\n');
    #endif
    tehssl_pop(vm->gc_stack);
    tehssl_pop(vm->gc_stack);
}

void tehssl_run_string(tehssl_vm_t vm, const char* string) {
    FILE* ss = fmemopen((void*)string, strlen(string), "r");
    tehssl_object_t rv = tehssl_compile_until(vm, ss, EOF);
    RIE(vm);
    #ifdef TEHSSL_DEBUG
    if (rv == NULL) {
        printf("compile returned NULL!!\n");
        return;
    }
    printf("Finished compile, running a ");
    debug_print_type(rv->type);
    putchar('\n');
    #endif
    fclose(ss);
    tehssl_eval(vm, rv, vm->global_scope);
}


// Register C functions
#define IS_MACRO true
#define NOT_MACRO false
void tehssl_register_word(tehssl_vm_t vm, const char* name, tehssl_fun_t fun) {
    if (vm->global_scope == NULL) {
        vm->global_scope = tehssl_alloc(vm, SCOPE);
    }
    tehssl_object_t fobj = tehssl_alloc(vm, CNFUNCTION);
    fobj->chars = strdup(name);
    fobj->c_function = fun;
    fobj->next = vm->global_scope->bindings;
    vm->global_scope->bindings = fobj;
}

// Register C types
void tehssl_register_type(tehssl_vm_t vm, const char* name, tehssl_typefun_t fun) {
    tehssl_object_t t = tehssl_alloc(vm, TFUNCTION);
    t->chars = strdup(name);
    t->type_function = fun;
    t->next = vm->type_functions;
    vm->type_functions = t;
}

void tehssl_init_builtins(tehssl_vm_t vm) {
    // TODO add all builtins
}

#ifdef TEHSSL_TEST
void myfunction(tehssl_vm_t vm, tehssl_object_t scope) { printf("myfunction called!\n"); }
int main(int argc, char* argv[]) {
    const char* str = "~~Hello world!; Foobar\nFor each number in Range 1 to 0x0A -step 3 do { take the Square; Print the Fibbonaci of said square; };\n~~Literals\nPrints {\"DONE!!\" 123 123.456E789 Infinity NaN Undefined DNE False True}";
    tehssl_vm_t vm = tehssl_new_vm();

    printf("\n\n-----test 1: garbage collector----\n\n");
    // Make some garbage
    for (int i = 0; i < 5; i++) {
        tehssl_make_float(vm, 123);
        tehssl_make_float(vm, 456.789123);
        tehssl_make_string(vm, "i am cow hear me moo");
        tehssl_make_symbol(vm, "Symbol!", SYMBOL_WORD);
        // This is not garbage, it is on the stack now
        tehssl_push(vm, vm->stack, tehssl_make_float(vm, 1.7E+123));
        tehssl_push(vm, vm->stack, tehssl_make_string(vm, "Foo123"));
    }
    tehssl_register_word(vm, "MyFunction", myfunction);
    printf("%u objects\n", vm->num_objects);
    tehssl_gc(vm);
    printf("%u objects after gc\n", vm->num_objects);

    printf("\n\n-----test 2: tokenizer----\n\n");
    FILE* s = fmemopen((void*)str, strlen(str), "r");
    char* token = NULL;
    while (!feof(s)) {
        token = tehssl_next_token(s);
        if (token == NULL) {
            printf("\n\nTOKENIZER ERROR!!");
            break;
        }
        if (strlen(token) == 0) {
            printf("\n\nEMPTY TOKEN!!");
            free(token);
            break;
        }
        printf("\n%s", token);
        if (token[0] == '"') putchar('"');
        free(token);
    }
    fclose(s);
    putchar('\n');

    printf("\n\n-----test 3: compiler----\n\n");
    printf("making stringstream...\n");
    s = fmemopen((void*)str, strlen(str), "r");
    tehssl_object_t c = tehssl_compile_until(vm, s, EOF);
    printf("Returned %d: ", vm->status);
    if (c == NULL) printf("Compile returned NULL!!");
    else debug_print_type(c->type);
    fclose(s);
    printf("\ncollecting garbage\n");
    tehssl_gc(vm);

    printf("\n\n-----test 4: freeing a fmemopen()'ed stream----\n\n");
    s = fmemopen((void*)str, strlen(str), "r");
    tehssl_make_stream(vm, "stringstream", s);
    tehssl_gc(vm);

    printf("\n\n-----test 5: evaluator----\n\n");
    tehssl_run_string(vm, str);

    printf("\n\n-----tests complete----\n\n");

    tehssl_destroy(vm);
}
#endif
