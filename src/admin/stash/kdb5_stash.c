/*
 * admin/stash/kdb5_stash.c
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
 * Store the master database key in a file.
 */

#include "k5-int.h"
#include "com_err.h"
#include <stdio.h>

extern int errno;

krb5_keyblock master_keyblock;
krb5_principal master_princ;
krb5_encrypt_block master_encblock;

static void
usage(who, status)
char *who;
int status;
{
    fprintf(stderr, "usage: %s [-d dbpathname] [-r realmname] [-k keytype]\n\
\t[-e etype] [-M mkeyname] [-f keyfile]\n",
	    who);
    exit(status);
}


void
main(argc, argv)
int argc;
char *argv[];
{
    extern char *optarg;
    int optchar;
    krb5_error_code retval;
    char *dbname = DEFAULT_KDB_FILE;
    char *realm = 0;
    char *mkey_name = 0;
    char *mkey_fullname;
    char *keyfile = 0;
    krb5_context context;

    int keytypedone = 0;
    krb5_enctype etype = 0xffff;

    if (strrchr(argv[0], '/'))
	argv[0] = strrchr(argv[0], '/')+1;

    krb5_init_context(&context);
    krb5_init_ets(context);

    while ((optchar = getopt(argc, argv, "d:r:k:M:e:f:")) != EOF) {
	switch(optchar) {
	case 'd':			/* set db name */
	    dbname = optarg;
	    break;
	case 'r':
	    realm = optarg;
	    break;
	case 'k':
	    master_keyblock.keytype = atoi(optarg);
	    keytypedone++;
	    break;
	case 'M':			/* master key name in DB */
	    mkey_name = optarg;
	    break;
	case 'e':
	    etype = atoi(optarg);
	    break;
	case 'f':
	    keyfile = optarg;
	    break;
	case '?':
	default:
	    usage(argv[0], 1);
	    /*NOTREACHED*/
	}
    }

    if (!keytypedone)
	master_keyblock.keytype = DEFAULT_KDC_KEYTYPE;

    if (!valid_keytype(master_keyblock.keytype)) {
	com_err(argv[0], KRB5_PROG_KEYTYPE_NOSUPP,
		"while setting up keytype %d", master_keyblock.keytype);
	exit(1);
    }

    if (etype == 0xffff)
	etype = DEFAULT_KDC_ETYPE;

    if (!valid_etype(etype)) {
	com_err(argv[0], KRB5_PROG_ETYPE_NOSUPP,
		"while setting up etype %d", etype);
	exit(1);
    }

    krb5_use_cstype(context, &master_encblock, etype);

    if (retval = krb5_db_set_name(context, dbname)) {
	com_err(argv[0], retval, "while setting active database to '%s'",
		dbname);
	exit(1);
    }
    if (!realm) {
	if (retval = krb5_get_default_realm(context, &realm)) {
	    com_err(argv[0], retval, "while retrieving default realm name");
	    exit(1);
	}	    
    }

    /* assemble & parse the master key name */

    if (retval = krb5_db_setup_mkey_name(context, mkey_name, realm, 
					 &mkey_fullname, &master_princ)) {
	com_err(argv[0], retval, "while setting up master key name");
	exit(1);
    }

    if (retval = krb5_db_init(context)) {
	com_err(argv[0], retval, "while initializing the database '%s'",
		dbname);
	exit(1);
    }

    /* TRUE here means read the keyboard, but only once */
    if (retval = krb5_db_fetch_mkey(context, master_princ, &master_encblock,
				    TRUE, FALSE, 0, &master_keyblock)) {
	com_err(argv[0], retval, "while reading master key");
	(void) krb5_db_fini(context);
	exit(1);
    }
    if (retval = krb5_db_verify_master_key(context, master_princ, 
					   &master_keyblock,&master_encblock)) {
	com_err(argv[0], retval, "while verifying master key");
	(void) krb5_db_fini(context);
	exit(1);
    }	
    if (retval = krb5_db_store_mkey(context, keyfile, master_princ, 
				    &master_keyblock)) {
	com_err(argv[0], errno, "while storing key");
	memset((char *)master_keyblock.contents, 0, master_keyblock.length);
	(void) krb5_db_fini(context);
	exit(1);
    }
    memset((char *)master_keyblock.contents, 0, master_keyblock.length);
    if (retval = krb5_db_fini(context)) {
	com_err(argv[0], retval, "closing database '%s'", dbname);
	exit(1);
    }

    exit(0);
}
