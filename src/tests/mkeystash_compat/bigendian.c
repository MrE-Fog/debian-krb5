/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/* tests/mkeystash_compat/bigendian.c */
/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <stdio.h>

/*
 * Test to see if system is bigendian
 * Returns 0 if it is big endian
 *         1 if it is little endian
 */
int main()
{
    int int_var = 1;
    unsigned char *char_array = (unsigned char*)&int_var;

    if (char_array[0] == 0)
        return 0; /* big endian */
    else
        return 1; /* little endian */
}
