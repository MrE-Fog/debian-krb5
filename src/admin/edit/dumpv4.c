/*
 * admin/edit/dumpv4.c
 *
 * Copyright 1990,1991, 1994 by the Massachusetts Institute of Technology.
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
 * Dump a KDC database into a V4 slave dump.
 */

#ifdef KRB4

#include <des.h>
#include <krb.h>
#include <krb_db.h>
/* MKEYFILE is now defined in kdc.h */
#include <kdc.h>

#include <krb5/krb5.h>
#include <krb5/kdb.h>
#include <krb5/kdb_dbm.h>
#include <krb5/los-proto.h>
#include <krb5/asn1.h>
#include <krb5/config.h>
#include <krb5/sysincl.h>		/* for MAXPATHLEN */
#include <krb5/ext-proto.h>
#include <krb5/func-proto.h>

#include <com_err.h>
#include <stdio.h>

#include "kdb5_edit.h"

struct dump_record {
	char	*comerr_name;
	FILE	*f;
	krb5_encrypt_block *v5master;
	C_Block		v4_master_key;
	Key_schedule	v4_master_key_schedule;
	long	master_key_version;
	char	*realm;
};

static krb5_encrypt_block master_encblock;
static krb5_keyblock master_keyblock;
extern krb5_boolean dbactive;
extern int exit_status;

void update_ok_file();

#define ANAME_SZ 40
#define INST_SZ 40

v4_print_time(file, timeval)
    FILE   *file;
    unsigned long timeval;
{
    struct tm *tm;
    struct tm *gmtime();
    tm = gmtime((time_t *)&timeval);
    fprintf(file, " %04d%02d%02d%02d%02d",
            tm->tm_year < 1900 ? tm->tm_year + 1900: tm->tm_year,
            tm->tm_mon + 1,
            tm->tm_mday,
            tm->tm_hour,
            tm->tm_min);
}



krb5_error_code
dump_v4_iterator(ptr, entry)
krb5_pointer ptr;
krb5_db_entry *entry;
{
    krb5_error_code retval;
    struct dump_record *arg = (struct dump_record *) ptr;
    char *name=NULL, *mod_name=NULL;
    int	i;
    struct v4princ {
      char name[ANAME_SZ+1];
      char instance[INST_SZ+1];
      int max_life;
      int kdc_key_ver, key_version, attributes;
      char mod_name[ANAME_SZ+1];
      char mod_instance[INST_SZ+1];
    } v4princ, *principal;
    des_cblock v4key;

    v4princ.name[ANAME_SZ] = 0;
    v4princ.mod_name[ANAME_SZ] = 0;
    v4princ.instance[INST_SZ] = 0;
    v4princ.mod_instance[INST_SZ] = 0;

    principal = &v4princ;

    if (retval = krb5_unparse_name(entry->principal, &name)) {
	com_err(arg->comerr_name, retval, "while unparsing principal");
	exit_status++;
	return retval;
    }
    if (retval = krb5_unparse_name(entry->mod_name, &mod_name)) {
	free(name);
	com_err(arg->comerr_name, retval, "while unparsing principal");
	exit_status++;
	return retval;
    }

    if (entry->salt_type != KRB5_KDB_SALTTYPE_V4) {
        free(name);
	free(mod_name);
	/* skip this because it's a new style key and we can't help it */
	return 0;
    }

    if (strcmp(krb5_princ_realm(entry->principal)->data,
		arg->realm)) {
        free(name);
	free(mod_name);
	/* skip this because it's a key for a different realm, probably
	   a paired krbtgt key */
	return 0;
    }


    strncpy(principal->name,
	    krb5_princ_component(entry->principal, 0)->data, 
	    ANAME_SZ);
    if (!principal->name[0]) {
      strcpy(principal->name, "*");
    }
    if (!strcmp(principal->name, "host")) {
      strcpy(principal->name, "rcmd");
    }
      
    if (entry->principal->length > 1) {
      char *inst;
      strncpy(principal->instance,
	      krb5_princ_component(entry->principal, 1)->data, 
	      INST_SZ);
      inst = strchr(principal->instance, '.');
      if (inst && strcmp(principal->name, "krbtgt")) {
	/* nuke domain off the end of anything that isn't a tgt */
	*inst = '\0';
      }
    }
    else {
      principal->instance[0] = '*';
      principal->instance[1] = '\0';
    }

    strncpy(principal->mod_name,
	    krb5_princ_component(entry->mod_name, 0)->data, 
	    ANAME_SZ);
    if (!principal->mod_name[0]) {
      strcpy(principal->mod_name, "*");
    }

    if (entry->mod_name->length > 1) {
      strncpy(principal->mod_instance, 
	      krb5_princ_component(entry->mod_name, 1)->data, 
	      INST_SZ);
    }
    else {
      principal->mod_instance[0] = '*';
      principal->mod_instance[1] = '\0';
    }

    principal->max_life = entry->max_life / (60 * 5);
    principal->kdc_key_ver = entry->mkvno; /* ??? not preserved incoming */
    principal->key_version = entry->kvno;
    principal->attributes = 0;	/* ??? not preserved either */
    
    fprintf(arg->f, "%s %s %d %d %d %d ",
	    principal->name,
	    principal->instance,
	    principal->max_life,
	    principal->kdc_key_ver,
	    principal->key_version,
	    principal->attributes);

    handle_one_key(arg, arg->v5master, &entry->key, v4key);

    for (i=0; i<8; i++) {
	fprintf(arg->f, "%02x", ((unsigned char*)v4key)[i]);
	if (i == 3) fputc(' ', arg->f);
    }

    v4_print_time(arg->f, entry->expiration);
    v4_print_time(arg->f, entry->mod_date);

    fprintf(arg->f, " %s %s\n",
	    principal->mod_name,
	    principal->mod_instance);

    free(name);
    free(mod_name);
    return 0;
}
/*ARGSUSED*/

void dump_v4db(argc, argv)
	int	argc;
	char	**argv;
{
	FILE	*f;
	struct dump_record	arg;
	
	if (argc > 2) {
		com_err(argv[0], 0, "Usage: %s filename", argv[0]);
		exit_status++;
		return;
	}
	if (!dbactive) {
		com_err(argv[0], 0, Err_no_database);
		exit_status++;
		return;
	}
	if (argc == 2) {
		/*
		 * Make sure that we don't open and truncate on the fopen,
		 * since that may hose an on-going kprop process.
		 * 
		 * We could also control this by opening for read and
		 * write, doing an flock with LOCK_EX, and then
		 * truncating the file once we have gotten the lock,
		 * but that would involve more OS dependancies than I
		 * want to get into.
		 */
		unlink(argv[1]);
		if (!(f = fopen(argv[1], "w"))) {
			com_err(argv[0], errno,
				"While opening file %s for writing", argv[1]);
			exit_status++;
			return;
		}
	} else {
		f = stdout;
	}
	arg.comerr_name = argv[0];
	arg.f = f;
	handle_keys(&arg);

	/* special handling for K.M since it isn't preserved */
	{
	  des_cblock v4key;
	  int i;

	  /* assume:
	     max lifetime (255)
	     key version == 1 (actually, should be whatever the v5 one is)
	     master key version == key version
	     args == 0 (none are preserved)
	     expiration date is the default 2000
	     last mod time is near zero (arbitrarily.)
	     creator is db_creation *
	     */

	  fprintf(f,"K M 255 1 1 0 ");
	  
	  kdb_encrypt_key (arg.v4_master_key, v4key, 
			   arg.v4_master_key, arg.v4_master_key_schedule, 
			   ENCRYPT);

	  for (i=0; i<8; i++) {
	    fprintf(f, "%02x", ((unsigned char*)v4key)[i]);
	    if (i == 3) fputc(' ', f);
	  }
	  fprintf(f," 200001010459 197001020000 db_creation *\n");
	}

	(void) krb5_db_iterate(dump_v4_iterator, (krb5_pointer) &arg);
	if (argc == 2)
		fclose(f);
	if (argv[1])
		update_ok_file(argv[1]);
}

int handle_keys(arg)
    struct dump_record *arg;
{
    krb5_error_code retval;
    char *defrealm;
    char *mkey_name = 0;
    char *mkey_fullname;
    krb5_principal master_princ;

    if (retval = krb5_get_default_realm(&defrealm)) {
      com_err(arg->comerr_name, retval, 
	      "while retrieving default realm name");
      exit(1);
    }	    
    arg->realm = defrealm;

    /* assemble & parse the master key name */

    if (retval = krb5_db_setup_mkey_name(mkey_name, arg->realm, &mkey_fullname,
					 &master_princ)) {
	com_err(arg->comerr_name, retval, "while setting up master key name");
	exit(1);
    }

    krb5_use_cstype(&master_encblock, DEFAULT_KDC_ETYPE);
    master_keyblock.keytype = DEFAULT_KDC_KEYTYPE;
    if (retval = krb5_db_fetch_mkey(master_princ, &master_encblock, 0,
				    0, 0, &master_keyblock)) {
	com_err(arg->comerr_name, retval, "while reading master key");
	exit(1);
    }
    if (retval = krb5_process_key(&master_encblock, &master_keyblock)) {
	com_err(arg->comerr_name, retval, "while processing master key");
	exit(1);
    }
    arg->v5master = &master_encblock;

    /* now master_encblock is set up for the database, we need the v4 key */
    if (kdb_get_master_key (0, arg->v4_master_key, arg->v4_master_key_schedule) != 0)
      {
	com_err(arg->comerr_name, 0, "Couldn't read v4 master key.");
	exit(1);
      }
    return 0;
}

handle_one_key(arg, v5master, v5key, v4key)
     struct dump_record *arg;
     krb5_encrypt_block *v5master;
     krb5_encrypted_keyblock *v5key;
     des_cblock v4key;
{
    krb5_error_code retval;

    krb5_keyblock v4v5key;
    krb5_keyblock v5plainkey;
    /* v4key is the actual v4 key from the file. */

    retval = krb5_kdb_decrypt_key(v5master, v5key, &v5plainkey);
    if (retval) {
	return retval;
    }

    /* v4v5key.contents = (krb5_octet *)v4key; */
    /* v4v5key.keytype = KEYTYPE_DES; */
    /* v4v5key.length = sizeof(v4key); */

    memcpy(v4key, v5plainkey.contents, sizeof(des_cblock));
    kdb_encrypt_key (v4key, v4key, 
		     arg->v4_master_key, arg->v4_master_key_schedule, 
		     ENCRYPT);

    return 0;
}

#else /* KRB4 */
void dump_v4db(argc, argv)
	int	argc;
	char	**argv;
{
	printf("This version of krb5_edit does not support the V4 dump command.\n");
}
#endif /* KRB4 */
