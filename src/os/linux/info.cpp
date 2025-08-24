#include <unistd.h>
#include <sys/sysinfo.h>
#include "os/info.h"

U64 os_get_proc_count () {
    return get_nprocs();
}

U64 os_get_page_size () {
    return sysconf(_SC_PAGESIZE);
}
