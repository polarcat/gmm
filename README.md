Rudimentary graphics memory mapper
---
Kernel module

```
~/> cd kernel
~/> make -C /lib/modules/`uname -r`/build M=$PWD
```

Userspace helpers as header-only library

```bash
#include <libgmm.h>
```
