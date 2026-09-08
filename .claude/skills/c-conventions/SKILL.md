---
name: c-conventions
description: common C code conventions for readability and maintainability
---

C code should generally follow Linuxs kernel style.
Line length can be 140 chararacters or greater, prefer NOT breaking code across lines
All multiline blocks of code in `if`s or loops, should use `{\n }\n`, C supports one line not having a block but this makes merging difficult.

```c
if (test) {
  do_thing();
}
```
single statement `if` can be on the same line `if (verbose) log("...")`, for logs and return statements, altho `{}` is preferred.
loops should always use `{}`.
dont put `{   }` on a single line

log lines should be single line up to 200 chars so grepping code based on logs is simple.
logs should start with a `xxx:` where xxx is a useful tag that unites common code over a module or feature, 
multiple tags shouyld be used `mx: wagval:` when the application gets large

switch fallthroughs should be commented.

Parsing code from TCP connections with push parsers, avoid buffering and then scanning.
