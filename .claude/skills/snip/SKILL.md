---
name: snip
description: write unit tests using the snip framework
allowed-tools: bash, snip/sniprun, chmod
---

Where possible, C code should be unit tested with the snip framework.  
Snip tests are a cut-and-paste tests that extract code to test from `.c` and `.h` files.  
A snip test is a `xxx.c.snip` file and an executable `xxx.c.make` file.  
`xxx.c.make` is a bash script that has apre-processor, then compiles the test and runs the test.  
`xxx.c.make` should be chmod +x.

Typical snip `xxx.c.make` is as follows...

```bash
#!/bin/bash
set -euo pipefail

cd $(dirname $0)

snip_test=xxx

musl-gcc -Wall -Werror -Wno-unused-function -g -O0 \
    $snip_test.c \
    -o $snip_test \
    -lm \
    && ./$snip_test \
    && rm -f ./$snip_test ./${snip_test}.c
```

for snip tsting io_uring code `test/snip/jsonlog/jsonlog_uring_write.c.make` has examples for the make stage.

C code that is to be snip tested has pairs of identical comments in the form `//SNIP_xxx`.  
sniprun's preprocessor will find these markers and insert the content into a temporary `.c` file (using awk).

e.g.
```
//SNIP_FILE SNIP_json_h ../../../src/nginx_mods/ngx_json_module/ngx_json.h
//SNIP_FILE SNIP_json_c ../../../src/nginx_mods/ngx_json_module/ngx_json.c
```

SNIP markers are added _below_ any include headers. So SNIP_FILE will only pull in C code, but not any headers.

`//SNIP_xxx` markers can cover whole files (excluding headers) or just specific functions if the function has no dependencies.

Commons mocks, stub, and asserts for snip tests are in `test/snip/include`. 

`c.snip` files define their own headers that replace `<ngx_*.h>` to use mocks & stubs for nginx features.

e.g.

```C
#include "../include/ngx_config.h"
#include "../include/ngx_core.h"
#include "../include/ngx_http.h"
#include "../asserts.h"
```

## Assertions

Snip has common assertions code in 
```C
#include "../asserts.h"
```

c.snip main() functions should use assertions from this header to integration failed and passed counts to the framework.

each c.snip should end
```C
    print_results(argv[0]);
    ... anny cleanup code
    return 0;
```

This will print test assertion counts, but also ensure targets in make fail when snip tests do.

N.B. you cannot nest `assert_require()` type macros.

## Stub maintenance

Occasionally `test/snip/include/ngx_*.h` mocks and stubs need to be updated when new code uses features from nginx that previously were not.
e.g. snip test fails with some `ngx_xxx` symbol not found.
The real headers are in `src/nginx/src`, snips should not refence the real header,  
we should create stubs and re-implementations `test/snip/include`

# Fixtures

Strings, test data, and conf files should generally be loaded from `./fixtures` in snip tests so that humans get visibility of test data.
`fixtures.h` has tools for loading fixture.

## Executing snip tests

Individual snip tests are run with `./snip/sniprun .../xxx/c.snip`  
All snips are run with `make test-snip`, which prints failures at the end of the run.  
snipcat should never be run directly.  
sniprun creates temporary `xxx.c` files (using snipcat) in the same directory as the `c.snip` file, these are removed after _successful_ test runs.  
After _failed_ runs the .c and binary file is left for reference only, (e.g. to validate SNIP_FILE and includes), there is no point editing this file, its regenerated the next run.  



## References

`test/snip/json/json_create.c.snip` is a good example of a snip test.
`https://tp23.org/snip-testing.html`