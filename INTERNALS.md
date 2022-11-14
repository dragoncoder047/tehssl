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
| SYMBOL     | `char*` name             |                          |                    |
| STRING     | `char*` contents         |                          |                    |
| STREAM     | `char*` id               | `FILE*`                  |                    |
| USERTYPE   | `char*` typename         | any pointer              | any pointer        |
| SCOPE      | functions list           | variables list           | parent scope       |
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
    3. If it starts with a `\`, it's a literal symbol, make it a SYMBOL and **set** the LITERAL_SYMBOL flag.
    4. If it's a symbol registered as a macro, pass the macro the stream and a pointer to the current line so it can operate on them.
    5. Otherwise, it's a word symbol, make it a SYMBOL and **clear** the LITERAL_SYMBOL flag. (Note that keyword-symbols which start with `.` are not handled here; that's the evaluator's job.)
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

## Evaluation

Coming from the compiler/reader is the tree described above
