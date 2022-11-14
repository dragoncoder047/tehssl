# TEHSSL Internals

## Object Representations

Lisp historically used a 2-cell "cons" pair for its objects, representing other types with special invalid pointer values.

TEHSSL has a few more requirements that that, so it uses 5 cells' worth for each object.

1. Not a pointer. It is used to store metadata like mark bits and the object's type.
2. Always points to the next allocated object; used by the garbage collector. Inaccessible to the user.
3. Usually points to a `char*` string, or a key or the upper bytes of a `double`.
4. Usually points to the value, an object of interest depending on the type of the object.
5. Usually points to the next item in the linked list or stack.

Cells 3-5 change based on the object. Here are how all the types use them (may be out of date):

|   Type   | Cell 3             | Cell 4         | Cell 5             |
|:--------:|:------------------ |:-------------- |:------------------ |
|   LIST   |                    | value          | next item          |
|   DICT   | key                | value          | next entry         |
|   LINE   |                    | item           | next thing in line |
|   BLOCK  |                    | code           | next line          |
|  CLOSURE | closed-over scope  | code block     |                    |
|  NUMBER  | `double` (2 cells) |                |                    |
|  SYMBOL  | `char*` name       |                |                    |
|  STRING  | `char*` contents   |                |                    |
|  STREAM  | `char*` id         | `FILE*`        |                    |
| USERTYPE | `char*` typename   | any pointer    | any pointer        |
|   SCOPE  | functions list     | variables list | parent scope       |
| UFUNCTION         |   `char*` name                    |      function BLOCK          |    next function                |
| CFUNCTION         |   `char*` name                    |      C function pointer          |    next function                |
| TFUNCTION         |   `char*` type name                    |      C type function pointer          |    next type function                |
| VARIABLE         |   `char*` name                    |      value          |    next variable                |
| TOKEN         |   `char*` token contents                    |                |                     |
