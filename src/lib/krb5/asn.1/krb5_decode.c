/*
 * src/lib/krb5/asn.1/krb5_decode.c
 * 
 * Copyright 1994 by the Massachusetts Institute of Technology.
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
 */

#include "krb5_decode.h"
#include "krbasn1.h"
#include "asn1_decode_k.h"
#include "asn1_decode.h"
#include "asn1_get.h"

/* setup *********************************************************/
/* set up variables */
#define setup()\
asn1_error_code retval;\
asn1buf buf;\
asn1_class class;\
asn1_construction construction;\
asn1_tagnum tagnum;\
int length;\
\
retval = asn1buf_wrap_data(&buf,code);\
if(retval) return retval

#define setup_no_length()\
asn1_error_code retval;\
asn1buf buf;\
asn1_class class;\
asn1_construction construction;\
asn1_tagnum tagnum;\
\
retval = asn1buf_wrap_data(&buf,code);\
if(retval) return retval

#define setup_no_tagnum()\
asn1_error_code retval;\
asn1buf buf;\
asn1_class class;\
asn1_construction construction;\
\
retval = asn1buf_wrap_data(&buf,code);\
if(retval) return retval

#define alloc_field(var,type)\
var = (type*)calloc(1,sizeof(type));\
if((var) == NULL) return ENOMEM

#define setup_buf_only()\
asn1_error_code retval;\
asn1buf buf;\
retval = asn1buf_wrap_data(&buf,code);\
if(retval) return retval


/* process encoding header ***************************************/
/* decode tag and check that it == [APPLICATION tagnum] */
#define check_apptag(tagexpect)\
retval = asn1_get_tag(&buf,&class,&construction,&tagnum,NULL);\
if(retval) return retval;\
if(class != APPLICATION || construction != CONSTRUCTED) return ASN1_BAD_ID;\
if(tagnum != (tagexpect)) return KRB5_BADMSGTYPE



/* process a structure *******************************************/

/* decode an explicit tag and place the number in tagnum */
#define next_tag()\
retval = asn1_get_tag(&subbuf,&class,&construction,&tagnum,NULL);\
if(retval) return retval;\
if(class != CONTEXT_SPECIFIC || construction != CONSTRUCTED)\
  return ASN1_BAD_ID

/* decode sequence header and initialize tagnum with the first field */
#define begin_structure()\
asn1buf subbuf;\
retval = asn1_get_tag(&buf,&class,&construction,&tagnum,&length);\
if(retval) return retval;\
if(class != UNIVERSAL || construction != CONSTRUCTED ||\
   tagnum != ASN1_SEQUENCE) return ASN1_BAD_ID;\
retval = asn1buf_imbed(&subbuf,&buf,length);\
if(retval) return retval;\
next_tag()

#define end_structure()\
asn1buf_sync(&buf,&subbuf)

/* process fields *******************************************/
/* normal fields ************************/
#define get_field_body(var,decoder)\
retval = decoder(&subbuf,&(var));\
if(retval) return (krb5_error_code)retval;\
next_tag()

/* decode a field (<[UNIVERSAL id]> <length> <contents>)
    check that the id number == tagexpect then
    decode into var
    get the next tag */
#define get_field(var,tagexpect,decoder)\
if(tagnum > (tagexpect)) return ASN1_MISSING_FIELD;\
if(tagnum < (tagexpect)) return ASN1_MISPLACED_FIELD;\
get_field_body(var,decoder)

/* decode (or skip, if not present) an optional field */
#define opt_field(var,tagexpect,decoder)\
if(tagnum == (tagexpect)){ get_field_body(var,decoder); }

/* field w/ accompanying length *********/
#define get_lenfield_body(len,var,decoder)\
retval = decoder(&subbuf,&(len),&(var));\
if(retval) return (krb5_error_code)retval;\
next_tag()

/* decode a field w/ its length (for string types) */
#define get_lenfield(len,var,tagexpect,decoder)\
if(tagnum > (tagexpect)) return ASN1_MISSING_FIELD;\
if(tagnum < (tagexpect)) return ASN1_MISPLACED_FIELD;\
get_lenfield_body(len,var,decoder)

/* decode an optional field w/ length */
#define opt_lenfield(len,var,tagexpect,decoder)\
if(tagnum == (tagexpect)){\
  get_lenfield_body(len,var,decoder);\
}


/* clean up ******************************************************/
/* finish up */
#define cleanup()\
return 0

krb5_error_code decode_krb5_authenticator(DECLARG(const krb5_data *, code),
					  DECLARG(krb5_authenticator **, rep))
     OLDDECLARG(const krb5_data *, code)
     OLDDECLARG(krb5_authenticator **, rep)
{
  setup();
  alloc_field(*rep,krb5_authenticator);

  check_apptag(2);
  { begin_structure();
    { krb5_kvno kvno;
      get_field(kvno,0,asn1_decode_kvno);
      if(kvno != KVNO) return KRB5KDC_ERR_BAD_PVNO; }
    alloc_field((*rep)->client,krb5_principal_data);
    get_field((*rep)->client,1,asn1_decode_realm);
    get_field((*rep)->client,2,asn1_decode_principal_name);
    if(tagnum == 3){
      alloc_field((*rep)->checksum,krb5_checksum);
      get_field(*((*rep)->checksum),3,asn1_decode_checksum); }
    get_field((*rep)->cusec,4,asn1_decode_int32);
    get_field((*rep)->ctime,5,asn1_decode_kerberos_time);
    if(tagnum == 6){ alloc_field((*rep)->subkey,krb5_keyblock); }
    opt_field(*((*rep)->subkey),6,asn1_decode_encryption_key);
    opt_field((*rep)->seq_number,7,asn1_decode_int32);
    opt_field((*rep)->authorization_data,8,asn1_decode_authorization_data);
    end_structure();
  }
  cleanup();
}

krb5_error_code decode_krb5_ticket(DECLARG(const krb5_data *, code),
				   DECLARG(krb5_ticket **, rep))
     OLDDECLARG(const krb5_data *, code)
     OLDDECLARG(krb5_ticket **, rep)
{
  setup();
  alloc_field(*rep,krb5_ticket);
  
  check_apptag(1);
  { begin_structure();
    { krb5_kvno kvno;
      get_field(kvno,0,asn1_decode_kvno);
      if(kvno != KVNO) return KRB5KDC_ERR_BAD_PVNO;
    }
    alloc_field((*rep)->server,krb5_principal_data);
    get_field((*rep)->server,1,asn1_decode_realm);
    get_field((*rep)->server,2,asn1_decode_principal_name);
    get_field((*rep)->enc_part,3,asn1_decode_encrypted_data);
    end_structure();
  }
  cleanup();
}

krb5_error_code decode_krb5_encryption_key(DECLARG(const krb5_data *, code),
					   DECLARG(krb5_keyblock **, rep))
     OLDDECLARG(const krb5_data *, code)
     OLDDECLARG(krb5_keyblock **, rep)
{
  setup();
  alloc_field(*rep,krb5_keyblock);

  { begin_structure();
    get_field((*rep)->keytype,0,asn1_decode_keytype);
    get_lenfield((*rep)->length,(*rep)->contents,1,asn1_decode_octetstring);
    end_structure();
  }
  cleanup();
}

krb5_error_code decode_krb5_enc_tkt_part(DECLARG(const krb5_data *, code),
					 DECLARG(krb5_enc_tkt_part **, rep))
     OLDDECLARG(const krb5_data *, code)
     OLDDECLARG(krb5_enc_tkt_part **, rep)
{
  setup();
  alloc_field(*rep,krb5_enc_tkt_part);

  check_apptag(3);
  { begin_structure();
    get_field((*rep)->flags,0,asn1_decode_ticket_flags);
    alloc_field((*rep)->session,krb5_keyblock);
    get_field(*((*rep)->session),1,asn1_decode_encryption_key);
    alloc_field((*rep)->client,krb5_principal_data);
    get_field((*rep)->client,2,asn1_decode_realm);
    get_field((*rep)->client,3,asn1_decode_principal_name);
    get_field((*rep)->transited,4,asn1_decode_transited_encoding);
    get_field((*rep)->times.authtime,5,asn1_decode_kerberos_time);
    opt_field((*rep)->times.starttime,6,asn1_decode_kerberos_time);
    get_field((*rep)->times.endtime,7,asn1_decode_kerberos_time);
    opt_field((*rep)->times.renew_till,8,asn1_decode_kerberos_time);
    opt_field((*rep)->caddrs,9,asn1_decode_host_addresses);
    opt_field((*rep)->authorization_data,10,asn1_decode_authorization_data);
    end_structure();
  }
  cleanup();
}

krb5_error_code decode_krb5_enc_kdc_rep_part(DECLARG(const krb5_data *, code),
					     DECLARG(krb5_enc_kdc_rep_part **, rep))
     OLDDECLARG(const krb5_data *, code)
     OLDDECLARG(krb5_enc_kdc_rep_part **, rep)
{
  setup_no_length();
  alloc_field(*rep,krb5_enc_kdc_rep_part);

#ifndef ENCKRB5KDCREPPART_HAS_MSGTYPE
  check_apptag(26);
#else
  retval = asn1_get_tag(&buf,&class,&construction,&tagnum,NULL);
  if(retval) return retval;
  if(class != APPLICATION || construction != CONSTRUCTED) return ASN1_BAD_ID;
  if(tagnum == 25) (*rep)->msg_type = KRB5_AS_REP;
  else if(tagnum == 26) (*rep)->msg_type = KRB5_TGS_REP;
  else return KRB5_BADMSGTYPE;
#endif
  retval = asn1_decode_enc_kdc_rep_part(&buf,*rep);
  if(retval) return (krb5_error_code)retval;

  cleanup();
}

krb5_error_code decode_krb5_as_rep(DECLARG(const krb5_data *, code),
				   DECLARG(krb5_kdc_rep **, rep))
     OLDDECLARG(const krb5_data *, code)
     OLDDECLARG(krb5_kdc_rep **, rep)
{
  setup_no_length();
  alloc_field(*rep,krb5_kdc_rep);

  check_apptag(11);
  retval = asn1_decode_kdc_rep(&buf,*rep);
  if(retval) return (krb5_error_code)retval;
  if((*rep)->msg_type != KRB5_AS_REP) return KRB5_BADMSGTYPE;

  cleanup();
}

krb5_error_code decode_krb5_tgs_rep(DECLARG(const krb5_data *, code),
				    DECLARG(krb5_kdc_rep **, rep))
     OLDDECLARG(const krb5_data *, code)
     OLDDECLARG(krb5_kdc_rep **, rep)
{
  setup_no_length();
  alloc_field(*rep,krb5_kdc_rep);

  check_apptag(13);
  retval = asn1_decode_kdc_rep(&buf,*rep);
  if(retval) return (krb5_error_code)retval;
  if((*rep)->msg_type != KRB5_TGS_REP) return KRB5_BADMSGTYPE;

  cleanup();
}

krb5_error_code decode_krb5_ap_req(DECLARG(const krb5_data *, code),
				   DECLARG(krb5_ap_req **, rep))
     OLDDECLARG(const krb5_data *, code)
     OLDDECLARG(krb5_ap_req **, rep)
{
  setup();
  alloc_field(*rep,krb5_ap_req);

  check_apptag(14);
  { begin_structure();
    { krb5_kvno kvno;
      get_field(kvno,0,asn1_decode_kvno);
      if(kvno != KVNO) return KRB5KDC_ERR_BAD_PVNO; }
    { krb5_msgtype msg_type;
      get_field(msg_type,1,asn1_decode_msgtype);
      if(msg_type != KRB5_AP_REQ) return KRB5_BADMSGTYPE; }
    get_field((*rep)->ap_options,2,asn1_decode_ap_options);
    alloc_field((*rep)->ticket,krb5_ticket);
    get_field(*((*rep)->ticket),3,asn1_decode_ticket);
    get_field((*rep)->authenticator,4,asn1_decode_encrypted_data);
    end_structure();
  }
  cleanup();
}

krb5_error_code decode_krb5_ap_rep(DECLARG(const krb5_data *, code),
				   DECLARG(krb5_ap_rep **, rep))
     OLDDECLARG(const krb5_data *, code)
     OLDDECLARG(krb5_ap_rep **, rep)
{
  setup();
  alloc_field(*rep,krb5_ap_rep);

  check_apptag(15);
  { begin_structure();
    { krb5_kvno kvno;
      get_field(kvno,0,asn1_decode_kvno);
      if(kvno != KVNO) return KRB5KDC_ERR_BAD_PVNO; }
    { krb5_msgtype msg_type;
      get_field(msg_type,1,asn1_decode_msgtype);
      if(msg_type != KRB5_AP_REP) return KRB5_BADMSGTYPE; }
    get_field((*rep)->enc_part,2,asn1_decode_encrypted_data);
    end_structure();
  }
  cleanup();
}

krb5_error_code decode_krb5_ap_rep_enc_part(DECLARG(const krb5_data *, code),
					    DECLARG(krb5_ap_rep_enc_part **, rep))
     OLDDECLARG(const krb5_data *, code)
     OLDDECLARG(krb5_ap_rep_enc_part **, rep)
{
  setup();
  alloc_field(*rep,krb5_ap_rep_enc_part);

  check_apptag(27);
  { begin_structure();
    get_field((*rep)->ctime,0,asn1_decode_kerberos_time);
    get_field((*rep)->cusec,1,asn1_decode_int32);
    if(tagnum == 2){ alloc_field((*rep)->subkey,krb5_keyblock); }
    opt_field(*((*rep)->subkey),2,asn1_decode_encryption_key);
    opt_field((*rep)->seq_number,3,asn1_decode_int32);
    end_structure();
  }
  cleanup();
}

krb5_error_code decode_krb5_as_req(DECLARG(const krb5_data *, code),
				   DECLARG(krb5_kdc_req **, rep))
     OLDDECLARG(const krb5_data *, code)
     OLDDECLARG(krb5_kdc_req **, rep)
{
  setup_no_length();
  alloc_field(*rep,krb5_kdc_req);

  check_apptag(10);
  retval = asn1_decode_kdc_req(&buf,*rep);
  if(retval) return (krb5_error_code)retval;
  if((*rep)->msg_type != KRB5_AS_REQ) return KRB5_BADMSGTYPE;

  cleanup();
}

krb5_error_code decode_krb5_tgs_req(DECLARG(const krb5_data *, code),
				    DECLARG(krb5_kdc_req **, rep))
     OLDDECLARG(const krb5_data *, code)
     OLDDECLARG(krb5_kdc_req **, rep)
{
  setup_no_length();
  alloc_field(*rep,krb5_kdc_req);

  check_apptag(12);
  retval = asn1_decode_kdc_req(&buf,*rep);
  if(retval) return (krb5_error_code)retval;
  if((*rep)->msg_type != KRB5_TGS_REQ) return KRB5_BADMSGTYPE;

  cleanup();
}

krb5_error_code decode_krb5_kdc_req_body(DECLARG(const krb5_data *, code),
					 DECLARG(krb5_kdc_req **, rep))
     OLDDECLARG(const krb5_data *, code)
     OLDDECLARG(krb5_kdc_req **, rep)
{
  setup_buf_only();
  alloc_field(*rep,krb5_kdc_req);

  retval = asn1_decode_kdc_req_body(&buf,*rep);
  if(retval) return (krb5_error_code)retval;

  cleanup();
}

krb5_error_code decode_krb5_safe(DECLARG(const krb5_data *, code),
				 DECLARG(krb5_safe **, rep))
     OLDDECLARG(const krb5_data *, code)
     OLDDECLARG(krb5_safe **, rep)
{
  setup();
  alloc_field(*rep,krb5_safe);

  check_apptag(20);
  { begin_structure();
    { krb5_kvno kvno;
      get_field(kvno,0,asn1_decode_kvno);
      if(kvno != KVNO) return KRB5KDC_ERR_BAD_PVNO; }
    { krb5_msgtype msg_type;
      get_field(msg_type,1,asn1_decode_msgtype);
      if(msg_type != KRB5_SAFE) return KRB5_BADMSGTYPE; }
    get_field(**rep,2,asn1_decode_krb_safe_body);
    alloc_field((*rep)->checksum,krb5_checksum);
    get_field(*((*rep)->checksum),3,asn1_decode_checksum);
    end_structure();
  }
  cleanup();
}

krb5_error_code decode_krb5_priv(DECLARG(const krb5_data *, code),
				 DECLARG(krb5_priv **, rep))
     OLDDECLARG(const krb5_data *, code)
     OLDDECLARG(krb5_priv **, rep)
{
  setup();
  alloc_field(*rep,krb5_priv);

  check_apptag(21);
  { begin_structure();
    { krb5_kvno kvno;
      get_field(kvno,0,asn1_decode_kvno);
      if(kvno != KVNO) return KRB5KDC_ERR_BAD_PVNO; }
    { krb5_msgtype msg_type;
      get_field(msg_type,1,asn1_decode_msgtype);
      if(msg_type != KRB5_PRIV) return KRB5_BADMSGTYPE; }
    get_field((*rep)->enc_part,3,asn1_decode_encrypted_data);
    end_structure();
  }
  cleanup();
}

krb5_error_code decode_krb5_enc_priv_part(DECLARG(const krb5_data *, code),
					  DECLARG(krb5_priv_enc_part **, rep))
     OLDDECLARG(const krb5_data *, code)
     OLDDECLARG(krb5_priv_enc_part **, rep)
{
  setup();
  alloc_field(*rep,krb5_priv_enc_part);

  check_apptag(28);
  { begin_structure();
    get_lenfield((*rep)->user_data.length,(*rep)->user_data.data,0,asn1_decode_charstring);
    opt_field((*rep)->timestamp,1,asn1_decode_kerberos_time);
    opt_field((*rep)->usec,2,asn1_decode_int32);
    opt_field((*rep)->seq_number,3,asn1_decode_int32);
    alloc_field((*rep)->s_address,krb5_address);
    get_field(*((*rep)->s_address),4,asn1_decode_host_address);
    if(tagnum == 5){ alloc_field((*rep)->r_address,krb5_address); }
    opt_field(*((*rep)->r_address),5,asn1_decode_host_address);
    end_structure();
  }
  cleanup();
}

krb5_error_code decode_krb5_cred(DECLARG(const krb5_data *, code),
				 DECLARG(krb5_cred **, rep))
     OLDDECLARG(const krb5_data *, code)
     OLDDECLARG(krb5_cred **, rep)
{
  setup();
  alloc_field(*rep,krb5_cred);

  check_apptag(22);
  { begin_structure();
    { krb5_kvno kvno;
      get_field(kvno,0,asn1_decode_kvno);
      if(kvno != KVNO) return KRB5KDC_ERR_BAD_PVNO; }
    { krb5_msgtype msg_type;
      get_field(msg_type,1,asn1_decode_msgtype);
      if(msg_type != KRB5_CRED) return KRB5_BADMSGTYPE; }
    get_field((*rep)->tickets,2,asn1_decode_sequence_of_ticket);
    get_field((*rep)->enc_part,3,asn1_decode_encrypted_data);
    end_structure();
  }
  cleanup();
}

krb5_error_code decode_krb5_enc_cred_part(DECLARG(const krb5_data *, code),
					  DECLARG(krb5_cred_enc_part **, rep))
     OLDDECLARG(const krb5_data *, code)
     OLDDECLARG(krb5_cred_enc_part **, rep)
{
  setup();
  alloc_field(*rep,krb5_cred_enc_part);

  check_apptag(29);
  { begin_structure();
    get_field((*rep)->ticket_info,0,asn1_decode_sequence_of_krb_cred_info);
    opt_field((*rep)->nonce,1,asn1_decode_int32);
    opt_field((*rep)->timestamp,2,asn1_decode_kerberos_time);
    opt_field((*rep)->usec,3,asn1_decode_int32);
    if(tagnum == 4){ alloc_field((*rep)->s_address,krb5_address); }
    opt_field(*((*rep)->s_address),4,asn1_decode_host_address);
    if(tagnum == 5){ alloc_field((*rep)->r_address,krb5_address); }
    opt_field(*((*rep)->r_address),5,asn1_decode_host_address);
    end_structure();
  }
  cleanup();
}

krb5_error_code decode_krb5_error(DECLARG(const krb5_data *, code),
				  DECLARG(krb5_error **, rep))
     OLDDECLARG(const krb5_data *, code)
     OLDDECLARG(krb5_error **, rep)
{
  setup();
  alloc_field(*rep,krb5_error);
  
  check_apptag(30);
  { begin_structure();
    { krb5_kvno kvno;
      get_field(kvno,0,asn1_decode_kvno);
      if(kvno != KVNO) return KRB5KDC_ERR_BAD_PVNO; }
    { krb5_msgtype msg_type;
      get_field(msg_type,1,asn1_decode_msgtype);
      if(msg_type != KRB5_ERROR) return KRB5_BADMSGTYPE; }
    opt_field((*rep)->ctime,2,asn1_decode_kerberos_time);
    opt_field((*rep)->cusec,3,asn1_decode_int32);
    get_field((*rep)->stime,4,asn1_decode_kerberos_time);
    get_field((*rep)->susec,5,asn1_decode_int32);
    get_field((*rep)->error,6,asn1_decode_ui_4);
    if(tagnum == 7){ alloc_field((*rep)->client,krb5_principal_data); }
    opt_field((*rep)->client,7,asn1_decode_realm);
    opt_field((*rep)->client,8,asn1_decode_principal_name);
    alloc_field((*rep)->server,krb5_principal_data);
    get_field((*rep)->server,9,asn1_decode_realm);
    get_field((*rep)->server,10,asn1_decode_principal_name);
    opt_lenfield((*rep)->text.length,(*rep)->text.data,11,asn1_decode_generalstring);
    opt_lenfield((*rep)->e_data.length,(*rep)->e_data.data,12,asn1_decode_charstring);
    end_structure();
  }
  cleanup();
}

krb5_error_code decode_krb5_authdata(DECLARG(const krb5_data *, code),
				     DECLARG(krb5_authdata ***, rep))
     OLDDECLARG(const krb5_data *, code)
     OLDDECLARG(krb5_authdata ***, rep)
{
  setup_buf_only();
  retval = asn1_decode_authorization_data(&buf,rep);
  if(retval) return (krb5_error_code)retval;
  cleanup();
}

krb5_error_code decode_krb5_pwd_sequence(DECLARG(const krb5_data *, code),
					 DECLARG(passwd_phrase_element **, rep))
     OLDDECLARG(const krb5_data *, code)
     OLDDECLARG(passwd_phrase_element **, rep)
{
  setup_buf_only();
  alloc_field(*rep,passwd_phrase_element);
  retval = asn1_decode_passwdsequence(&buf,*rep);
  if(retval) return retval;
  cleanup();
}

krb5_error_code decode_krb5_pwd_data(DECLARG(const krb5_data *, code),
				     DECLARG(krb5_pwd_data **, rep))
     OLDDECLARG(const krb5_data *, code)
     OLDDECLARG(krb5_pwd_data **, rep)
{
  setup();
  alloc_field(*rep,krb5_pwd_data);
  { begin_structure();
    get_field((*rep)->sequence_count,0,asn1_decode_int);
    get_field((*rep)->element,1,asn1_decode_sequence_of_passwdsequence);
    end_structure (); }
  cleanup();
}
