#
# Information about who is compiling the binary....
# This file is in the public domain, such as it is.
#

info_c_sum=`cksum ./info.c.sh`
comp_host=`uname -n`
comp_user=$LOGNAME
comp_time=`date | \
awk '{if (NF == 6) \
         { print $1 " "  $2 " " $3 " "  $6 " at " $4 " " $5 } \
else \
         { print $1 " "  $2 " " $3 " " $7 " at " $4 " " $5 " " $6 }}'`

# Dump the C file...
cat > info.c << __E__O__F__
/*
 * info.c -- info about who compiled this version.
 * This file is auto-magically created.   Changes will be nuked.
 */
#include "config.h"
#ifdef ANONYMOUS_COMPILE
#define USER "<anonymous>"
#else
#define USER "$comp_user"
#endif

const char *compile_user = "$comp_user";
const char *compile_host = "$comp_host";
const char *compile_time = "$comp_time";
const char *info_c_sum   = "$info_c_sum";
const char *compile_info = "Compiled by " USER "@$comp_host on $comp_time";

__E__O__F__
