/*
 * Copyright 1993 by OpenVision Technologies, Inc.
 * 
 * Permission to use, copy, modify, distribute, and sell this software
 * and its documentation for any purpose is hereby granted without fee,
 * provided that the above copyright notice appears in all copies and
 * that both that copyright notice and this permission notice appear in
 * supporting documentation, and that the name of OpenVision not be used
 * in advertising or publicity pertaining to distribution of the software
 * without specific, written prior permission. OpenVision makes no
 * representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 * 
 * OPENVISION DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL OPENVISION BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF
 * USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _GSSAPIP_KRB5_H_
#define _GSSAPIP_KRB5_H_

#include <krb5/krb5.h>
#include <memory.h>
#include <netinet/in.h>

/* work around sunos braindamage */
#ifdef major
#undef major
#endif
#ifdef minor
#undef minor
#endif

/* this must be after <krb5/krb5.h>, since krb5 #defines xfree(), too */
#include "../generic/gssapiP_generic.h"
#include "gssapi_krb5.h"
#include "gssapi_krb5_err.h"

/** constants **/

#define CKSUMTYPE_KG_CB		0x8003

#define KG_TOK_CTX_AP_REQ	0x0100
#define KG_TOK_CTX_AP_REP	0x0200
#define KG_TOK_CTX_ERROR	0x0300
#define KG_TOK_SIGN_MSG		0x0101
#define KG_TOK_SEAL_MSG		0x0201
#define KG_TOK_DEL_CTX		0x0102

/** internal types **/

typedef krb5_principal krb5_gss_name_t;

typedef struct _krb5_gss_cred_id_rec {
   /* name/type of credential */
   int usage;
   krb5_principal princ;	/* this is not interned as a gss_name_t */

   /* keytab (accept) data */
   krb5_keytab keytab;

   /* ccache (init) data */
   krb5_ccache ccache;
   krb5_timestamp tgt_expire;
} krb5_gss_cred_id_rec, *krb5_gss_cred_id_t; 

typedef struct _krb5_gss_enc_desc {
   int processed;
   krb5_keyblock *key;
   krb5_encrypt_block eblock;
} krb5_gss_enc_desc;

typedef struct _krb5_gss_ctx_id_rec {
   int initiate;	/* nonzero if initiating, zero if accepting */
   int mutual;
   int seed_init;
   unsigned char seed[16];
   krb5_gss_cred_id_t cred;
   krb5_principal here;
   krb5_principal there;
   krb5_keyblock *subkey;
   krb5_gss_enc_desc enc;
   krb5_gss_enc_desc seq;
   krb5_timestamp endtime;
   krb5_flags flags;
   krb5_int32 seq_send;
   krb5_int32 seq_recv;
   int established;
   int big_endian;
   krb5_context context;
} krb5_gss_ctx_id_rec, krb5_gss_ctx_id_t;

extern void *kg_vdb;

extern krb5_context kg_context;

/* helper macros */

#define kg_save_name(name)		g_save_name(&kg_vdb,name)
#define kg_save_cred_id(cred)		g_save_cred_id(&kg_vdb,cred)
#define kg_save_ctx_id(ctx)		g_save_ctx_id(&kg_vdb,ctx)

#define kg_validate_name(name)		g_validate_name(&kg_vdb,name)
#define kg_validate_cred_id(cred)	g_validate_cred_id(&kg_vdb,cred)
#define kg_validate_ctx_id(ctx)		g_validate_ctx_id(&kg_vdb,ctx)

#define kg_delete_name(name)		g_delete_name(&kg_vdb,name)
#define kg_delete_cred_id(cred)		g_delete_cred_id(&kg_vdb,cred)
#define kg_delete_ctx_id(ctx)		g_delete_ctx_id(&kg_vdb,ctx)

/** helper functions **/

OM_uint32 kg_get_defcred 
	PROTOTYPE((OM_uint32 *minor_status, 
		   gss_cred_id_t *cred));

OM_uint32 kg_release_defcred PROTOTYPE((OM_uint32 *minor_status));

krb5_error_code kg_checksum_channel_bindings PROTOTYPE((gss_channel_bindings_t cb,
					     krb5_checksum *cksum,
					     int bigend));

krb5_error_code kg_make_seq_num PROTOTYPE((krb5_gss_enc_desc *ed, int direction,
				int seqnum, unsigned char *cksum,
				unsigned char *buf));

krb5_error_code kg_make_seed PROTOTYPE((krb5_keyblock *key, unsigned char *seed));

int kg_confounder_size PROTOTYPE((krb5_gss_enc_desc *ed));

krb5_error_code kg_make_confounder PROTOTYPE((krb5_gss_enc_desc *ed, unsigned char *buf));

int kg_encrypt_size PROTOTYPE((krb5_gss_enc_desc *ed, int n));

krb5_error_code kg_encrypt PROTOTYPE((krb5_gss_enc_desc *ed, krb5_pointer iv,
			   krb5_pointer in, krb5_pointer out, int length));

krb5_error_code kg_decrypt PROTOTYPE((krb5_gss_enc_desc *ed, krb5_pointer iv,
			   krb5_pointer in, krb5_pointer out, int length));

OM_uint32 kg_seal PROTOTYPE((OM_uint32 *minor_status,
		  gss_ctx_id_t context_handle,
		  int conf_req_flag,
		  int qop_req,
		  gss_buffer_t input_message_buffer,
		  int *conf_state,
		  gss_buffer_t output_message_buffer,
		  int toktype));

OM_uint32 kg_unseal PROTOTYPE((OM_uint32 *minor_status,
		    gss_ctx_id_t context_handle,
		    gss_buffer_t input_token_buffer,
		    gss_buffer_t message_buffer,
		    int *conf_state,
		    int *qop_state,
		    int toktype));

/** declarations of internal name mechanism functions **/

OM_uint32 krb5_gss_acquire_cred
PROTOTYPE( (krb5_context,
	    OM_uint32*,       /* minor_status */
            gss_name_t,       /* desired_name */
            OM_uint32,        /* time_req */
            gss_OID_set,      /* desired_mechs */
            int,              /* cred_usage */
            gss_cred_id_t*,   /* output_cred_handle */
            gss_OID_set*,     /* actual_mechs */
            OM_uint32*        /* time_rec */
           ));

OM_uint32 krb5_gss_release_cred
PROTOTYPE( (krb5_context,
	    OM_uint32*,       /* minor_status */
            gss_cred_id_t*    /* cred_handle */
           ));

OM_uint32 krb5_gss_init_sec_context
PROTOTYPE( (krb5_context,
	    OM_uint32*,       /* minor_status */
            gss_cred_id_t,    /* claimant_cred_handle */
            gss_ctx_id_t*,    /* context_handle */
            gss_name_t,       /* target_name */
            const_gss_OID,    /* mech_type */
            int,              /* req_flags */
            OM_uint32,        /* time_req */
            gss_channel_bindings_t,
                              /* input_chan_bindings */
            gss_buffer_t,     /* input_token */
            gss_OID*,         /* actual_mech_type */
            gss_buffer_t,     /* output_token */
            int*,             /* ret_flags */
            OM_uint32*        /* time_rec */
           ));

OM_uint32 krb5_gss_accept_sec_context
PROTOTYPE( (krb5_context,
	    OM_uint32*,       /* minor_status */
            gss_ctx_id_t*,    /* context_handle */
            gss_cred_id_t,    /* verifier_cred_handle */
            gss_buffer_t,     /* input_token_buffer */
            gss_channel_bindings_t,
                              /* input_chan_bindings */
            gss_name_t*,      /* src_name */
            gss_OID*,         /* mech_type */
            gss_buffer_t,     /* output_token */
            int*,             /* ret_flags */
            OM_uint32*,       /* time_rec */
            gss_cred_id_t*    /* delegated_cred_handle */
           ));

OM_uint32 krb5_gss_process_context_token
PROTOTYPE( (krb5_context,
	    OM_uint32*,       /* minor_status */
            gss_ctx_id_t,     /* context_handle */
            gss_buffer_t      /* token_buffer */
           ));

OM_uint32 krb5_gss_delete_sec_context
PROTOTYPE( (krb5_context,
	    OM_uint32*,       /* minor_status */
            gss_ctx_id_t*,    /* context_handle */
            gss_buffer_t      /* output_token */
           ));

OM_uint32 krb5_gss_context_time
PROTOTYPE( (krb5_context,
	    OM_uint32*,       /* minor_status */
            gss_ctx_id_t,     /* context_handle */
            OM_uint32*        /* time_rec */
           ));

OM_uint32 krb5_gss_sign
PROTOTYPE( (krb5_context,
	    OM_uint32*,       /* minor_status */
            gss_ctx_id_t,     /* context_handle */
            int,              /* qop_req */
            gss_buffer_t,     /* message_buffer */
            gss_buffer_t      /* message_token */
           ));

OM_uint32 krb5_gss_verify
PROTOTYPE( (krb5_context,
	    OM_uint32*,       /* minor_status */
            gss_ctx_id_t,     /* context_handle */
            gss_buffer_t,     /* message_buffer */
            gss_buffer_t,     /* token_buffer */
            int*              /* qop_state */
           ));

OM_uint32 krb5_gss_seal
PROTOTYPE( (krb5_context,
	    OM_uint32*,       /* minor_status */
            gss_ctx_id_t,     /* context_handle */
            int,              /* conf_req_flag */
            int,              /* qop_req */
            gss_buffer_t,     /* input_message_buffer */
            int*,             /* conf_state */
            gss_buffer_t      /* output_message_buffer */
           ));

OM_uint32 krb5_gss_unseal
PROTOTYPE( (krb5_context,
	    OM_uint32*,       /* minor_status */
            gss_ctx_id_t,     /* context_handle */
            gss_buffer_t,     /* input_message_buffer */
            gss_buffer_t,     /* output_message_buffer */
            int*,             /* conf_state */
            int*              /* qop_state */
           ));

OM_uint32 krb5_gss_display_status
PROTOTYPE( (krb5_context,
	    OM_uint32*,       /* minor_status */
            OM_uint32,        /* status_value */
            int,              /* status_type */
            const_gss_OID,    /* mech_type */
            int*,             /* message_context */
            gss_buffer_t      /* status_string */
           ));

OM_uint32 krb5_gss_indicate_mechs
PROTOTYPE( (krb5_context,
	    OM_uint32*,       /* minor_status */
            gss_OID_set*      /* mech_set */
           ));

OM_uint32 krb5_gss_compare_name
PROTOTYPE( (krb5_context,
	    OM_uint32*,       /* minor_status */
            gss_name_t,       /* name1 */
            gss_name_t,       /* name2 */
            int*              /* name_equal */
           ));

OM_uint32 krb5_gss_display_name
PROTOTYPE( (krb5_context,
	    OM_uint32*,      /* minor_status */
            gss_name_t,      /* input_name */
            gss_buffer_t,     /* output_name_buffer */
            gss_OID*         /* output_name_type */
           ));

OM_uint32 krb5_gss_import_name
PROTOTYPE( (krb5_context,
	    OM_uint32*,       /* minor_status */
            gss_buffer_t,     /* input_name_buffer */
            const_gss_OID,    /* input_name_type */
            gss_name_t*       /* output_name */
           ));

OM_uint32 krb5_gss_release_name
PROTOTYPE( (krb5_context,
	    OM_uint32*,       /* minor_status */
            gss_name_t*       /* input_name */
           ));

OM_uint32 krb5_gss_inquire_cred
PROTOTYPE( (krb5_context,
	    OM_uint32 *,      /* minor_status */
            gss_cred_id_t,    /* cred_handle */
            gss_name_t *,     /* name */
            OM_uint32 *,      /* lifetime */
            int *,            /* cred_usage */
            gss_OID_set *     /* mechanisms */
           ));

OM_uint32 krb5_gss_inquire_context
PROTOTYPE( (krb5_context,
	    OM_uint32*,       /* minor_status */
	    gss_ctx_id_t,     /* context_handle */
	    gss_name_t*,      /* initiator_name */
	    gss_name_t*,      /* acceptor_name */
	    OM_uint32*,       /* lifetime_rec */
	    gss_OID*,         /* mech_type */
	    int*,             /* ret_flags */
	    int*              /* locally_initiated */
	   ));

OM_uint32 kg_get_context();
	
#endif /* _GSSAPIP_KRB5_H_ */
