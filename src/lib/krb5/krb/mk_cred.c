/* 
 * NAME
 *    cred.c
 * 
 * DESCRIPTION
 *    Provide an interface to assemble and disassemble krb5_cred
 *    structures.
 *
 * MODIFIED
 * $Log$
 * Revision 5.9  1995/04/28 01:18:18  keithv
 * Fixes so that the Unix changes no longer breaks on the PC.
 *
 * Revision 5.8  1995/04/26 03:03:11  proven
 * 	* Makefile.in : Added gc_via_tkt.c and removed get_fcreds.c
 * 	* auth_con.c (krb5_auth_con_setaddrs()) : Fixed so it allocates
 * 		space and copies addresses, not just pointer.
 * 	* mk_cred.c: Completely rewritten from sources donated by asriniva.
 * 	* rd_cred.c: Completely rewritten from sources donated by asriniva.
 * 	* mk_priv.c (krb5_mk_priv()), mk_safe.c (krb5_mk_safe()),
 * 	  rd_priv.c (krb5_rd_priv()), and rd_safe (krb5_rd_safe()) :
 * 		Try using a subkey before using the session key for encryption.
 * 	* recvauth.c (krb5_recvauth()): Don't close the rcache on success.
 *
 * Revision 1.3  1995/01/26  00:09:24  asriniva
 * Completely rewrote API to credential passing code.
 *
 * Revision 1.2  1994/12/30  21:57:17  asriniva
 * Killed compile time warnings/errors.
 * Fixed runtime bugs.
 * Require a ticket when calling krb5_initcred.
 * Cleaned up krb5_addticket.
 *
 * Revision 1.1  1994/12/29  17:03:30  asriniva
 * Initial revision
 *
 */
#include <k5-int.h>
#include "auth_con.h"

#include <stddef.h>           /* NULL */
#include <stdlib.h>           /* malloc */
#include <errno.h>            /* ENOMEM */

/*-------------------- encrypt_credencpart --------------------*/

/*
 * encrypt the enc_part of krb5_cred
 */
static krb5_error_code 
encrypt_credencpart(context, pcredpart, pkeyblock, pencdata)
    krb5_context	  context;
    krb5_cred_enc_part 	* pcredpart;
    krb5_keyblock 	* pkeyblock;
    krb5_enc_data 	* pencdata;
{
    krb5_error_code 	  retval;
    krb5_encrypt_block 	  eblock;
    krb5_data 		* scratch;

    if (!valid_etype(pkeyblock->etype))
    	return KRB5_PROG_ETYPE_NOSUPP;

    /* start by encoding to-be-encrypted part of the message */
    if (retval = encode_krb5_enc_cred_part(pcredpart, &scratch))
    	return retval;

    /* put together an eblock for this encryption */

    pencdata->kvno = 0;
    pencdata->etype = pkeyblock->etype;

    krb5_use_cstype(context, &eblock, pkeyblock->etype);
    pencdata->ciphertext.length = krb5_encrypt_size(scratch->length, 
						    eblock.crypto_entry);

    /* add padding area, and zero it */
    if (!(scratch->data = (char *)realloc(scratch->data,
                                          pencdata->ciphertext.length))) {
    	/* may destroy scratch->data */
    	krb5_xfree(scratch);
    	return ENOMEM;
    }

    memset(scratch->data + scratch->length, 0,
           pencdata->ciphertext.length - scratch->length);
    if (!(pencdata->ciphertext.data =
          (char *)malloc(pencdata->ciphertext.length))) {
    	retval = ENOMEM;
    	goto clean_scratch;
    }

    /* do any necessary key pre-processing */
    if (retval = krb5_process_key(context, &eblock, pkeyblock)) {
    	goto clean_encpart;
    }

    /* call the encryption routine */
    if (retval = krb5_encrypt(context, (krb5_pointer)scratch->data,
                              (krb5_pointer)pencdata->ciphertext.data, 
                              scratch->length, &eblock, 0)) {
	krb5_finish_key(context, &eblock);
	goto clean_encpart;
    }
    
    retval = krb5_finish_key(context, &eblock);

clean_encpart:
    if (retval) {
    	memset(pencdata->ciphertext.data, 0, pencdata->ciphertext.length);
        free(pencdata->ciphertext.data);
        pencdata->ciphertext.length = 0;
        pencdata->ciphertext.data = 0;
    }

clean_scratch:
    memset(scratch->data, 0, scratch->length); 
    krb5_free_data(context, scratch);

    return retval;
}

/*----------------------- krb5_mk_ncred_basic -----------------------*/

static krb5_error_code
krb5_mk_ncred_basic(context, ppcreds, nppcreds, keyblock,                 
		    replaydata, local_addr, remote_addr, pcred)
    krb5_context 	  context;
    krb5_creds 	       ** ppcreds;
    krb5_int32 		  nppcreds;
    krb5_keyblock 	* keyblock;
    krb5_replay_data    * replaydata;
    krb5_address  	* local_addr;
    krb5_address  	* remote_addr;
    krb5_cred 		* pcred;
{
    krb5_cred_enc_part 	  credenc;
    krb5_error_code	  retval;
    char 		* tmp;
    int			  i;

    credenc.magic = KV5M_CRED_ENC_PART;

    credenc.s_address = local_addr;
    credenc.r_address = remote_addr;
    credenc.nonce = replaydata->seq;
    credenc.usec = replaydata->usec;
    credenc.timestamp = replaydata->timestamp;

    /* Get memory for creds and initialize it */
    if ((credenc.ticket_info = (krb5_cred_info **)
		malloc((size_t) (sizeof(krb5_cred_info *) * (nppcreds + 1)))) == NULL) {
        return ENOMEM;
    }
    if ((tmp = (char *)malloc((size_t) (sizeof(krb5_cred_info) * nppcreds))) == NULL) {
	retval = ENOMEM;
	goto cleanup_info;
    }
    memset(tmp, 0, (size_t) (sizeof(krb5_cred_info) * nppcreds));

    /*
     * For each credential in the list, initialize a cred info
     * structure and copy the ticket into the ticket list.
     */
    for (i = 0; i < nppcreds; i++) {
    	credenc.ticket_info[i] = (krb5_cred_info *)tmp + i;

        credenc.ticket_info[i]->magic = KV5M_CRED_INFO;
        credenc.ticket_info[i]->times = ppcreds[i]->times;
        credenc.ticket_info[i]->flags = ppcreds[i]->ticket_flags;

    	if (retval = decode_krb5_ticket(&ppcreds[i]->ticket, 
					&pcred->tickets[i]))
	    goto cleanup_info_ptrs;

	if (retval = krb5_copy_keyblock(context, &ppcreds[i]->keyblock,
                                        &credenc.ticket_info[i]->session)) 
            goto cleanup_info_ptrs;

        if (retval = krb5_copy_principal(context, ppcreds[i]->client,
                                         &credenc.ticket_info[i]->client))
            goto cleanup_info_ptrs;

      	if (retval = krb5_copy_principal(context, ppcreds[i]->server,
                                         &credenc.ticket_info[i]->server))
            goto cleanup_info_ptrs;

      	if (retval = krb5_copy_addresses(context, ppcreds[i]->addresses,
                                         &credenc.ticket_info[i]->caddrs))
            goto cleanup_info_ptrs;
    }

    /*
     * NULL terminate the lists.
     */
    credenc.ticket_info[i] = NULL;
    pcred->tickets[i] = NULL;

    retval = encrypt_credencpart(context, &credenc, keyblock, &pcred->enc_part);

cleanup_info_ptrs:
    free(tmp);

cleanup_info:
    free(credenc.ticket_info);
    return retval;
}

/*----------------------- krb5_mk_ncred -----------------------*/

/*
 * This functions takes as input an array of krb5_credentials, and
 * outputs an encoded KRB_CRED message suitable for krb5_rd_cred
 */
krb5_error_code INTERFACE
krb5_mk_ncred(context, auth_context, ppcreds, ppdata, outdata)

    krb5_context 	  context;
    krb5_auth_context	* auth_context;
    krb5_creds 	       ** ppcreds;
    krb5_data 	       ** ppdata;
    krb5_replay_data  	* outdata;
{
    krb5_error_code 	  retval;
    krb5_keyblock	* keyblock;
    krb5_replay_data      replaydata;
    krb5_cred 		* pcred;
    int			  ncred;

    if (ppcreds == NULL) {
    	return KRB5KRB_AP_ERR_BADADDR;
    }

    /*
     * Allocate memory for a NULL terminated list of tickets.
     */
    for (ncred = 0; ppcreds[ncred]; ncred++);

    if ((pcred = (krb5_cred *)malloc(sizeof(krb5_cred))) == NULL) 
        return ENOMEM;

    if ((pcred->tickets 
      = (krb5_ticket **)malloc(sizeof(krb5_ticket *) * (ncred + 1))) == NULL) {
	retval = ENOMEM;
	free(pcred);
    }

    /* Get keyblock */
    if ((keyblock = auth_context->local_subkey) == NULL) 
	if ((keyblock = auth_context->remote_subkey) == NULL) 
	    keyblock = auth_context->keyblock;

    /* Get replay info */
    if ((auth_context->auth_context_flags & KRB5_AUTH_CONTEXT_DO_TIME) &&
      (auth_context->rcache == NULL))
        return KRB5_RC_REQUIRED;

    if (((auth_context->auth_context_flags & KRB5_AUTH_CONTEXT_RET_TIME) ||
      (auth_context->auth_context_flags & KRB5_AUTH_CONTEXT_RET_SEQUENCE)) &&
      (outdata == NULL))
        /* Need a better error */
        return KRB5_RC_REQUIRED;

    if ((auth_context->auth_context_flags & KRB5_AUTH_CONTEXT_DO_TIME) ||
        (auth_context->auth_context_flags & KRB5_AUTH_CONTEXT_RET_TIME)) {
        if (retval = krb5_us_timeofday(context, &replaydata.timestamp,
                                       &replaydata.usec))
            return retval;
        if (auth_context->auth_context_flags & KRB5_AUTH_CONTEXT_RET_TIME) {
            outdata->timestamp = replaydata.timestamp;
            outdata->usec = replaydata.usec;
        }
        if (auth_context->auth_context_flags & KRB5_AUTH_CONTEXT_RET_TIME) {
            outdata->timestamp = replaydata.timestamp;
            outdata->usec = replaydata.usec;
        }
    }
    if ((auth_context->auth_context_flags & KRB5_AUTH_CONTEXT_DO_SEQUENCE) ||
        (auth_context->auth_context_flags & KRB5_AUTH_CONTEXT_RET_SEQUENCE)) {
        replaydata.seq = auth_context->local_seq_number;
        if (auth_context->auth_context_flags & KRB5_AUTH_CONTEXT_DO_SEQUENCE) {
            auth_context->local_seq_number++;
        } else {
            outdata->seq = replaydata.seq;
        }
    }

    /* Setup creds structure */
    if (retval = krb5_mk_ncred_basic(context, ppcreds, ncred, keyblock,
		     		     &replaydata, auth_context->local_addr, 
				     auth_context->remote_addr, pcred))
	goto cleanup_tickets;

    if (auth_context->auth_context_flags & KRB5_AUTH_CONTEXT_DO_TIME) {
        krb5_donot_replay replay;

        if (retval = krb5_gen_replay_name(context, auth_context->local_addr,
                                          "_forw", &replay.client)) 
            goto cleanup_tickets;

        replay.server = "";             /* XXX */
        replay.cusec = replaydata.usec;
        replay.ctime = replaydata.timestamp;
        if (retval = krb5_rc_store(context, auth_context->rcache, &replay)) {
            /* should we really error out here? XXX */
            krb5_xfree(replay.client);
            goto cleanup_tickets;
        }
        krb5_xfree(replay.client);
    }

    /* Encode creds structure */
    retval = encode_krb5_cred(pcred, ppdata);

cleanup_tickets:
    if (retval) {
	if ((auth_context->auth_context_flags & KRB5_AUTH_CONTEXT_DO_SEQUENCE) 
	 || (auth_context->auth_context_flags & KRB5_AUTH_CONTEXT_RET_SEQUENCE))
            auth_context->local_seq_number--;
	free(pcred->tickets);
	free(pcred);
    }
    return retval;
}

/*----------------------- krb5_mk_1cred -----------------------*/

/*
 * A convenience function that calls krb5_mk_ncred.
 */
krb5_error_code INTERFACE
krb5_mk_1cred(context, auth_context, pcreds, ppdata, outdata)
    krb5_context 	  context;
    krb5_auth_context	* auth_context;
    krb5_creds 		* pcreds;
    krb5_data 	       ** ppdata;
    krb5_replay_data  	* outdata;
{
    krb5_error_code retval;
    krb5_creds **ppcreds;

    if ((ppcreds = (krb5_creds **)malloc(sizeof(*ppcreds) * 2)) == NULL) {
	return ENOMEM;
    }

    ppcreds[0] = pcreds;
    ppcreds[1] = NULL;

    if (retval = krb5_mk_ncred(context, auth_context, ppcreds, ppdata, outdata))
	free(ppcreds);
    return retval;
}

