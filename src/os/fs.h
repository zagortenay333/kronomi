#pragma once

#include "base/core.h"
#include "base/mem.h"
#include "base/string.h"

struct FsIter {
    Mem *mem;
    Bool is_directory;
    Bool skip_files;
    Bool skip_directories;
    String directory_path;
    String current_file_name;
    AString current_full_path;
};

U64     fs_file_size            (String path);
Bool    fs_copy                 (String oldpath, String newpath);
Bool    fs_make_dir             (String path);
Bool    fs_move                 (String oldpath, String newpath);
Bool    fs_delete               (String path);
String  fs_get_full_path        (Mem *, String path);
String  fs_current_working_dir  (Mem *);
Bool    fs_make_file_executable (String path);
Bool    fs_file_exists          (String path);
Bool    fs_dir_exists           (String path);
FsIter *fs_iter_new             (Mem *, String path, Bool, Bool);
Bool    fs_iter_next            (FsIter *);
Void    fs_iter_destroy         (FsIter *);
Bool    fs_write_entire_file    (String path, String buf);

// The returned string is 0-terminated, but the 0-terminator
// is not counted by String.count. The extra_space is padding
// at the end of the returned buffer; also not counted.
String  fs_read_entire_file  (Mem *, String path, U64 extra_space);
