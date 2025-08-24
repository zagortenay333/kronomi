#include "base/core.h"

#if OS_LINUX
    #include "os/linux/fs.cpp"
    #include "os/linux/time.cpp"
    #include "os/linux/info.cpp"
#else
    #error "Bad os."
#endif
