/*
 * lib/krb5/krb/gen_subkey.c
 *
 * Copyright 1991 by the Massachusetts Institute of Technology.
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
 * Routine to automatically generate a subsession key based on an input key.
 */

#include "k5-int.h"

krb5_error_code
krb5_generate_subkey(context, key, subkey)
    krb5_context context;
    const krb5_keyblock *key;
    krb5_keyblock **subkey;
{
    krb5_pointer random_state;
    krb5_encrypt_block eblock;
    krb5_error_code retval;

    if (!valid_enctype(key->enctype))
	return KRB5_PROG_ETYPE_NOSUPP;

    krb5_use_enctype(context, &eblock, key->enctype);

    if ((retval = krb5_init_random_key(context, &eblock, key, &random_state)))
	return(retval);
    if ((retval = krb5_random_key(context, &eblock, random_state, subkey))) {
	(void) krb5_finish_random_key(context, &eblock, &random_state);
	krb5_xfree(*subkey);
	return retval;
    }	
    /* ignore the error if any, since we've already gotten the key out */
    (void) krb5_finish_random_key(context, &eblock, &random_state);
    return 0;
}

