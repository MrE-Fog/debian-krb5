/*
 * lib/krb5/os/ktdefname.c
 *
 * Copyright 1990 by the Massachusetts Institute of Technology.
 * All Rights Reserved.
 *
 * Export of this software from the United States of America may
 *   require a specific license from the United States Government.
 *   It is the responsibility of any person or organization contemplating
 *   export to obtain such a license before exporting.
 * 
 * WITHIN THAT CONSTRAINT, permission to use, copy, modify, and
 * distribute this software and its documentation for any purpose and
 * without fee is hereby granted, provided that the above copyright
 * notice appear in all copies and that both that copyright notice and
 * this permission notice appear in supporting documentation, and that
 * the name of M.I.T. not be used in advertising or publicity pertaining
 * to distribution of the software without specific, written prior
 * permission.  M.I.T. makes no representations about the suitability of
 * this software for any purpose.  It is provided "as is" without express
 * or implied warranty.
 * 
 *
 * Return default keytab file name.
 */


#include <krb5/krb5.h>
#include <krb5/osconf.h>

#include <krb5/ext-proto.h>

extern char *krb5_defkeyname;

krb5_error_code
krb5_kt_default_name(context, name, namesize)
    krb5_context context;
    char *name;
    int namesize;
{
    strncpy(name, krb5_defkeyname, namesize);
    if (namesize < strlen(krb5_defkeyname))
	return KRB5_CONFIG_NOTENUFSPACE;
    else
	return 0;
}
    
