#ifndef __vww_H__
#define __vww_H__

#define __PREFIX(x) vww ## x
// Include basic GAP builtins defined in the Autotiler
#include "Gap.h"

#ifdef __EMUL__
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/param.h>
#include <string.h>
#endif

#define QUOTE(name) #name
#define STR(macro) QUOTE(macro)

extern AT_EMRAMFLASH_EXT_ADDR_TYPE vww_L3_Flash;
#endif