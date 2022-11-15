# TEHSSL

(pronounced "tessel")

The Tiny Embedded Hot-Swapping Scripting Language.

[TEHSSL scripting guide](docs/tehsslscripting.md)

[For C++ programmers: Embedding Guide](docs/embeddingguide.md)

[Internals of how TEHSSL Works](docs/tehsslinternals.md)

## Known Bugs

* The garbage collector chokes on `fmemopen()`'ed streams for some reason; I have no idea why. PRs welcome.
