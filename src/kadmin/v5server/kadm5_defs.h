/*
 * kadmin/v5server/kadm5_defs.h
 *
 * Copyright 1995 by the Massachusetts Institute of Technology.
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
 */

/*
 * kadmind5
 * Version 5 administrative daemon.
 */
#ifndef	KADM5_DEFS_H__
#define	KADM5_DEFS_H__

/*
 * Debug definitions.
 */
#define	DEBUG_SPROC	1
#define	DEBUG_OPERATION	2
#define	DEBUG_HOST	4
#define	DEBUG_REALM	8
#define	DEBUG_REQUESTS	16
#define	DEBUG_ACL	32
#define	DEBUG_PROTO	64
#define	DEBUG_CALLS	128
#define	DEBUG_NOSLAVES	256
#ifdef	DEBUG
#define	DPRINT(l1, cl, al)	if ((cl & l1) != 0) printf al
#else	/* DEBUG */
#define	DPRINT(l1, cl, al)
#endif	/* DEBUG */
#define	DLOG(l1, cl, msg)	if ((cl & l1) != 0)	\
					com_err(programname, 0, msg)

/*
 * Access control bits.
 */
#define	ACL_ADD_PRINCIPAL	1
#define	ACL_DELETE_PRINCIPAL	2
#define	ACL_MODIFY_PRINCIPAL	4
#define	ACL_CHANGEPW		8
#define	ACL_CHANGE_OWN_PW	16
#define	ACL_INQUIRE		32
#define	ACL_EXTRACT		64
#define	ACL_RENAME_PRINCIPAL	(ACL_ADD_PRINCIPAL+ACL_DELETE_PRINCIPAL)

#define	ACL_PRINCIPAL_MASK	(ACL_ADD_PRINCIPAL|ACL_DELETE_PRINCIPAL|\
				 ACL_MODIFY_PRINCIPAL)
#define	ACL_PASSWD_MASK		(ACL_CHANGEPW|ACL_CHANGE_OWN_PW)
#define	ACL_ALL_MASK		(ACL_ADD_PRINCIPAL	| \
				 ACL_DELETE_PRINCIPAL	| \
				 ACL_MODIFY_PRINCIPAL	| \
				 ACL_CHANGEPW		| \
				 ACL_CHANGE_OWN_PW	| \
				 ACL_INQUIRE		| \
				 ACL_EXTRACT)
/*
 * Subcodes.
 */
#define	KADM_BAD_ARGS		10
#define	KADM_BAD_CMD		11
#define	KADM_NO_CMD		12
#define	KADM_BAD_PRINC		20
#define	KADM_PWD_TOO_SHORT	21
#define	KADM_PWD_WEAK		22
#define	KADM_NOT_ALLOWED	100

/*
 * Inter-module function prototypes
 */

/* srv_key.c */
krb5_error_code key_init
	PROTOTYPE((krb5_context,
		   int,
		   int,
		   int,
		   char *,
		   int,
		   char *,
		   char *,
		   char *));
void key_finish
	PROTOTYPE((krb5_context,
		   int));
krb5_error_code key_string_to_keys
	PROTOTYPE((krb5_context,
		   krb5_principal,
		   krb5_data *,
		   krb5_int32,
		   krb5_int32,
		   krb5_keyblock *,
		   krb5_keyblock *,
		   krb5_data *,
		   krb5_data *));
krb5_error_code key_random_key
	PROTOTYPE((krb5_context,
		   krb5_keyblock *));
krb5_error_code key_encrypt_keys
	PROTOTYPE((krb5_context,
		   krb5_principal,
		   krb5_keyblock *,
		   krb5_keyblock *,
		   krb5_encrypted_keyblock *,
		   krb5_encrypted_keyblock *));
krb5_error_code key_decrypt_keys
	PROTOTYPE((krb5_context,
		   krb5_principal,
		   krb5_encrypted_keyblock *,
		   krb5_encrypted_keyblock *,
		   krb5_keyblock *,
		   krb5_keyblock *));
krb5_boolean key_pwd_is_weak
	PROTOTYPE((krb5_context,
		   krb5_principal,
		   krb5_data *,
		   krb5_int32,
		   krb5_int32));
krb5_db_entry *key_master_entry();
char *key_master_realm();
krb5_keytab key_keytab_id();

/* srv_acl.c */
krb5_error_code acl_init
	PROTOTYPE((krb5_context,
		   int,
		   char *));
void acl_finish
	PROTOTYPE((krb5_context,
		   int));
krb5_boolean acl_op_permitted
	PROTOTYPE((krb5_context,
		   krb5_principal,
		   krb5_int32));

/* srv_output.c */
krb5_error_code output_init
	PROTOTYPE((krb5_context,
		   int,
		   char *,
		   krb5_boolean));
void output_finish
	PROTOTYPE((krb5_context,
		   int));
krb5_boolean output_lang_supported
	PROTOTYPE((char *));
char *output_krb5_errmsg
	PROTOTYPE((char *,
		   krb5_boolean,
		   krb5_int32));
char *output_adm_error
	PROTOTYPE((char *,
		   krb5_boolean,
		   krb5_int32,
		   krb5_int32,
		   krb5_int32,
		   krb5_data *));

/* srv_net.c */
krb5_error_code net_init
	PROTOTYPE((krb5_context,
		   int,
		   krb5_int32));
void net_finish
	PROTOTYPE((krb5_context,
		   int));
krb5_error_code net_dispatch
	PROTOTYPE((krb5_context));
krb5_principal net_server_princ();

/* proto_serv.c */
krb5_error_code proto_init
	PROTOTYPE((krb5_context,
		   int,
		   int));
void proto_finish
	PROTOTYPE((krb5_context,
		   int));
krb5_error_code proto_serv
	PROTOTYPE((krb5_context,
		   krb5_int32,
		   int,
		   void *,
		   void *));

/* passwd.c */
krb5_int32 passwd_check
	PROTOTYPE((krb5_context,
		   int,
		   krb5_auth_context *,
		   krb5_ticket *,
		   krb5_data *,
		   krb5_int32 *));
krb5_int32 passwd_change
	PROTOTYPE((krb5_context,
		   int,
		   krb5_auth_context *,
		   krb5_ticket *,
		   krb5_data *,
		   krb5_data *,
		   krb5_int32 *));
krb5_boolean passwd_check_npass_ok
	PROTOTYPE((krb5_context,
		   int,
		   krb5_principal,
		   krb5_db_entry *,
		   krb5_data *,
		   krb5_int32 *));

/* admin.c */
krb5_error_code admin_add_principal
	PROTOTYPE((krb5_context,
		   int,
		   krb5_ticket *,
		   krb5_int32,
		   krb5_data *));
krb5_error_code admin_delete_principal
	PROTOTYPE((krb5_context,
		   int,
		   krb5_ticket *,
		   krb5_data *));
krb5_error_code admin_rename_principal
	PROTOTYPE((krb5_context,
		   int,
		   krb5_ticket *,
		   krb5_data *,
		   krb5_data *));
krb5_error_code admin_modify_principal
	PROTOTYPE((krb5_context,
		   int,
		   krb5_ticket *,
		   krb5_int32,
		   krb5_data *));
krb5_error_code admin_change_opw
	PROTOTYPE((krb5_context,
		   int,
		   krb5_ticket *,
		   krb5_data *,
		   krb5_data *));
krb5_error_code admin_change_orandpw
	PROTOTYPE((krb5_context,
		   int,
		   krb5_ticket *,
		   krb5_data *));
krb5_error_code admin_inquire
	PROTOTYPE((krb5_context,
		   int,
		   krb5_ticket *,
		   krb5_data *,
		   krb5_int32 *,
		   krb5_data **));
krb5_error_code admin_extract_key
	PROTOTYPE((krb5_context,
		   int,
		   krb5_ticket *,
		   krb5_data *,
		   krb5_data *,
		   krb5_int32 *,
		   krb5_data **));
#endif	/* KADM5_DEFS_H__ */
