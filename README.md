# TEHSSL

(pronounced "tessel")

The Tiny Embedded Hot-Swapping Scripting Language.

Reccommended file extension for scripts: `.teh`

It's an Arduino library! Embed it in your projects, hook up a microSD card, and never use the Arduino IDE again!

## Embedding

Download `tehssl.c` and link to it in your arduino sketch. Here is a basic example of what functions you will need to call:

```cpp
// this is your custom function
tehssl_result_t myfunction(struct tehssl_vm_t*) {
    // do something
    return OK;
}
void loop() {

    // Returns a pointer to a new TEHSSL VM malloc()ed on the heap
    struct tehssl_vm_t* vm = tehssl_new_vm();

    // Registers the builtin functions
    tehssl_init_builtins(vm);

    // Registers a custom function onto the global scope
    tehssl_regsiter(vm, "MyFunction", myfunction);

    // Run some code
    // TODO

    // Free the VM and all its objects
    tehssl_destroy(vm);
}

```
