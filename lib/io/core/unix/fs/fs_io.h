//
// Created by weijing on 2024/5/22.
//

#ifndef SKY_FS_IO_H
#define SKY_FS_IO_H

#if defined(__unix__) || defined(__unix) || (defined(__APPLE__) && defined(__MACH__))



#include "../unix_io.h"
#include <io/fs.h>

#endif

#endif //SKY_FS_IO_H
