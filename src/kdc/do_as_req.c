/*
 * $Source$
 * $Author$
 *
 * Copyright 1990,1991 by the Massachusetts Institute of Technology.
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
 * KDC Routines to deal with AS_REQ's
 */

#if !defined(lint) && !defined(SABER)
static char rcsid_do_as_req_c[] =
"$Id$";
#endif	/* !lint & !SABER */

#include <krb5/krb5.h>
#include <krb5/kdb.h>
#include <krb5/preauth.h>
#include <krb5/los-proto.h>
#include <krb5/asn1.h>
#include <krb5/osconf.h>
#include <com_err.h>

#include <krb5/ext-proto.h>

#include <syslog.h>
#ifdef KRB5_USE_INET
#include <sys/types.h>
#include <netinet/in.h>
#ifndef hpux
#include <arpa/inet.h>
#endif	/* hpux */
#endif /* KRB5_USE_INET */

#include "kdc_util.h"
#include "policy.h"
#include "extern.h"

static krb5_error_code prepare_error_as PROTOTYPE((krb5_kdc_req *,
						   int,
						   krb5_data **));
/*
 * This routine is called to verify the preauthentication information
 * for a V5 request.  Client contains information about the principal
 * from the database. Padata contains pre-auth info received from
 * the network.
 *
 * Returns 0 if the pre-authentication is valid, non-zero to indicate
 * an error code of some sort.
 */

static krb5_error_code
check_padata (client, src_addr, padata, pa_id, flags)
    krb5_db_entry  *client;
    krb5_address **src_addr;
    krb5_pa_data **padata;
    int *pa_id;			/* Unique id which can be used for replay
				   of padata. */
    int *flags;
{
    krb5_encrypted_keyblock *enckey;
    krb5_keyblock tmpkey;
    krb5_error_code retval;
   
    enckey = &(client->key);
    /* 	Extract client key/alt_key from master key */
   
    retval = KDB_CONVERT_KEY_OUTOF_DB(enckey,&tmpkey);
    if (retval) {
	syslog( LOG_ERR, "AS_REQ: Unable to extract client key: %s",
	       error_message(retval));
	return retval;
    }
    retval =  krb5_verify_padata(*padata,client->principal,src_addr,
				 &tmpkey, pa_id, flags);
    memset((char *)tmpkey.contents, 0, tmpkey.length);
    krb5_xfree(tmpkey.contents);
    if (retval && client->alt_key.length) {
	/*
	 * If we failed, try again with the alternative key
	 */
	enckey = &(client->alt_key);
	/* Extract client key/alt_key from master key */
	if (retval = KDB_CONVERT_KEY_OUTOF_DB(enckey,&tmpkey)) {
	    syslog( LOG_ERR, "AS_REQ: Unable to extract client alt_key: %s",
		   error_message(retval));
	    return retval;
	}
	retval = krb5_verify_padata(*padata,client->principal,src_addr,
				    &tmpkey, pa_id, flags);
	memset((char *)tmpkey.contents, 0, tmpkey.length);
	krb5_xfree(tmpkey.contents);
    }
    return retval;
}

/*ARGSUSED*/
krb5_error_code
process_as_req(request, from, is_secondary, response)
register krb5_kdc_req *request;
const krb5_fulladdr *from;		/* who sent it ? */
int	is_secondary;
krb5_data **response;			/* filled in with a response packet */
{

    krb5_db_entry client, server;
    krb5_kdc_rep reply;
    krb5_enc_kdc_rep_part reply_encpart;
    krb5_ticket ticket_reply;
    krb5_enc_tkt_part enc_tkt_reply;
    krb5_error_code retval;
    int	errcode;
    int c_nprincs = 0, s_nprincs = 0;
    char cpw_service[255];
    int pwreq, pa_id, pa_flags;
    krb5_boolean more;
    krb5_timestamp kdc_time, authtime;
    krb5_keyblock *session_key = 0;
    krb5_keyblock encrypting_key;
    krb5_enctype useetype;
    krb5_pa_data *padat_tmp[2], padat_local;
    krb5_data salt_data;
    char *status;

    register int i;

    krb5_timestamp until, rtime;
    char *cname = 0, *sname = 0, *fromstring = 0;

    ticket_reply.enc_part.ciphertext.data = 0;
    salt_data.data = 0;

    if (!request->client)
	return(prepare_error_as(request, KDC_ERR_C_PRINCIPAL_UNKNOWN,
				response));
    if (retval = krb5_unparse_name(request->client, &cname)) {
	syslog(LOG_INFO, "AS_REQ: %s while unparsing client name",
	       error_message(retval));
	return(prepare_error_as(request, KDC_ERR_C_PRINCIPAL_UNKNOWN,
				response));
    }
    if (retval = krb5_unparse_name(request->server, &sname)) {
	free(cname);
	syslog(LOG_INFO, "AS_REQ: %s while unparsing server name",
	       error_message(retval));
	return(prepare_error_as(request, KDC_ERR_S_PRINCIPAL_UNKNOWN,
				response));
    }
#ifdef KRB5_USE_INET
    if (from->address->addrtype == ADDRTYPE_INET)
	fromstring = (char *) inet_ntoa(*(struct in_addr *)from->address->contents);
#endif
    if (!fromstring)
	fromstring = "<unknown>";

    /*
     * Special considerations are allowed when changing passwords. Is
     * this request for changepw?
     *
     * XXX This logic should be moved someplace else, perhaps the
     * site-specific policiy file....
     */
    pwreq = 0;
    sprintf(cpw_service, "%s@%s", "changepw/kerberos", 
	    krb5_princ_realm(request->server)->data);
    if (strcmp(sname, cpw_service) == 0) pwreq++;

    c_nprincs = 1;
    if (retval = krb5_db_get_principal(request->client, &client, &c_nprincs,
				       &more)) {
	c_nprincs = 0;
	goto errout;
    }
    if (more) {
	retval = prepare_error_as(request, KDC_ERR_PRINCIPAL_NOT_UNIQUE,
				  response);
	goto errout;
    } else if (c_nprincs != 1) {
#ifdef KRBCONF_VAGUE_ERRORS
	retval = prepare_error_as(request, KRB_ERR_GENERIC, response);
#else
	retval = prepare_error_as(request, KDC_ERR_C_PRINCIPAL_UNKNOWN,
				  response);
#endif
	goto errout;
    }
    
    s_nprincs = 1;
    if (retval = krb5_db_get_principal(request->server, &server, &s_nprincs,
				       &more)) {
	s_nprincs = 0;
	goto errout;
    }
    if (more) {
	retval = prepare_error_as(request, KDC_ERR_PRINCIPAL_NOT_UNIQUE,
				  response);
	goto errout;
    } else if (s_nprincs != 1) {
	retval = prepare_error_as(request, KDC_ERR_S_PRINCIPAL_UNKNOWN,
				  response);
	goto errout;
    }

    if (retval = krb5_timeofday(&kdc_time)) {
	syslog(LOG_INFO, "AS_REQ: TIME_OF_DAY: host %s, %s for %s", 
                  fromstring, cname, sname);
	goto errout;
    }

    status = "UNKNOWN REASON";
    if (retval = validate_as_request(request, client, server,
				     kdc_time, &status)) {
	syslog(LOG_INFO, "AS_REQ: %s: host %s, %s for %s", status,
                  fromstring, cname, sname);
	retval = prepare_error_as(request, retval, response);
	goto errout;
    }
      
    for (i = 0; i < request->netypes; i++)
	if (valid_etype(request->etype[i]))
	    break;
    if (i == request->netypes) {
	/* unsupported etype */
	    
	syslog(LOG_INFO, "AS_REQ: BAD ENCRYPTION TYPE: host %s, %s for %s",
                  fromstring, cname, sname);
	retval = prepare_error_as(request, KDC_ERR_ETYPE_NOSUPP, response);
	goto errout;
    }
    useetype = request->etype[i];

    if (retval = (*(krb5_csarray[useetype]->system->random_key))(krb5_csarray[useetype]->random_sequence, &session_key)) {
	/* random key failed */
	syslog(LOG_INFO, "AS_REQ: RANDOM KEY FAILED: host %s, %s for %s",
                  fromstring, cname, sname);
	goto errout;
    }

    ticket_reply.server = request->server;
    ticket_reply.enc_part.etype = useetype;
    ticket_reply.enc_part.kvno = server.kvno;

    enc_tkt_reply.flags = 0;
    setflag(enc_tkt_reply.flags, TKT_FLG_INITIAL);

    	/* It should be noted that local policy may affect the  */
        /* processing of any of these flags.  For example, some */
        /* realms may refuse to issue renewable tickets         */

    if (isflagset(request->kdc_options, KDC_OPT_FORWARDABLE))
	setflag(enc_tkt_reply.flags, TKT_FLG_FORWARDABLE);

    if (isflagset(request->kdc_options, KDC_OPT_PROXIABLE))
	    setflag(enc_tkt_reply.flags, TKT_FLG_PROXIABLE);

    if (isflagset(request->kdc_options, KDC_OPT_ALLOW_POSTDATE))
	    setflag(enc_tkt_reply.flags, TKT_FLG_MAY_POSTDATE);

    enc_tkt_reply.session = session_key;
    enc_tkt_reply.client = request->client;
    enc_tkt_reply.transited.tr_type = KRB5_DOMAIN_X500_COMPRESS;
    enc_tkt_reply.transited.tr_contents = empty_string; /* equivalent of "" */

    enc_tkt_reply.times.authtime = kdc_time;

    if (isflagset(request->kdc_options, KDC_OPT_POSTDATED)) {
	setflag(enc_tkt_reply.flags, TKT_FLG_POSTDATED);
	setflag(enc_tkt_reply.flags, TKT_FLG_INVALID);
	enc_tkt_reply.times.starttime = request->from;
    } else
	enc_tkt_reply.times.starttime = kdc_time;
    
    until = (request->till == 0) ? kdc_infinity : request->till;

    enc_tkt_reply.times.endtime =
	min(until,
	    min(enc_tkt_reply.times.starttime + client.max_life,
		min(enc_tkt_reply.times.starttime + server.max_life,
		    enc_tkt_reply.times.starttime + max_life_for_realm)));

    if (isflagset(request->kdc_options, KDC_OPT_RENEWABLE_OK) &&
	!isflagset(client.attributes, KRB5_KDB_DISALLOW_RENEWABLE) &&
	(enc_tkt_reply.times.endtime < request->till)) {

	/* we set the RENEWABLE option for later processing */

	setflag(request->kdc_options, KDC_OPT_RENEWABLE);
	request->rtime = request->till;
    }
    rtime = (request->rtime == 0) ? kdc_infinity : request->rtime;

    if (isflagset(request->kdc_options, KDC_OPT_RENEWABLE)) {
	/*
	 * XXX Should we squelch the output renew_till to be no
	 * earlier than the endtime of the ticket? 
	 */
	setflag(enc_tkt_reply.flags, TKT_FLG_RENEWABLE);
	enc_tkt_reply.times.renew_till =
	    min(rtime, enc_tkt_reply.times.starttime +
		       min(client.max_renewable_life,
			   min(server.max_renewable_life,
			       max_renewable_life_for_realm)));
    } else
	enc_tkt_reply.times.renew_till = 0; /* XXX */

    /* starttime is optional, and treated as authtime if not present.
       so we can nuke it if it matches */
    if (enc_tkt_reply.times.starttime == enc_tkt_reply.times.authtime)
	enc_tkt_reply.times.starttime = 0;

    enc_tkt_reply.caddrs = request->addresses;
    enc_tkt_reply.authorization_data = 0;

    /* 
     * Check the preauthentication if it is there.
     */
    if (request->padata) {
	retval = check_padata(&client,request->addresses,
			      request->padata, &pa_id, &pa_flags);
	if (retval) {
#ifdef KRBCONF_KDC_MODIFIES_KDB
	    /*
	     * Note: this doesn't work if you're using slave servers!!!
	     * It also causes the database to be modified (and thus
	     * need to be locked) frequently.
	     */
	    if (client.fail_auth_count < KRB5_MAX_FAIL_COUNT) {
		client.fail_auth_count = client.fail_auth_count + 1;
		if (client.fail_auth_count == KRB5_MAX_FAIL_COUNT) { 
		    client.attributes |= KRB5_KDB_DISALLOW_ALL_TIX;
		}
	    }
            krb5_db_put_principal(&client, &one);
#endif
            syslog(LOG_INFO, "AS_REQ: PREAUTH FAILED: host %s, %s for %s (%s)",
		   fromstring, cname, sname, error_message(retval));
#ifdef KRBCONF_VAGUE_ERRORS
            retval = prepare_error_as(request, KRB_ERR_GENERIC, response);
#else
	    retval -= ERROR_TABLE_BASE_krb5;
	    if ((retval < 0) || (retval > 127))
		    retval = KDC_PREAUTH_FAILED;
            retval = prepare_error_as(request, retval, response);
#endif
	    goto errout;
	} 

	setflag(enc_tkt_reply.flags, TKT_FLG_PRE_AUTH);
	/*
	 * If pa_type is one in which additional hardware authentication
	 * was performed set TKT_FLG_HW_AUTH too.
	 */
	if (pa_flags & KRB5_PREAUTH_FLAGS_HARDWARE)
            setflag(enc_tkt_reply.flags, TKT_FLG_HW_AUTH);
    }

    /*
     * Final check before handing out ticket: If the client requires
     * Hardware authentication, verify ticket flag is set
     */  

    if (isflagset(client.attributes, KRB5_KDB_REQUIRES_HW_AUTH) &&
	!isflagset(enc_tkt_reply.flags, TKT_FLG_HW_AUTH)) {

	  /* Of course their are always exceptions, in this case if the
	     service requested is for changing of the key (password), then
	     if TKT_FLG_PRE_AUTH is set allow it. */
	
	  if (!pwreq || !(enc_tkt_reply.flags & TKT_FLG_PRE_AUTH)){
              syslog(LOG_INFO, "AS_REQ: Needed HW preauth: host %s, %s for %s",
		     fromstring, cname, sname);
              retval = prepare_error_as(request, KRB_ERR_GENERIC, response);
	      goto errout;
	  }
      }

    ticket_reply.enc_part2 = &enc_tkt_reply;

    /* convert server.key into a real key (it may be encrypted
       in the database) */
    if (retval = KDB_CONVERT_KEY_OUTOF_DB(&server.key, &encrypting_key))
	goto errout;
    retval = krb5_encrypt_tkt_part(&encrypting_key, &ticket_reply);
    memset((char *)encrypting_key.contents, 0, encrypting_key.length);
    krb5_xfree(encrypting_key.contents);
    if (retval)
	goto errout;

    /* Start assembling the response */
    reply.msg_type = KRB5_AS_REP;

    reply.padata = 0;
    if (client.salt_type != KRB5_KDB_SALTTYPE_NORMAL) {
        padat_tmp[0] = &padat_local;
        padat_tmp[1] = 0;
 
        padat_tmp[0]->pa_type = KRB5_PADATA_PW_SALT;

	/* WARNING: sharing substructure here, but it's not a real problem,
	   since nothing below will "pull out the rug" */

	switch (client.salt_type) {
	    krb5_data *data_foo;
	case KRB5_KDB_SALTTYPE_V4:
	    /* send an empty (V4) salt */
	    padat_tmp[0]->contents = 0;
	    padat_tmp[0]->length = 0;
	    break;
	case KRB5_KDB_SALTTYPE_NOREALM:
	    if (retval = krb5_principal2salt_norealm(request->client,
						     &salt_data))
		goto errout;
	    padat_tmp[0]->length = salt_data.length;
	    padat_tmp[0]->contents = (krb5_octet *)salt_data.data;
	    break;
	case KRB5_KDB_SALTTYPE_ONLYREALM:
	    data_foo = krb5_princ_realm(request->client);
	    padat_tmp[0]->length = data_foo->length;
	    padat_tmp[0]->contents = (krb5_octet *)data_foo->data;
	    break;
	case KRB5_KDB_SALTTYPE_SPECIAL:
	    padat_tmp[0]->length = client.salt_length;
	    padat_tmp[0]->contents = client.salt;
	    break;
	}
	reply.padata = padat_tmp;
    }

    reply.client = request->client;
    /* XXX need separate etypes for ticket encryption and kdc_rep encryption */
    reply.enc_part.etype = useetype;
    reply.enc_part.kvno = client.kvno;
    reply.ticket = &ticket_reply;

    reply_encpart.session = session_key;
    if (retval = fetch_last_req_info(&client, &reply_encpart.last_req))
	goto errout;

    reply_encpart.nonce = request->nonce;
    reply_encpart.key_exp = client.expiration;
    reply_encpart.flags = enc_tkt_reply.flags;
    reply_encpart.server = ticket_reply.server;

    /* copy the time fields EXCEPT for authtime; it's location
       is used for ktime */
    reply_encpart.times = enc_tkt_reply.times;
    reply_encpart.times.authtime = authtime = kdc_time;

    reply_encpart.caddrs = enc_tkt_reply.caddrs;

    /* now encode/encrypt the response */

    /* convert client.key into a real key (it may be encrypted
       in the database) */
    if (retval = KDB_CONVERT_KEY_OUTOF_DB(&client.key, &encrypting_key))
	goto errout;

    retval = krb5_encode_kdc_rep(KRB5_AS_REP, &reply_encpart,
				 &encrypting_key,  &reply, response);
    memset((char *)encrypting_key.contents, 0, encrypting_key.length);
    krb5_xfree(encrypting_key.contents);

    if (retval) {
	syslog(LOG_INFO, "AS_REQ: ENCODE_KDC_REP: host %s, %s for %s (%s)",
	       fromstring, cname, sname, error_message(retval));
	goto errout;
    }
    
    /* these parts are left on as a courtesy from krb5_encode_kdc_rep so we
       can use them in raw form if needed.  But, we don't... */
    memset(reply.enc_part.ciphertext.data, 0,
	   reply.enc_part.ciphertext.length);
    free(reply.enc_part.ciphertext.data);

    if (is_secondary)
	syslog(LOG_INFO, "AS_REQ; ISSUE: authtime %d, host %s, %s for %s",
	       authtime, fromstring, cname, sname);
    else
	syslog(LOG_INFO, "AS_REQ: ISSUE: authtime %d, host %s, %s for %s",
	       authtime, fromstring, cname, sname);

errout:
    if (cname)
	    free(cname);
    if (sname)
	    free(sname);
    if (c_nprincs)
	krb5_db_free_principal(&client, c_nprincs);
    if (s_nprincs)
	krb5_db_free_principal(&server, s_nprincs);
    if (session_key)
	krb5_free_keyblock(session_key);
    if (ticket_reply.enc_part.ciphertext.data) {
	memset(ticket_reply.enc_part.ciphertext.data , 0,
	       ticket_reply.enc_part.ciphertext.length);
	free(ticket_reply.enc_part.ciphertext.data);
    }
    if (salt_data.data)
	krb5_xfree(salt_data.data);
    
    return retval;
}

static krb5_error_code
prepare_error_as (request, error, response)
register krb5_kdc_req *request;
int error;
krb5_data **response;
{
    krb5_error errpkt;
    krb5_error_code retval;
    krb5_data *scratch;
    char *cname = 0, *sname = 0;

    if (retval = krb5_unparse_name(request->client, &cname))
       syslog(LOG_INFO, "AS_REQ: %s while unparsing client name for error",
              error_message(retval));
    if (retval = krb5_unparse_name(request->server, &sname))
       syslog(LOG_INFO, "AS_REQ: %s while unparsing server name for error",
              error_message(retval));

    syslog(LOG_INFO, "AS_REQ: %s while processing request from %s for %s",
	   error_message(error+KRB5KDC_ERR_NONE),
	   cname ? cname : "UNKNOWN CLIENT", sname ? sname : "UNKNOWN SERVER");

    if (cname)
	    free(cname);
    if (sname)
	    free(sname);

    errpkt.ctime = request->nonce;
    errpkt.cusec = 0;

    if (retval = krb5_us_timeofday(&errpkt.stime, &errpkt.susec))
	return(retval);
    errpkt.error = error;
    errpkt.server = request->server;
    errpkt.client = request->client;
    errpkt.text.length = strlen(error_message(error+KRB5KDC_ERR_NONE))+1;
    if (!(errpkt.text.data = malloc(errpkt.text.length)))
	return ENOMEM;
    (void) strcpy(errpkt.text.data, error_message(error+KRB5KDC_ERR_NONE));

    if (!(scratch = (krb5_data *)malloc(sizeof(*scratch)))) {
	free(errpkt.text.data);
	return ENOMEM;
    }
    errpkt.e_data.length = 0;
    errpkt.e_data.data = 0;

    retval = krb5_mk_error(&errpkt, scratch);
    free(errpkt.text.data);
    *response = scratch;
    return retval;
}
