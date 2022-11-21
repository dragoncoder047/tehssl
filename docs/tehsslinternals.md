# TEHSSL Internals

## Object Representations

Lisp historically used a 2-cell "cons" pair for its objects, representing other types with special invalid pointer values.

TEHSSL has a few more requirements that that, so it uses 5 cells' worth for each object. Here are how the cells are used:

1. Stores metadata like mark bits and the object's type.
2. Always points to the next allocated object; used by the garbage collector. Inaccessible to the user.
3. Usually points to a `char*` string, or a key or the upper bytes of a `double`.
4. Usually points to the value, an object of interest depending on the type of the object.
5. Usually points to the next item in the linked list or stack.

Cells 3-5 change based on the object. Here are how all the types use them (may be out of date):

| Type       | Cell 3                   | Cell 4                   | Cell 5             |
|:---------- |:------------------------ |:------------------------ |:------------------ |
| LIST       |                          | value                    | next item          |
| DICT       | key                      | value                    | next entry         |
| LINE       |                          | item                     | next thing in line |
| BLOCK      |                          | code                     | next line          |
| CLOSURE    | closed-over scope        | code block               |                    |
| NUMBER     | `double` (upper 32 bits) | `double` (lower 32 bits) |                    |
| SINGLETON  | singleton ID             |                          |                    |
| SYMBOL     | `char*` name             |                          |                    |
| STRING     | `char*` contents         |                          |                    |
| STREAM     | `char*` id               | `FILE*`                  |                    |
| USERTYPE   | `char*` typename         | any pointer              | any pointer        |
| SCOPE      |                          | bindings list            | parent scope       |
| UNFUNCTION | `char*` name             | function BLOCK           | next function      |
| CNFUNCTION | `char*` name             | C function pointer       | next function      |
| CMFUNCTION | `char*` name             | C macro function pointer | next macro         |
| TFUNCTION  | `char*` type name        | C type function pointer  | next type function |
| VARIABLE   | `char*` name             | value                    | next variable      |

## Program Structure

The tokenizer first does the job of stripping comments and informal syntax. From the token stream, the compiler (or should I say "reader"?) turns this into a binary tree of BLOCK and LINE objects. The compiler maintains the current block it is in and the current line (i.e. the stuff between semicolons), and works like this:

1. If the current token is a `{`, recursively compile until a `}` and push that onto the line.
2. If the current token is a `;`, push the current line onto the current block, and start a new line.
3. Otherwise, it's a literal token.
    1. If it's obviously a string (i.e. strats with `"`), make a STRING object.
    2. Use `sscanf()` to try to get a double out of it. If that succeeds, make it a NUMBER.
    3. If it starts with a `:`, it's a literal symbol, make it a SYMBOL and **set** the LITERAL_SYMBOL flag.
    4. If it's a symbol registered as a macro, pass the macro the stream and a pointer to the current line so it can operate on them.
    5. Otherwise, it's a word symbol, make it a SYMBOL and **clear** the LITERAL_SYMBOL flag. (Note that keyword-symbols which start with `-` are not handled here; that's the evaluator's job.)
4. When the specified end-of-file marker is reached (EOF for top-level, `}` for recursive) return the current block.

The resulting code structure is laid out like this:

```text
root --> BLOCK --> LINE --> LINE --> LINE --> LINE --> null
           |        |        |        |        |
           |        V        V        V        V
           |      item     item     item     item
           V
         BLOCK --> LINE --> LINE --> LINE --> LINE --> null
           |        |        |        |        |
           |        V        V        V        V
           |      item     item     item     item
           V
         BLOCK --> LINE --> LINE --> LINE --> LINE --> null
           |        |        |        |        |
           |        V        V        V        V
           |      item     item     item     item
           V
         BLOCK --> LINE --> LINE --> LINE --> LINE --> null
           |        |        |        |        |
           V        V        V        V        V
         null     item     item     item     item
```

Each of the `item` values can be a literal produced in step 3 above, or a sub-block.

### Types of Literals

| Example | Description |
|:-------:|:----------- |
| `True`, `False` | Booleans |
| `Null` | An object that exists, is defined, and is empty (a C `NULL` pointer) |
| `Undefined` | A sentinel value for an object that exists, but is not defined. |
| `DNE` | A sentinel value for an object that does not exist. |
| `123.456`, `1.23456e+2` | A double-precision floating point number |
| `123`, `0x7B`, `0b1111011` | 32-bit signed integers |
| `NaN` | The IEEE 754 Not-a-Number value (what you get when dividing zero by zero, for example) |
| `"string"` | A string, of course. Only double quotes can be used right now, but if I feel that a single quote isn't going to be used for anything, I might add single-quote support. |
| Anything else | A symbol (see below) |

## Evaluation

Coming from the compiler/reader is the tree described above, with special macros (such as `Def` and `Let`) already processed into their dynamic siblings. Executing a block of code (from the `root` node, above) proceeds as follows:

1. The current line is nondestructively reversed. That way, the line doesn't have to be a doubly linked list or have a flag to note that it has already been reversed. I don't know how this hurts the garbage collector, however.
2. The first element of the reversed line (really the last element of the line) is checked.
    1. If it is a string, number, literal symbol, etc. it is simply pushed to the stack.
    2. If it is a word symbol (not a literal), it is looked up in first the variable and then the function namespaces. If both lookups fail, an appropriate error massage is returned. If not, a variable is pushed to the stack, and a function is called.
    3. If it is a sub-block, it is made into a CLOSURE with the current scope, and the closure is pushed to the stack.
3. The next element of the line is tried as in step 2.
4. When the line is exhausted, the next line is reversed, and run as before.

## Keyword Arguments

TEHSSL allows the use of keyword arguments. A keyword-argument is formed by prefixing it with a dash (`-`), and when this is executed, it performs the special "magic" operation of pushing the top stack value onto the keywords dict at the particular key. This allows the programmer to write things such as `-foo 123` and the function will be given a keyword argument of `foo` with a value of 123 -- without it, the function will recieve no keyword argument, and its behavior will ostensibly be changed.

Note that despite the appearance of a keyword argument, they are not the same as a command-line flag -- they must always have a value. You cannot simply write `-foo` with no value on the stack; you'll get a stack underflow error before this.

Inside a user-code function, `&foo` will retrieve the value of the keyword argument named `foo` (`DNE` if it was not set) and `%foo` will pop it from the dict (unsetting it in the process).

### Recognized Keyword Sigils

| Symbol Prefix | Behavior |
|:-------------:|:-------- |
| `foo` (none)        | With a lowercase first letter, this is "informal syntax" and is completely ignored (the tokenizer discards these). With an uppercase first letter this is a normal symbol.
| `:foo` (colon) | Makes a literal symbol, which does nothing but push itself to the stack when run. |
| `-foo` (dash) | Takes the top item of the stack and puts it into the keywords dictionary. |
| `&foo` (ampersand) | Looks up the symbol in the keywords dictionary, pushes `DNE` if it is unset, otherwise the value. |
| `%foo` (percent) | Pops the symbol from the keywords dict (unsetting it), ands returned the popped value. |
| `+foo` (plus) | Shorthand for `-foo True` (setting a keyword to `True`, like a command-line flag). |
