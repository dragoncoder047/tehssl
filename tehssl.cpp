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
    GC_MARK,
    PRINTER_MARK,
    LITERAL_SYMBOL
};

// Different types
enum tehssl_typeid {
//  Type      Cell--> A            B            C
    LIST,        //                (value)      (next)
    DICT,        //   (key)        (value)      (next)
    LINE,        //                (item)       (next)
    BLOCK,       //                (code)       (next)
    CLOSURE,     //   (scope)      (block)
    NUMBER,      //   double
    SINGLETON,   //   int32_t
    SYMBOL,      //   char*
    STRING,      //   char*
    STREAM,      //   char*        FILE*
    USERTYPE,    //   char*        (ptr1)       (ptr2)
    // Special internal types
    SCOPE,       //   (functions)  (variables)  (parent)
    UNFUNCTION,  //   char*        (block)      (next)
    CNFUNCTION,  //   char*        fun_t        (next)
    CMFUNCTION,  //   char*        macfun_t     (next)
    TFUNCTION,   //   char*        typefun_t    (next)
    VARIABLE     //   char*        (value)      (next)
};
// N.B. the char* pointers are "owned" by the object and MUST be strcpy()'d if the object is duplicated.

enum tehssl_type_action {
    CTYPE_INIT,
    CTYPE_PRINT,
    CTYPE_MARK,
    CTYPE_COMPARE,
    CTYPE_DESTROY
};

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
typedef enum tehssl_type_action tehssl_type_action_t;
typedef enum tehssl_singleton tehssl_singleton_t;
typedef struct tehssl_object *tehssl_object_t;
typedef struct tehssl_vm *tehssl_vm_t;
typedef void (*tehssl_fun_t)(tehssl_vm_t, tehssl_object_t);
typedef void (*tehssl_macfun_t)(tehssl_vm_t, tehssl_object_t*, FILE*);
typedef int (*tehssl_typefun_t)(tehssl_vm_t, tehssl_type_action_t, tehssl_object_t, void*);
typedef uint16_t tehssl_flags_t;

// Main OBJECT type
struct tehssl_object {
    tehssl_typeid_t type;
    tehssl_flags_t flags;
    tehssl_object_t next_object;
    union {
        double number;
        tehssl_singleton_t singleton;
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
                tehssl_fun_t c_function;
                tehssl_typefun_t type_function;
                tehssl_macfun_t macro_function;
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
    tehssl_status_t status;
    tehssl_object_t first_object;
    tehssl_object_t type_functions;
    size_t num_objects;
    size_t next_gc;
    char last_char;
    bool enable_gc;
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
        case CLOSURE: printf("CLOSURE"); break;
        case NUMBER: printf("NUMBER"); break;
        case SINGLETON: printf("SINGLETON"); break;
        case SYMBOL: printf("SYMBOL"); break;
        case STRING: printf("STRING"); break;
        case STREAM: printf("STREAM"); break;
        case USERTYPE: printf("USERTYPE"); break;
        case SCOPE: printf("SCOPE"); break;
        case UNFUNCTION: printf("UNFUNCTION"); break;
        case CNFUNCTION: printf("CNFUNCTION"); break;
        case CMFUNCTION: printf("CMFUNCTION"); break;
        case TFUNCTION: printf("TFUNCTION"); break;
        case VARIABLE: printf("VARIABLE"); break;
    }
}
#endif

inline bool tehssl_has_name(tehssl_object_t object) {
    if (object == NULL) return false;
    switch (object->type) {
        case SYMBOL:
        case STRING:
        case STREAM:
        case USERTYPE:
        case UNFUNCTION:
        case CNFUNCTION:
        case VARIABLE:
            return true;
        default:
            return false;
    }
}

inline bool tehssl_is_literal(tehssl_object_t object) {
    if (object == NULL) return true;
    switch (object->type) {
        case SYMBOL:
            return tehssl_test_flag(object, LITERAL_SYMBOL);
        case LIST:
        case DICT:
        case SINGLETON:
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
    if (object == NULL) {
        #ifdef TEHSSL_DEBUG
        printf("Marking NULL\n");
        #endif
        return;
    }
    #ifdef TEHSSL_DEBUG
    printf("Marking a "); debug_print_type(object->type); putchar('\n');
    #endif
    if (tehssl_test_flag(object, GC_MARK)) {
        #ifdef TEHSSL_DEBUG
        printf("Already marked, returning\n");
        #endif
        return;
    }
    tehssl_set_flag(object, GC_MARK);
    switch (object->type) {
        case DICT:
        case SCOPE:
            tehssl_markobject(vm, object->key);
            // fallthrough
        case BLOCK:
        case LIST:
        case LINE:
            tehssl_markobject(vm, object->value);
            object = object->next;
            goto MARK;
        case NUMBER:
        case SINGLETON:
        case SYMBOL:
        case STRING:
        case STREAM:
            break; // noop
        case CLOSURE:
            tehssl_markobject(vm, object->key);
            tehssl_markobject(vm, object->value);
            break;
        case UNFUNCTION:
        case VARIABLE:
            tehssl_markobject(vm, object->value);
            // fallthrough
        case CNFUNCTION:
        case CMFUNCTION:
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
            if (unreached->type == SINGLETON) printf(" singleton-> %i", unreached->singleton);
            if (*object == NULL) printf(", No next");
            else { printf(", Next a "); debug_print_type((*object)->type); }
            printf(": Now have %lu objects\n", vm->num_objects - 1);
            #endif
            free(unreached);
            vm->num_objects--;
        } else {
            #ifdef TEHSSL_DEBUG
            printf("Skipping marked "); debug_print_type((*object)->type); putchar('\n');
            #endif
            tehssl_clear_flag(*object, GC_MARK);
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
    vm->stack = NULL;
    vm->return_value = NULL;
    vm->global_scope = NULL;
    vm->gc_stack = NULL;
    vm->type_functions = NULL;
    vm->enable_gc = true;
    tehssl_gc(vm);
    free(vm);
}

// Push / Pop (for stacks)
#define tehssl_push(vm, stack, item) do { tehssl_object_t cell = tehssl_alloc((vm), LIST); cell->value = item; cell->next = (stack); (stack) = cell; } while (false)
#define tehssl_pop(stack) do { if ((stack) != NULL) (stack) = (stack)->next; } while (false)

// Make objects
tehssl_object_t tehssl_make_string(tehssl_vm_t vm, char* string) {
    tehssl_object_t object = vm->first_object;
    while (object != NULL) {
        if (object->type == STRING && strcmp(object->name, string) == 0) return object;
        object = object->next_object;
    }
    tehssl_object_t sobj = tehssl_alloc(vm, STRING);
    sobj->name = strdup(string);
    return sobj;
}

#define SYMBOL_LITERAL true
#define SYMBOL_WORD false
tehssl_object_t tehssl_make_symbol(tehssl_vm_t vm, char* name, bool is_literal) {
    tehssl_object_t object = vm->first_object;
    while (object != NULL) {
        if (object->type == SYMBOL && strcmp(object->name, name) == 0 && tehssl_test_flag(object, LITERAL_SYMBOL) == is_literal) return object;
        object = object->next_object;
    }
    tehssl_object_t sobj = tehssl_alloc(vm, SYMBOL);
    sobj->name = strdup(name);
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
    sobj->name = strdup(name);
    sobj->file = file;
    return sobj;
}

tehssl_object_t tehssl_make_custom(tehssl_vm_t vm, char* type, void* data) {
    tehssl_object_t obj = tehssl_alloc(vm, USERTYPE);
    obj->name = strdup(type);
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
#define LOOKUP_VARIABLE 1
tehssl_object_t tehssl_lookup(tehssl_object_t scope, char* name, uint8_t where) {
    LOOKUP:
    if (scope == NULL || scope->type != SCOPE) return NULL;
    tehssl_object_t result = NULL;
    if (where == LOOKUP_VARIABLE) result = scope->variables;
    else result = scope->functions;
    while (result != NULL) {
        if (strcmp(result->name, name) == 0) return result;
        result = result->next;
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
    vm->return_value->name = buf;
    vm->status = ERROR;
}

#define IFERR(vm) if ((vm)->status == ERROR || (vm)->status == OUT_OF_MEMORY)
#define RIE(vm) do { IFERR(vm) return; } while (false)
#define PRIE(vm, stack) do { IFERR(vm) { tehssl_pop(&(stack)); return; } } while (false)
#define ERR(vm, msg) do { tehssl_error(vm, (msg)); return; } while (false) 
#define ERR2(vm, msg, detail) do { tehssl_error(vm, (msg), (detail)); return; } while (false)

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
            if (!tehssl_equal(vm, a->key, b->key)) return false;
            // fallthrough
        case LIST:
        case LINE:
            if (!tehssl_equal(vm, a->value, b->value)) return false;
            a = a->next;
            b = b->next;
            goto CMP;
        case NUMBER:
            return tehssl_compare_numbers(a->number, b->number, false, true, false);
        case SINGLETON:
            return a->singleton == b->singleton;
        case SYMBOL:
        case STRING:
        case STREAM:
            return tehssl_compare_strings(a->name, b->name, false, true, false);
        case USERTYPE: {
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
        default:
            return false;
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
        else {
            free(buffer);
            // unexpected EOF
            return NULL;
        }
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
}

// Compiler
tehssl_object_t tehssl_compile_until(tehssl_vm_t vm, FILE* stream, char stop) {
    tehssl_gc(vm);
    bool oldenable = vm->enable_gc;
    vm->enable_gc = false;
    tehssl_object_t c_block = tehssl_alloc(vm, BLOCK);
    tehssl_object_t* block_tail = &c_block;
    // Outer loop: Lines
    while (!feof(stream)) {
        #ifdef TEHSSL_DEBUG
        printf("Top of Outer loop\n");
        #endif
        tehssl_object_t c_line = tehssl_alloc(vm, LINE);
        tehssl_object_t* line_tail = &c_line;
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
                    item = tehssl_make_number(vm, num);
                } else if (strcmp(token, "NaN") == 0) {
                    #ifdef TEHSSL_DEBUG
                    printf("NAN\n");
                    #endif
                    item = tehssl_make_number(vm, 0.0f / 0.0f);
                } else if (strcmp(token, "True") == 0) {
                    #ifdef TEHSSL_DEBUG
                    printf("TRUE\n");
                    #endif
                    item = tehssl_make_singleton(vm, TRUE);
                } else if (strcmp(token, "False") == 0) {
                    #ifdef TEHSSL_DEBUG
                    printf("FALSE\n");
                    #endif
                    item = tehssl_make_singleton(vm, FALSE);
                } else if (strcmp(token, "Undefined") == 0) {
                    #ifdef TEHSSL_DEBUG
                    printf("UNDEFINED\n");
                    #endif
                    item = tehssl_make_singleton(vm, UNDEFINED);
                } else if (strcmp(token, "DNE") == 0) {
                    #ifdef TEHSSL_DEBUG
                    printf("DNEX\n");
                    #endif
                    item = tehssl_make_singleton(vm, DNE);
                } else if (strcmp(token, "Null") == 0) {
                    #ifdef TEHSSL_DEBUG
                    printf("Nul\n");
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
                    item = tehssl_make_symbol(vm, token, SYMBOL_WORD);
                }
                free(token);
                *line_tail = tehssl_alloc(vm, LINE);
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
    vm->enable_gc = oldenable;
    return c_block;
}

// Evaluator


#ifndef yield
#define yield()
#endif

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
    // tehssl_eval(vm, rv);
}


// Register C functions
#define IS_MACRO true
#define NOT_MACRO false
void tehssl_register_word(tehssl_vm_t vm, const char* name, tehssl_fun_t fun) {
    if (vm->global_scope == NULL) {
        vm->global_scope = tehssl_alloc(vm, SCOPE);
    }
    tehssl_object_t fobj = tehssl_alloc(vm, CNFUNCTION);
    fobj->name = strdup(name);
    fobj->c_function = fun;
    fobj->next = vm->global_scope->functions;
    vm->global_scope->functions = fobj;
}

// Register C types
void tehssl_register_type(tehssl_vm_t vm, const char* name, tehssl_typefun_t fun) {
    tehssl_object_t t = tehssl_alloc(vm, TFUNCTION);
    t->name = strdup(name);
    t->type_function = fun;
    t->next = vm->type_functions;
    vm->type_functions = t;
}

void tehssl_init_builtins(tehssl_vm_t vm) {
    // TODO
}

#ifdef TEHSSL_TEST
void myfunction(tehssl_vm_t vm, tehssl_object_t scope) { printf("myfunction called!\n"); }
int main(int argc, char* argv[]) {
    const char* str = "~~Hello world!; Foobar\nFor each number in Range 1 to 0x0A -step 3 do { take the Square; Print the Fibbonaci of said square; }; Print \"DONE!!\" 123";
    tehssl_vm_t vm = tehssl_new_vm();

    printf("\n\n-----test 1: garbage collector----\n\n");
    // Make some garbage
    for (int i = 0; i < 5; i++) {
        tehssl_make_number(vm, 123);
        tehssl_make_number(vm, 456.789123);
        tehssl_make_string(vm, "i am cow hear me moo");
        tehssl_make_symbol(vm, "Symbol!", SYMBOL_WORD);
        // This is not garbage, it is on the stack now
        tehssl_push(vm, vm->stack, tehssl_make_number(vm, 1.7E+123));
        tehssl_push(vm, vm->stack, tehssl_make_string(vm, "Foo123"));
    }
    tehssl_register_word(vm, "MyFunction", myfunction);
    printf("%lu objects\n", vm->num_objects);
    tehssl_gc(vm);
    printf("%lu objects after gc\n", vm->num_objects);

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


    printf("\n\n-----tests complete----\n\n");

    tehssl_destroy(vm);
}
#endif
