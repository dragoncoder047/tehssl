# TEHSSL Embedding Guide

## C FFI

### Custom Types

## In an Arduino sketch

Download `tehssl.cpp` and link to it in your arduino sketch. Here is a basic example of what functions you will need to call:

```cpp
#include "tehssl.cpp"

// this is your custom function
tehssl_result_t myfunction(tehssl_vm_t vm, tehssl_object_t scope) {
    // do something
    return OK;
}
void loop() {

    // Returns a pointer to a new TEHSSL VM malloc()ed on the heap
    tehssl_vm_t vm = tehssl_new_vm();

    // Registers the builtin functions
    tehssl_init_builtins(vm);

    // Registers a custom function onto the global scope
    tehssl_register_word(vm, "MyFunction", myfunction);

    // Run some code
    // TODO

    // Free the VM and all its objects
    tehssl_destroy(vm);
}
```
