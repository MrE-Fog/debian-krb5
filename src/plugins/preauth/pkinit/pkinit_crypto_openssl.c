/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 * COPYRIGHT (C) 2006,2007
 * THE REGENTS OF THE UNIVERSITY OF MICHIGAN
 * ALL RIGHTS RESERVED
 *
 * Permission is granted to use, copy, create derivative works
 * and redistribute this software and such derivative works
 * for any purpose, so long as the name of The University of
 * Michigan is not used in any advertising or publicity
 * pertaining to the use of distribution of this software
 * without specific, written prior authorization.  If the
 * above copyright notice or any other identification of the
 * University of Michigan is included in any copy of any
 * portion of this software, then the disclaimer below must
 * also be included.
 *
 * THIS SOFTWARE IS PROVIDED AS IS, WITHOUT REPRESENTATION
 * FROM THE UNIVERSITY OF MICHIGAN AS TO ITS FITNESS FOR ANY
 * PURPOSE, AND WITHOUT WARRANTY BY THE UNIVERSITY OF
 * MICHIGAN OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING
 * WITHOUT LIMITATION THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. THE
 * REGENTS OF THE UNIVERSITY OF MICHIGAN SHALL NOT BE LIABLE
 * FOR ANY DAMAGES, INCLUDING SPECIAL, INDIRECT, INCIDENTAL, OR
 * CONSEQUENTIAL DAMAGES, WITH RESPECT TO ANY CLAIM ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OF THE SOFTWARE, EVEN
 * IF IT HAS BEEN OR IS HEREAFTER ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGES.
 */

#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <unistd.h>
#include <dirent.h>
#include <arpa/inet.h>

#include "k5-platform.h"

#include "pkinit_crypto_openssl.h"

static void openssl_init(void);

static krb5_error_code pkinit_init_pkinit_oids(pkinit_plg_crypto_context );
static void pkinit_fini_pkinit_oids(pkinit_plg_crypto_context );

static krb5_error_code pkinit_init_dh_params(pkinit_plg_crypto_context );
static void pkinit_fini_dh_params(pkinit_plg_crypto_context );

static krb5_error_code pkinit_init_certs(pkinit_identity_crypto_context ctx);
static void pkinit_fini_certs(pkinit_identity_crypto_context ctx);

static krb5_error_code pkinit_init_pkcs11(pkinit_identity_crypto_context ctx);
static void pkinit_fini_pkcs11(pkinit_identity_crypto_context ctx);

static krb5_error_code pkinit_encode_dh_params
(BIGNUM *, BIGNUM *, BIGNUM *, unsigned char **, unsigned int *);
static DH *pkinit_decode_dh_params
(DH **, unsigned char **, unsigned int );
static int pkinit_check_dh_params
(BIGNUM * p1, BIGNUM * p2, BIGNUM * g1, BIGNUM * q1);

static krb5_error_code pkinit_sign_data
(krb5_context context, pkinit_identity_crypto_context cryptoctx,
 unsigned char *data, unsigned int data_len,
 unsigned char **sig, unsigned int *sig_len);

static krb5_error_code create_signature
(unsigned char **, unsigned int *, unsigned char *, unsigned int,
 EVP_PKEY *pkey);

static krb5_error_code pkinit_decode_data
(krb5_context context, pkinit_identity_crypto_context cryptoctx,
 unsigned char *data, unsigned int data_len,
 unsigned char **decoded, unsigned int *decoded_len);

static krb5_error_code decode_data
(unsigned char **, unsigned int *, unsigned char *, unsigned int,
 EVP_PKEY *pkey, X509 *cert);

#ifdef DEBUG_DH
static void print_dh(DH *, char *);
static void print_pubkey(BIGNUM *, char *);
#endif

static int prepare_enc_data
(unsigned char *indata, int indata_len, unsigned char **outdata,
 int *outdata_len);

static int openssl_callback (int, X509_STORE_CTX *);
static int openssl_callback_ignore_crls (int, X509_STORE_CTX *);

static int pkcs7_decrypt
(krb5_context context, pkinit_identity_crypto_context id_cryptoctx,
 PKCS7 *p7, BIO *bio);

static BIO * pkcs7_dataDecode
(krb5_context context, pkinit_identity_crypto_context id_cryptoctx,
 PKCS7 *p7);

static ASN1_OBJECT * pkinit_pkcs7type2oid
(pkinit_plg_crypto_context plg_cryptoctx, int pkcs7_type);

static krb5_error_code pkinit_create_sequence_of_principal_identifiers
(krb5_context context, pkinit_plg_crypto_context plg_cryptoctx,
 pkinit_req_crypto_context req_cryptoctx,
 pkinit_identity_crypto_context id_cryptoctx,
 int type, krb5_pa_data ***e_data_out);

#ifndef WITHOUT_PKCS11
static krb5_error_code pkinit_find_private_key
(pkinit_identity_crypto_context, CK_ATTRIBUTE_TYPE usage,
 CK_OBJECT_HANDLE *objp);
static krb5_error_code pkinit_login
(krb5_context context, pkinit_identity_crypto_context id_cryptoctx,
 CK_TOKEN_INFO *tip);
static krb5_error_code pkinit_open_session
(krb5_context context, pkinit_identity_crypto_context id_cryptoctx);
static void * pkinit_C_LoadModule(const char *modname, CK_FUNCTION_LIST_PTR_PTR p11p);
static CK_RV pkinit_C_UnloadModule(void *handle);
#ifdef SILLYDECRYPT
CK_RV pkinit_C_Decrypt
(pkinit_identity_crypto_context id_cryptoctx,
 CK_BYTE_PTR pEncryptedData, CK_ULONG  ulEncryptedDataLen,
 CK_BYTE_PTR pData, CK_ULONG_PTR pulDataLen);
#endif

static krb5_error_code pkinit_sign_data_pkcs11
(krb5_context context, pkinit_identity_crypto_context id_cryptoctx,
 unsigned char *data, unsigned int data_len,
 unsigned char **sig, unsigned int *sig_len);
static krb5_error_code pkinit_decode_data_pkcs11
(krb5_context context, pkinit_identity_crypto_context id_cryptoctx,
 unsigned char *data, unsigned int data_len,
 unsigned char **decoded_data, unsigned int *decoded_data_len);
#endif  /* WITHOUT_PKCS11 */

static krb5_error_code pkinit_sign_data_fs
(krb5_context context, pkinit_identity_crypto_context id_cryptoctx,
 unsigned char *data, unsigned int data_len,
 unsigned char **sig, unsigned int *sig_len);
static krb5_error_code pkinit_decode_data_fs
(krb5_context context, pkinit_identity_crypto_context id_cryptoctx,
 unsigned char *data, unsigned int data_len,
 unsigned char **decoded_data, unsigned int *decoded_data_len);

static krb5_error_code der_decode_data
(unsigned char *, long, unsigned char **, long *);

static krb5_error_code
create_krb5_invalidCertificates(krb5_context context,
                                pkinit_plg_crypto_context plg_cryptoctx,
                                pkinit_req_crypto_context req_cryptoctx,
                                pkinit_identity_crypto_context id_cryptoctx,
                                krb5_external_principal_identifier *** ids);

static krb5_error_code
create_identifiers_from_stack(STACK_OF(X509) *sk,
                              krb5_external_principal_identifier *** ids);
#ifdef LONGHORN_BETA_COMPAT
static int
wrap_signeddata(unsigned char *data, unsigned int data_len,
                unsigned char **out, unsigned int *out_len,
                int is_longhorn_server);
#else
static int
wrap_signeddata(unsigned char *data, unsigned int data_len,
                unsigned char **out, unsigned int *out_len);
#endif

static char *
pkinit_pkcs11_code_to_text(int err);


#if OPENSSL_VERSION_NUMBER >= 0x10000000L
/* Use CMS support present in OpenSSL 1.0 and later. */
#include <openssl/cms.h>
#define pkinit_CMS_get0_content_signed(_cms) CMS_get0_content(_cms)
#define pkinit_CMS_get0_content_data(_cms) CMS_get0_content(_cms)
#define pkinit_CMS_free1_crls(_sk_x509crl) sk_X509_CRL_free((_sk_x509crl))
#define pkinit_CMS_free1_certs(_sk_x509) sk_X509_free((_sk_x509))
#define pkinit_CMS_SignerInfo_get_cert(_cms,_si,_x509_pp)       \
    CMS_SignerInfo_get0_algs(_si,NULL,_x509_pp,NULL,NULL)
#else
/* Fake up CMS support using PKCS7. */
#define pkinit_CMS_free1_crls(_stack_of_x509crls)   /* Don't free these */
#define pkinit_CMS_free1_certs(_stack_of_x509certs) /* Don't free these */
#define CMS_NO_SIGNER_CERT_VERIFY PKCS7_NOVERIFY
#define CMS_NOATTR PKCS7_NOATTR
#define CMS_ContentInfo PKCS7
#define CMS_SignerInfo PKCS7_SIGNER_INFO
#define d2i_CMS_ContentInfo d2i_PKCS7
#define CMS_get0_type(_p7) ((_p7)->type)
#define pkinit_CMS_get0_content_signed(_p7) (&((_p7)->d.sign->contents->d.other->value.octet_string))
#define pkinit_CMS_get0_content_data(_p7) (&((_p7)->d.other->value.octet_string))
#define CMS_set1_signers_certs(_p7,_stack_of_x509,_uint)
#define CMS_get0_SignerInfos PKCS7_get_signer_info
#define stack_st_CMS_SignerInfo stack_st_PKCS7_SIGNER_INFO
#undef  sk_CMS_SignerInfo_value
#define sk_CMS_SignerInfo_value sk_PKCS7_SIGNER_INFO_value
#define CMS_get0_eContentType(_p7) (_p7->d.sign->contents->type)
#define CMS_verify PKCS7_verify
#define CMS_get1_crls(_p7) (_p7->d.sign->crl)
#define CMS_get1_certs(_p7) (_p7->d.sign->cert)
#define CMS_ContentInfo_free(_p7) PKCS7_free(_p7)
#define pkinit_CMS_SignerInfo_get_cert(_p7,_si,_x509_pp)        \
    (*_x509_pp) = PKCS7_cert_from_signer_info(_p7,_si)
#endif

static struct pkcs11_errstrings {
    short code;
    char *text;
} pkcs11_errstrings[] = {
    { 0x0,      "ok" },
    { 0x1,      "cancel" },
    { 0x2,      "host memory" },
    { 0x3,      "slot id invalid" },
    { 0x5,      "general error" },
    { 0x6,      "function failed" },
    { 0x7,      "arguments bad" },
    { 0x8,      "no event" },
    { 0x9,      "need to create threads" },
    { 0xa,      "cant lock" },
    { 0x10,     "attribute read only" },
    { 0x11,     "attribute sensitive" },
    { 0x12,     "attribute type invalid" },
    { 0x13,     "attribute value invalid" },
    { 0x20,     "data invalid" },
    { 0x21,     "data len range" },
    { 0x30,     "device error" },
    { 0x31,     "device memory" },
    { 0x32,     "device removed" },
    { 0x40,     "encrypted data invalid" },
    { 0x41,     "encrypted data len range" },
    { 0x50,     "function canceled" },
    { 0x51,     "function not parallel" },
    { 0x54,     "function not supported" },
    { 0x60,     "key handle invalid" },
    { 0x62,     "key size range" },
    { 0x63,     "key type inconsistent" },
    { 0x64,     "key not needed" },
    { 0x65,     "key changed" },
    { 0x66,     "key needed" },
    { 0x67,     "key indigestible" },
    { 0x68,     "key function not permitted" },
    { 0x69,     "key not wrappable" },
    { 0x6a,     "key unextractable" },
    { 0x70,     "mechanism invalid" },
    { 0x71,     "mechanism param invalid" },
    { 0x82,     "object handle invalid" },
    { 0x90,     "operation active" },
    { 0x91,     "operation not initialized" },
    { 0xa0,     "pin incorrect" },
    { 0xa1,     "pin invalid" },
    { 0xa2,     "pin len range" },
    { 0xa3,     "pin expired" },
    { 0xa4,     "pin locked" },
    { 0xb0,     "session closed" },
    { 0xb1,     "session count" },
    { 0xb3,     "session handle invalid" },
    { 0xb4,     "session parallel not supported" },
    { 0xb5,     "session read only" },
    { 0xb6,     "session exists" },
    { 0xb7,     "session read only exists" },
    { 0xb8,     "session read write so exists" },
    { 0xc0,     "signature invalid" },
    { 0xc1,     "signature len range" },
    { 0xd0,     "template incomplete" },
    { 0xd1,     "template inconsistent" },
    { 0xe0,     "token not present" },
    { 0xe1,     "token not recognized" },
    { 0xe2,     "token write protected" },
    { 0xf0,     "unwrapping key handle invalid" },
    { 0xf1,     "unwrapping key size range" },
    { 0xf2,     "unwrapping key type inconsistent" },
    { 0x100,    "user already logged in" },
    { 0x101,    "user not logged in" },
    { 0x102,    "user pin not initialized" },
    { 0x103,    "user type invalid" },
    { 0x104,    "user another already logged in" },
    { 0x105,    "user too many types" },
    { 0x110,    "wrapped key invalid" },
    { 0x112,    "wrapped key len range" },
    { 0x113,    "wrapping key handle invalid" },
    { 0x114,    "wrapping key size range" },
    { 0x115,    "wrapping key type inconsistent" },
    { 0x120,    "random seed not supported" },
    { 0x121,    "random no rng" },
    { 0x130,    "domain params invalid" },
    { 0x150,    "buffer too small" },
    { 0x160,    "saved state invalid" },
    { 0x170,    "information sensitive" },
    { 0x180,    "state unsaveable" },
    { 0x190,    "cryptoki not initialized" },
    { 0x191,    "cryptoki already initialized" },
    { 0x1a0,    "mutex bad" },
    { 0x1a1,    "mutex not locked" },
    { 0x200,    "function rejected" },
    { -1,       NULL }
};

/* DH parameters */
unsigned char pkinit_1024_dhprime[128] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xC9, 0x0F, 0xDA, 0xA2, 0x21, 0x68, 0xC2, 0x34,
    0xC4, 0xC6, 0x62, 0x8B, 0x80, 0xDC, 0x1C, 0xD1,
    0x29, 0x02, 0x4E, 0x08, 0x8A, 0x67, 0xCC, 0x74,
    0x02, 0x0B, 0xBE, 0xA6, 0x3B, 0x13, 0x9B, 0x22,
    0x51, 0x4A, 0x08, 0x79, 0x8E, 0x34, 0x04, 0xDD,
    0xEF, 0x95, 0x19, 0xB3, 0xCD, 0x3A, 0x43, 0x1B,
    0x30, 0x2B, 0x0A, 0x6D, 0xF2, 0x5F, 0x14, 0x37,
    0x4F, 0xE1, 0x35, 0x6D, 0x6D, 0x51, 0xC2, 0x45,
    0xE4, 0x85, 0xB5, 0x76, 0x62, 0x5E, 0x7E, 0xC6,
    0xF4, 0x4C, 0x42, 0xE9, 0xA6, 0x37, 0xED, 0x6B,
    0x0B, 0xFF, 0x5C, 0xB6, 0xF4, 0x06, 0xB7, 0xED,
    0xEE, 0x38, 0x6B, 0xFB, 0x5A, 0x89, 0x9F, 0xA5,
    0xAE, 0x9F, 0x24, 0x11, 0x7C, 0x4B, 0x1F, 0xE6,
    0x49, 0x28, 0x66, 0x51, 0xEC, 0xE6, 0x53, 0x81,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

unsigned char pkinit_2048_dhprime[2048/8] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xC9, 0x0F, 0xDA, 0xA2, 0x21, 0x68, 0xC2, 0x34,
    0xC4, 0xC6, 0x62, 0x8B, 0x80, 0xDC, 0x1C, 0xD1,
    0x29, 0x02, 0x4E, 0x08, 0x8A, 0x67, 0xCC, 0x74,
    0x02, 0x0B, 0xBE, 0xA6, 0x3B, 0x13, 0x9B, 0x22,
    0x51, 0x4A, 0x08, 0x79, 0x8E, 0x34, 0x04, 0xDD,
    0xEF, 0x95, 0x19, 0xB3, 0xCD, 0x3A, 0x43, 0x1B,
    0x30, 0x2B, 0x0A, 0x6D, 0xF2, 0x5F, 0x14, 0x37,
    0x4F, 0xE1, 0x35, 0x6D, 0x6D, 0x51, 0xC2, 0x45,
    0xE4, 0x85, 0xB5, 0x76, 0x62, 0x5E, 0x7E, 0xC6,
    0xF4, 0x4C, 0x42, 0xE9, 0xA6, 0x37, 0xED, 0x6B,
    0x0B, 0xFF, 0x5C, 0xB6, 0xF4, 0x06, 0xB7, 0xED,
    0xEE, 0x38, 0x6B, 0xFB, 0x5A, 0x89, 0x9F, 0xA5,
    0xAE, 0x9F, 0x24, 0x11, 0x7C, 0x4B, 0x1F, 0xE6,
    0x49, 0x28, 0x66, 0x51, 0xEC, 0xE4, 0x5B, 0x3D,
    0xC2, 0x00, 0x7C, 0xB8, 0xA1, 0x63, 0xBF, 0x05,
    0x98, 0xDA, 0x48, 0x36, 0x1C, 0x55, 0xD3, 0x9A,
    0x69, 0x16, 0x3F, 0xA8, 0xFD, 0x24, 0xCF, 0x5F,
    0x83, 0x65, 0x5D, 0x23, 0xDC, 0xA3, 0xAD, 0x96,
    0x1C, 0x62, 0xF3, 0x56, 0x20, 0x85, 0x52, 0xBB,
    0x9E, 0xD5, 0x29, 0x07, 0x70, 0x96, 0x96, 0x6D,
    0x67, 0x0C, 0x35, 0x4E, 0x4A, 0xBC, 0x98, 0x04,
    0xF1, 0x74, 0x6C, 0x08, 0xCA, 0x18, 0x21, 0x7C,
    0x32, 0x90, 0x5E, 0x46, 0x2E, 0x36, 0xCE, 0x3B,
    0xE3, 0x9E, 0x77, 0x2C, 0x18, 0x0E, 0x86, 0x03,
    0x9B, 0x27, 0x83, 0xA2, 0xEC, 0x07, 0xA2, 0x8F,
    0xB5, 0xC5, 0x5D, 0xF0, 0x6F, 0x4C, 0x52, 0xC9,
    0xDE, 0x2B, 0xCB, 0xF6, 0x95, 0x58, 0x17, 0x18,
    0x39, 0x95, 0x49, 0x7C, 0xEA, 0x95, 0x6A, 0xE5,
    0x15, 0xD2, 0x26, 0x18, 0x98, 0xFA, 0x05, 0x10,
    0x15, 0x72, 0x8E, 0x5A, 0x8A, 0xAC, 0xAA, 0x68,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

unsigned char pkinit_4096_dhprime[4096/8] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xC9, 0x0F, 0xDA, 0xA2, 0x21, 0x68, 0xC2, 0x34,
    0xC4, 0xC6, 0x62, 0x8B, 0x80, 0xDC, 0x1C, 0xD1,
    0x29, 0x02, 0x4E, 0x08, 0x8A, 0x67, 0xCC, 0x74,
    0x02, 0x0B, 0xBE, 0xA6, 0x3B, 0x13, 0x9B, 0x22,
    0x51, 0x4A, 0x08, 0x79, 0x8E, 0x34, 0x04, 0xDD,
    0xEF, 0x95, 0x19, 0xB3, 0xCD, 0x3A, 0x43, 0x1B,
    0x30, 0x2B, 0x0A, 0x6D, 0xF2, 0x5F, 0x14, 0x37,
    0x4F, 0xE1, 0x35, 0x6D, 0x6D, 0x51, 0xC2, 0x45,
    0xE4, 0x85, 0xB5, 0x76, 0x62, 0x5E, 0x7E, 0xC6,
    0xF4, 0x4C, 0x42, 0xE9, 0xA6, 0x37, 0xED, 0x6B,
    0x0B, 0xFF, 0x5C, 0xB6, 0xF4, 0x06, 0xB7, 0xED,
    0xEE, 0x38, 0x6B, 0xFB, 0x5A, 0x89, 0x9F, 0xA5,
    0xAE, 0x9F, 0x24, 0x11, 0x7C, 0x4B, 0x1F, 0xE6,
    0x49, 0x28, 0x66, 0x51, 0xEC, 0xE4, 0x5B, 0x3D,
    0xC2, 0x00, 0x7C, 0xB8, 0xA1, 0x63, 0xBF, 0x05,
    0x98, 0xDA, 0x48, 0x36, 0x1C, 0x55, 0xD3, 0x9A,
    0x69, 0x16, 0x3F, 0xA8, 0xFD, 0x24, 0xCF, 0x5F,
    0x83, 0x65, 0x5D, 0x23, 0xDC, 0xA3, 0xAD, 0x96,
    0x1C, 0x62, 0xF3, 0x56, 0x20, 0x85, 0x52, 0xBB,
    0x9E, 0xD5, 0x29, 0x07, 0x70, 0x96, 0x96, 0x6D,
    0x67, 0x0C, 0x35, 0x4E, 0x4A, 0xBC, 0x98, 0x04,
    0xF1, 0x74, 0x6C, 0x08, 0xCA, 0x18, 0x21, 0x7C,
    0x32, 0x90, 0x5E, 0x46, 0x2E, 0x36, 0xCE, 0x3B,
    0xE3, 0x9E, 0x77, 0x2C, 0x18, 0x0E, 0x86, 0x03,
    0x9B, 0x27, 0x83, 0xA2, 0xEC, 0x07, 0xA2, 0x8F,
    0xB5, 0xC5, 0x5D, 0xF0, 0x6F, 0x4C, 0x52, 0xC9,
    0xDE, 0x2B, 0xCB, 0xF6, 0x95, 0x58, 0x17, 0x18,
    0x39, 0x95, 0x49, 0x7C, 0xEA, 0x95, 0x6A, 0xE5,
    0x15, 0xD2, 0x26, 0x18, 0x98, 0xFA, 0x05, 0x10,
    0x15, 0x72, 0x8E, 0x5A, 0x8A, 0xAA, 0xC4, 0x2D,
    0xAD, 0x33, 0x17, 0x0D, 0x04, 0x50, 0x7A, 0x33,
    0xA8, 0x55, 0x21, 0xAB, 0xDF, 0x1C, 0xBA, 0x64,
    0xEC, 0xFB, 0x85, 0x04, 0x58, 0xDB, 0xEF, 0x0A,
    0x8A, 0xEA, 0x71, 0x57, 0x5D, 0x06, 0x0C, 0x7D,
    0xB3, 0x97, 0x0F, 0x85, 0xA6, 0xE1, 0xE4, 0xC7,
    0xAB, 0xF5, 0xAE, 0x8C, 0xDB, 0x09, 0x33, 0xD7,
    0x1E, 0x8C, 0x94, 0xE0, 0x4A, 0x25, 0x61, 0x9D,
    0xCE, 0xE3, 0xD2, 0x26, 0x1A, 0xD2, 0xEE, 0x6B,
    0xF1, 0x2F, 0xFA, 0x06, 0xD9, 0x8A, 0x08, 0x64,
    0xD8, 0x76, 0x02, 0x73, 0x3E, 0xC8, 0x6A, 0x64,
    0x52, 0x1F, 0x2B, 0x18, 0x17, 0x7B, 0x20, 0x0C,
    0xBB, 0xE1, 0x17, 0x57, 0x7A, 0x61, 0x5D, 0x6C,
    0x77, 0x09, 0x88, 0xC0, 0xBA, 0xD9, 0x46, 0xE2,
    0x08, 0xE2, 0x4F, 0xA0, 0x74, 0xE5, 0xAB, 0x31,
    0x43, 0xDB, 0x5B, 0xFC, 0xE0, 0xFD, 0x10, 0x8E,
    0x4B, 0x82, 0xD1, 0x20, 0xA9, 0x21, 0x08, 0x01,
    0x1A, 0x72, 0x3C, 0x12, 0xA7, 0x87, 0xE6, 0xD7,
    0x88, 0x71, 0x9A, 0x10, 0xBD, 0xBA, 0x5B, 0x26,
    0x99, 0xC3, 0x27, 0x18, 0x6A, 0xF4, 0xE2, 0x3C,
    0x1A, 0x94, 0x68, 0x34, 0xB6, 0x15, 0x0B, 0xDA,
    0x25, 0x83, 0xE9, 0xCA, 0x2A, 0xD4, 0x4C, 0xE8,
    0xDB, 0xBB, 0xC2, 0xDB, 0x04, 0xDE, 0x8E, 0xF9,
    0x2E, 0x8E, 0xFC, 0x14, 0x1F, 0xBE, 0xCA, 0xA6,
    0x28, 0x7C, 0x59, 0x47, 0x4E, 0x6B, 0xC0, 0x5D,
    0x99, 0xB2, 0x96, 0x4F, 0xA0, 0x90, 0xC3, 0xA2,
    0x23, 0x3B, 0xA1, 0x86, 0x51, 0x5B, 0xE7, 0xED,
    0x1F, 0x61, 0x29, 0x70, 0xCE, 0xE2, 0xD7, 0xAF,
    0xB8, 0x1B, 0xDD, 0x76, 0x21, 0x70, 0x48, 0x1C,
    0xD0, 0x06, 0x91, 0x27, 0xD5, 0xB0, 0x5A, 0xA9,
    0x93, 0xB4, 0xEA, 0x98, 0x8D, 0x8F, 0xDD, 0xC1,
    0x86, 0xFF, 0xB7, 0xDC, 0x90, 0xA6, 0xC0, 0x8F,
    0x4D, 0xF4, 0x35, 0xC9, 0x34, 0x06, 0x31, 0x99,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

static int pkinit_oids_refs = 0;

krb5_error_code
pkinit_init_plg_crypto(pkinit_plg_crypto_context *cryptoctx)
{
    krb5_error_code retval = ENOMEM;
    pkinit_plg_crypto_context ctx = NULL;

    /* initialize openssl routines */
    openssl_init();

    ctx = malloc(sizeof(*ctx));
    if (ctx == NULL)
        goto out;
    memset(ctx, 0, sizeof(*ctx));

    pkiDebug("%s: initializing openssl crypto context at %p\n",
             __FUNCTION__, ctx);
    retval = pkinit_init_pkinit_oids(ctx);
    if (retval)
        goto out;

    retval = pkinit_init_dh_params(ctx);
    if (retval)
        goto out;

    *cryptoctx = ctx;

out:
    if (retval && ctx != NULL)
        pkinit_fini_plg_crypto(ctx);

    return retval;
}

void
pkinit_fini_plg_crypto(pkinit_plg_crypto_context cryptoctx)
{
    pkiDebug("%s: freeing context at %p\n", __FUNCTION__, cryptoctx);

    if (cryptoctx == NULL)
        return;
    pkinit_fini_pkinit_oids(cryptoctx);
    pkinit_fini_dh_params(cryptoctx);
    free(cryptoctx);
}

krb5_error_code
pkinit_init_identity_crypto(pkinit_identity_crypto_context *idctx)
{
    krb5_error_code retval = ENOMEM;
    pkinit_identity_crypto_context ctx = NULL;

    ctx = malloc(sizeof(*ctx));
    if (ctx == NULL)
        goto out;
    memset(ctx, 0, sizeof(*ctx));

    retval = pkinit_init_certs(ctx);
    if (retval)
        goto out;

    retval = pkinit_init_pkcs11(ctx);
    if (retval)
        goto out;

    pkiDebug("%s: returning ctx at %p\n", __FUNCTION__, ctx);
    *idctx = ctx;

out:
    if (retval) {
        if (ctx)
            pkinit_fini_identity_crypto(ctx);
    }

    return retval;
}

void
pkinit_fini_identity_crypto(pkinit_identity_crypto_context idctx)
{
    if (idctx == NULL)
        return;

    pkiDebug("%s: freeing   ctx at %p\n", __FUNCTION__, idctx);
    pkinit_fini_certs(idctx);
    pkinit_fini_pkcs11(idctx);
    free(idctx);
}

krb5_error_code
pkinit_init_req_crypto(pkinit_req_crypto_context *cryptoctx)
{
    krb5_error_code retval = ENOMEM;
    pkinit_req_crypto_context ctx = NULL;

    ctx = malloc(sizeof(*ctx));
    if (ctx == NULL)
        goto out;
    memset(ctx, 0, sizeof(*ctx));

    ctx->dh = NULL;
    ctx->received_cert = NULL;

    *cryptoctx = ctx;

    pkiDebug("%s: returning ctx at %p\n", __FUNCTION__, ctx);
    retval = 0;
out:
    if (retval)
        free(ctx);

    return retval;
}

void
pkinit_fini_req_crypto(pkinit_req_crypto_context req_cryptoctx)
{
    if (req_cryptoctx == NULL)
        return;

    pkiDebug("%s: freeing   ctx at %p\n", __FUNCTION__, req_cryptoctx);
    if (req_cryptoctx->dh != NULL)
        DH_free(req_cryptoctx->dh);
    if (req_cryptoctx->received_cert != NULL)
        X509_free(req_cryptoctx->received_cert);

    free(req_cryptoctx);
}

static krb5_error_code
pkinit_init_pkinit_oids(pkinit_plg_crypto_context ctx)
{
    krb5_error_code retval = ENOMEM;
    int nid = 0;

    /*
     * If OpenSSL already knows about the OID, use the
     * existing definition. Otherwise, create an OID object.
     */
#define CREATE_OBJ_IF_NEEDED(oid, vn, sn, ln)                           \
    nid = OBJ_txt2nid(oid);                                             \
    if (nid == NID_undef) {                                             \
        nid = OBJ_create(oid, sn, ln);                                  \
        if (nid == NID_undef) {                                         \
            pkiDebug("Error creating oid object for '%s'\n", oid);      \
            goto out;                                                   \
        }                                                               \
    }                                                                   \
    ctx->vn = OBJ_nid2obj(nid);

    CREATE_OBJ_IF_NEEDED("1.3.6.1.5.2.2", id_pkinit_san,
                         "id-pkinit-san", "KRB5PrincipalName");

    CREATE_OBJ_IF_NEEDED("1.3.6.1.5.2.3.1", id_pkinit_authData,
                         "id-pkinit-authdata", "PKINIT signedAuthPack");

    CREATE_OBJ_IF_NEEDED("1.3.6.1.5.2.3.2", id_pkinit_DHKeyData,
                         "id-pkinit-DHKeyData", "PKINIT dhSignedData");

    CREATE_OBJ_IF_NEEDED("1.3.6.1.5.2.3.3", id_pkinit_rkeyData,
                         "id-pkinit-rkeyData", "PKINIT encKeyPack");

    CREATE_OBJ_IF_NEEDED("1.3.6.1.5.2.3.4", id_pkinit_KPClientAuth,
                         "id-pkinit-KPClientAuth", "PKINIT Client EKU");

    CREATE_OBJ_IF_NEEDED("1.3.6.1.5.2.3.5", id_pkinit_KPKdc,
                         "id-pkinit-KPKdc", "KDC EKU");

#if 0
    CREATE_OBJ_IF_NEEDED("1.2.840.113549.1.7.1", id_pkinit_authData9,
                         "id-pkcs7-data", "PKCS7 data");
#else
    /* See note in pkinit_pkcs7type2oid() */
    ctx->id_pkinit_authData9 = NULL;
#endif

    CREATE_OBJ_IF_NEEDED("1.3.6.1.4.1.311.20.2.2", id_ms_kp_sc_logon,
                         "id-ms-kp-sc-logon EKU", "Microsoft SmartCard Login EKU");

    CREATE_OBJ_IF_NEEDED("1.3.6.1.4.1.311.20.2.3", id_ms_san_upn,
                         "id-ms-san-upn", "Microsoft Universal Principal Name");

    CREATE_OBJ_IF_NEEDED("1.3.6.1.5.5.7.3.1", id_kp_serverAuth,
                         "id-kp-serverAuth EKU", "Server Authentication EKU");

    /* Success */
    retval = 0;
    pkinit_oids_refs++;

out:
    return retval;
}

static krb5_error_code
get_cert(char *filename, X509 **retcert)
{
    X509 *cert = NULL;
    BIO *tmp = NULL;
    int code;
    krb5_error_code retval;

    if (filename == NULL || retcert == NULL)
        return EINVAL;

    *retcert = NULL;

    tmp = BIO_new(BIO_s_file());
    if (tmp == NULL)
        return ENOMEM;

    code = BIO_read_filename(tmp, filename);
    if (code == 0) {
        retval = errno;
        goto cleanup;
    }

    cert = (X509 *) PEM_read_bio_X509(tmp, NULL, NULL, NULL);
    if (cert == NULL) {
        retval = EIO;
        pkiDebug("failed to read certificate from %s\n", filename);
        goto cleanup;
    }
    *retcert = cert;
    retval = 0;
cleanup:
    if (tmp != NULL)
        BIO_free(tmp);
    return retval;
}

static krb5_error_code
get_key(char *filename, EVP_PKEY **retkey)
{
    EVP_PKEY *pkey = NULL;
    BIO *tmp = NULL;
    int code;
    krb5_error_code retval;

    if (filename == NULL || retkey == NULL)
        return EINVAL;

    tmp = BIO_new(BIO_s_file());
    if (tmp == NULL)
        return ENOMEM;

    code = BIO_read_filename(tmp, filename);
    if (code == 0) {
        retval = errno;
        goto cleanup;
    }
    pkey = (EVP_PKEY *) PEM_read_bio_PrivateKey(tmp, NULL, NULL, NULL);
    if (pkey == NULL) {
        retval = EIO;
        pkiDebug("failed to read private key from %s\n", filename);
        goto cleanup;
    }
    *retkey = pkey;
    retval = 0;
cleanup:
    if (tmp != NULL)
        BIO_free(tmp);
    return retval;
}

static void
pkinit_fini_pkinit_oids(pkinit_plg_crypto_context ctx)
{
    if (ctx == NULL)
        return;

    /* Only call OBJ_cleanup once! */
    if (--pkinit_oids_refs == 0)
        OBJ_cleanup();
}

static krb5_error_code
pkinit_init_dh_params(pkinit_plg_crypto_context plgctx)
{
    krb5_error_code retval = ENOMEM;

    plgctx->dh_1024 = DH_new();
    if (plgctx->dh_1024 == NULL)
        goto cleanup;
    plgctx->dh_1024->p = BN_bin2bn(pkinit_1024_dhprime,
                                   sizeof(pkinit_1024_dhprime), NULL);
    if ((plgctx->dh_1024->g = BN_new()) == NULL ||
        (plgctx->dh_1024->q = BN_new()) == NULL)
        goto cleanup;
    BN_set_word(plgctx->dh_1024->g, DH_GENERATOR_2);
    BN_rshift1(plgctx->dh_1024->q, plgctx->dh_1024->p);

    plgctx->dh_2048 = DH_new();
    if (plgctx->dh_2048 == NULL)
        goto cleanup;
    plgctx->dh_2048->p = BN_bin2bn(pkinit_2048_dhprime,
                                   sizeof(pkinit_2048_dhprime), NULL);
    if ((plgctx->dh_2048->g = BN_new()) == NULL ||
        (plgctx->dh_2048->q = BN_new()) == NULL)
        goto cleanup;
    BN_set_word(plgctx->dh_2048->g, DH_GENERATOR_2);
    BN_rshift1(plgctx->dh_2048->q, plgctx->dh_2048->p);

    plgctx->dh_4096 = DH_new();
    if (plgctx->dh_4096 == NULL)
        goto cleanup;
    plgctx->dh_4096->p = BN_bin2bn(pkinit_4096_dhprime,
                                   sizeof(pkinit_4096_dhprime), NULL);
    if ((plgctx->dh_4096->g = BN_new()) == NULL ||
        (plgctx->dh_4096->q = BN_new()) == NULL)
        goto cleanup;
    BN_set_word(plgctx->dh_4096->g, DH_GENERATOR_2);
    BN_rshift1(plgctx->dh_4096->q, plgctx->dh_4096->p);

    retval = 0;

cleanup:
    if (retval)
        pkinit_fini_dh_params(plgctx);

    return retval;
}

static void
pkinit_fini_dh_params(pkinit_plg_crypto_context plgctx)
{
    if (plgctx->dh_1024 != NULL)
        DH_free(plgctx->dh_1024);
    if (plgctx->dh_2048 != NULL)
        DH_free(plgctx->dh_2048);
    if (plgctx->dh_4096 != NULL)
        DH_free(plgctx->dh_4096);

    plgctx->dh_1024 = plgctx->dh_2048 = plgctx->dh_4096 = NULL;
}

static krb5_error_code
pkinit_init_certs(pkinit_identity_crypto_context ctx)
{
    krb5_error_code retval = ENOMEM;
    int i;

    for (i = 0; i < MAX_CREDS_ALLOWED; i++)
        ctx->creds[i] = NULL;
    ctx->my_certs = NULL;
    ctx->cert_index = 0;
    ctx->my_key = NULL;
    ctx->trustedCAs = NULL;
    ctx->intermediateCAs = NULL;
    ctx->revoked = NULL;

    retval = 0;
    return retval;
}

static void
pkinit_fini_certs(pkinit_identity_crypto_context ctx)
{
    if (ctx == NULL)
        return;

    if (ctx->my_certs != NULL)
        sk_X509_pop_free(ctx->my_certs, X509_free);

    if (ctx->my_key != NULL)
        EVP_PKEY_free(ctx->my_key);

    if (ctx->trustedCAs != NULL)
        sk_X509_pop_free(ctx->trustedCAs, X509_free);

    if (ctx->intermediateCAs != NULL)
        sk_X509_pop_free(ctx->intermediateCAs, X509_free);

    if (ctx->revoked != NULL)
        sk_X509_CRL_pop_free(ctx->revoked, X509_CRL_free);
}

static krb5_error_code
pkinit_init_pkcs11(pkinit_identity_crypto_context ctx)
{
    krb5_error_code retval = ENOMEM;

#ifndef WITHOUT_PKCS11
    ctx->p11_module_name = strdup(PKCS11_MODNAME);
    if (ctx->p11_module_name == NULL)
        return retval;
    ctx->p11_module = NULL;
    ctx->slotid = PK_NOSLOT;
    ctx->token_label = NULL;
    ctx->cert_label = NULL;
    ctx->session = CK_INVALID_HANDLE;
    ctx->p11 = NULL;
#endif
    ctx->pkcs11_method = 0;

    retval = 0;
    return retval;
}

static void
pkinit_fini_pkcs11(pkinit_identity_crypto_context ctx)
{
#ifndef WITHOUT_PKCS11
    if (ctx == NULL)
        return;

    if (ctx->p11 != NULL) {
        if (ctx->session) {
            ctx->p11->C_CloseSession(ctx->session);
            ctx->session = CK_INVALID_HANDLE;
        }
        ctx->p11->C_Finalize(NULL_PTR);
        ctx->p11 = NULL;
    }
    if (ctx->p11_module != NULL) {
        pkinit_C_UnloadModule(ctx->p11_module);
        ctx->p11_module = NULL;
    }
    free(ctx->p11_module_name);
    free(ctx->token_label);
    free(ctx->cert_id);
    free(ctx->cert_label);
#endif
}

krb5_error_code
pkinit_identity_set_prompter(pkinit_identity_crypto_context id_cryptoctx,
                             krb5_prompter_fct prompter,
                             void *prompter_data)
{
    id_cryptoctx->prompter = prompter;
    id_cryptoctx->prompter_data = prompter_data;

    return 0;
}

/*helper function for creating pkinit ContentInfo*/
static krb5_error_code
create_contentinfo(krb5_context context,
                   pkinit_plg_crypto_context plg_crypto_context,
                   ASN1_OBJECT *oid, unsigned char *data, size_t data_len,
                   PKCS7 **out_p7)
{
    krb5_error_code retval = EINVAL;
    PKCS7 *inner_p7;
    ASN1_TYPE *pkinit_data = NULL;

    *out_p7 = NULL;
    if ((inner_p7 = PKCS7_new()) == NULL)
        goto cleanup;
    if ((pkinit_data = ASN1_TYPE_new()) == NULL)
        goto cleanup;
    pkinit_data->type = V_ASN1_OCTET_STRING;
    if ((pkinit_data->value.octet_string = ASN1_OCTET_STRING_new()) == NULL)
        goto cleanup;
    if (!ASN1_OCTET_STRING_set(pkinit_data->value.octet_string,
                               (unsigned char *) data, data_len)) {
        unsigned long err = ERR_peek_error();
        retval = KRB5KDC_ERR_PREAUTH_FAILED;
        krb5_set_error_message(context, retval, "%s\n",
                               ERR_error_string(err, NULL));
        pkiDebug("failed to add pkcs7 data\n");
        goto cleanup;
    }
    if (!PKCS7_set0_type_other(inner_p7, OBJ_obj2nid(oid), pkinit_data))
        goto cleanup;
    retval = 0;
    *out_p7 = inner_p7;
    inner_p7 = NULL;
    pkinit_data = NULL;
cleanup:
    if (inner_p7)
        PKCS7_free(inner_p7);
    if (pkinit_data)
        ASN1_TYPE_free(pkinit_data);
    return retval;
}

krb5_error_code
cms_contentinfo_create(krb5_context context,                          /* IN */
                       pkinit_plg_crypto_context plg_cryptoctx,       /* IN */
                       pkinit_req_crypto_context req_cryptoctx,       /* IN */
                       pkinit_identity_crypto_context id_cryptoctx,   /* IN */
                       int cms_msg_type,
                       unsigned char *data, unsigned int data_len,
                       unsigned char **out_data, unsigned int *out_data_len)
{
    krb5_error_code retval = ENOMEM;
    ASN1_OBJECT *oid = NULL;
    PKCS7 *p7 = NULL;
    unsigned char *p;

    /* Pick the correct oid for the eContentInfo. */
    oid = pkinit_pkcs7type2oid(plg_cryptoctx, cms_msg_type);
    if (oid == NULL)
        goto cleanup;
    retval = create_contentinfo(context, plg_cryptoctx, oid,
                                data, data_len, &p7);
    if (retval != 0)
        goto cleanup;
    *out_data_len = i2d_PKCS7(p7, NULL);
    if (!(*out_data_len)) {
        unsigned long err = ERR_peek_error();
        retval = KRB5KDC_ERR_PREAUTH_FAILED;
        krb5_set_error_message(context, retval, "%s\n",
                               ERR_error_string(err, NULL));
        pkiDebug("failed to der encode pkcs7\n");
        goto cleanup;
    }
    retval = ENOMEM;
    if ((p = *out_data = malloc(*out_data_len)) == NULL)
        goto cleanup;

    /* DER encode PKCS7 data */
    retval = i2d_PKCS7(p7, &p);
    if (!retval) {
        unsigned long err = ERR_peek_error();
        retval = KRB5KDC_ERR_PREAUTH_FAILED;
        krb5_set_error_message(context, retval, "%s\n",
                               ERR_error_string(err, NULL));
        pkiDebug("failed to der encode pkcs7\n");
        goto cleanup;
    }
    retval = 0;
cleanup:
    if (p7)
        PKCS7_free(p7);
    if (oid)
        ASN1_OBJECT_free(oid);
    return retval;
}



krb5_error_code
cms_signeddata_create(krb5_context context,
                      pkinit_plg_crypto_context plg_cryptoctx,
                      pkinit_req_crypto_context req_cryptoctx,
                      pkinit_identity_crypto_context id_cryptoctx,
                      int cms_msg_type,
                      int include_certchain,
                      unsigned char *data,
                      unsigned int data_len,
                      unsigned char **signed_data,
                      unsigned int *signed_data_len)
{
    krb5_error_code retval = ENOMEM;
    PKCS7  *p7 = NULL, *inner_p7 = NULL;
    PKCS7_SIGNED *p7s = NULL;
    PKCS7_SIGNER_INFO *p7si = NULL;
    unsigned char *p;
    STACK_OF(X509) * cert_stack = NULL;
    ASN1_OCTET_STRING *digest_attr = NULL;
    EVP_MD_CTX ctx, ctx2;
    const EVP_MD *md_tmp = NULL;
    unsigned char md_data[EVP_MAX_MD_SIZE], md_data2[EVP_MAX_MD_SIZE];
    unsigned char *digestInfo_buf = NULL, *abuf = NULL;
    unsigned int md_len, md_len2, alen, digestInfo_len;
    STACK_OF(X509_ATTRIBUTE) * sk;
    unsigned char *sig = NULL;
    unsigned int sig_len = 0;
    X509_ALGOR *alg = NULL;
    ASN1_OCTET_STRING *digest = NULL;
    unsigned int alg_len = 0, digest_len = 0;
    unsigned char *y = NULL, *alg_buf = NULL, *digest_buf = NULL;
    X509 *cert = NULL;
    ASN1_OBJECT *oid = NULL;

    /* Start creating PKCS7 data. */
    if ((p7 = PKCS7_new()) == NULL)
        goto cleanup;
    p7->type = OBJ_nid2obj(NID_pkcs7_signed);

    if ((p7s = PKCS7_SIGNED_new()) == NULL)
        goto cleanup;
    p7->d.sign = p7s;
    if (!ASN1_INTEGER_set(p7s->version, 3))
        goto cleanup;

    /* pick the correct oid for the eContentInfo */
    oid = pkinit_pkcs7type2oid(plg_cryptoctx, cms_msg_type);
    if (oid == NULL)
        goto cleanup;

    if (id_cryptoctx->my_certs != NULL) {
        /* create a cert chain that has at least the signer's certificate */
        if ((cert_stack = sk_X509_new_null()) == NULL)
            goto cleanup;

        cert = sk_X509_value(id_cryptoctx->my_certs, id_cryptoctx->cert_index);
        if (!include_certchain) {
            pkiDebug("only including signer's certificate\n");
            sk_X509_push(cert_stack, X509_dup(cert));
        } else {
            /* create a cert chain */
            X509_STORE *certstore = NULL;
            X509_STORE_CTX certctx;
            STACK_OF(X509) *certstack = NULL;
            char buf[DN_BUF_LEN];
            unsigned int i = 0, size = 0;

            if ((certstore = X509_STORE_new()) == NULL)
                goto cleanup;
            pkiDebug("building certificate chain\n");
            X509_STORE_set_verify_cb_func(certstore, openssl_callback);
            X509_STORE_CTX_init(&certctx, certstore, cert,
                                id_cryptoctx->intermediateCAs);
            X509_STORE_CTX_trusted_stack(&certctx, id_cryptoctx->trustedCAs);
            if (!X509_verify_cert(&certctx)) {
                pkiDebug("failed to create a certificate chain: %s\n",
                         X509_verify_cert_error_string(X509_STORE_CTX_get_error(&certctx)));
                if (!sk_X509_num(id_cryptoctx->trustedCAs))
                    pkiDebug("No trusted CAs found. Check your X509_anchors\n");
                goto cleanup;
            }
            certstack = X509_STORE_CTX_get1_chain(&certctx);
            size = sk_X509_num(certstack);
            pkiDebug("size of certificate chain = %d\n", size);
            for(i = 0; i < size - 1; i++) {
                X509 *x = sk_X509_value(certstack, i);
                X509_NAME_oneline(X509_get_subject_name(x), buf, sizeof(buf));
                pkiDebug("cert #%d: %s\n", i, buf);
                sk_X509_push(cert_stack, X509_dup(x));
            }
            X509_STORE_CTX_cleanup(&certctx);
            X509_STORE_free(certstore);
            sk_X509_pop_free(certstack, X509_free);
        }
        p7s->cert = cert_stack;

        /* fill-in PKCS7_SIGNER_INFO */
        if ((p7si = PKCS7_SIGNER_INFO_new()) == NULL)
            goto cleanup;
        if (!ASN1_INTEGER_set(p7si->version, 1))
            goto cleanup;
        if (!X509_NAME_set(&p7si->issuer_and_serial->issuer,
                           X509_get_issuer_name(cert)))
            goto cleanup;
        /* because ASN1_INTEGER_set is used to set a 'long' we will do
         * things the ugly way. */
        M_ASN1_INTEGER_free(p7si->issuer_and_serial->serial);
        if (!(p7si->issuer_and_serial->serial =
              M_ASN1_INTEGER_dup(X509_get_serialNumber(cert))))
            goto cleanup;

        /* will not fill-out EVP_PKEY because it's on the smartcard */

        /* Set digest algs */
        p7si->digest_alg->algorithm = OBJ_nid2obj(NID_sha1);

        if (p7si->digest_alg->parameter != NULL)
            ASN1_TYPE_free(p7si->digest_alg->parameter);
        if ((p7si->digest_alg->parameter = ASN1_TYPE_new()) == NULL)
            goto cleanup;
        p7si->digest_alg->parameter->type = V_ASN1_NULL;

        /* Set sig algs */
        if (p7si->digest_enc_alg->parameter != NULL)
            ASN1_TYPE_free(p7si->digest_enc_alg->parameter);
        p7si->digest_enc_alg->algorithm = OBJ_nid2obj(NID_sha1WithRSAEncryption);
        if (!(p7si->digest_enc_alg->parameter = ASN1_TYPE_new()))
            goto cleanup;
        p7si->digest_enc_alg->parameter->type = V_ASN1_NULL;

        if (cms_msg_type == CMS_SIGN_DRAFT9){
            /* don't include signed attributes for pa-type 15 request */
            abuf = data;
            alen = data_len;
        } else {
            /* add signed attributes */
            /* compute sha1 digest over the EncapsulatedContentInfo */
            EVP_MD_CTX_init(&ctx);
            EVP_DigestInit_ex(&ctx, EVP_sha1(), NULL);
            EVP_DigestUpdate(&ctx, data, data_len);
            md_tmp = EVP_MD_CTX_md(&ctx);
            EVP_DigestFinal_ex(&ctx, md_data, &md_len);

            /* create a message digest attr */
            digest_attr = ASN1_OCTET_STRING_new();
            ASN1_OCTET_STRING_set(digest_attr, md_data, (int)md_len);
            PKCS7_add_signed_attribute(p7si, NID_pkcs9_messageDigest,
                                       V_ASN1_OCTET_STRING, (char *) digest_attr);

            /* create a content-type attr */
            PKCS7_add_signed_attribute(p7si, NID_pkcs9_contentType,
                                       V_ASN1_OBJECT, oid);

            /* create the signature over signed attributes. get DER encoded value */
            /* This is the place where smartcard signature needs to be calculated */
            sk = p7si->auth_attr;
            alen = ASN1_item_i2d((ASN1_VALUE *) sk, &abuf,
                                 ASN1_ITEM_rptr(PKCS7_ATTR_SIGN));
            if (abuf == NULL)
                goto cleanup2;
        } /* signed attributes */

#ifndef WITHOUT_PKCS11
        /* Some tokens can only do RSAEncryption without sha1 hash */
        /* to compute sha1WithRSAEncryption, encode the algorithm ID for the hash
         * function and the hash value into an ASN.1 value of type DigestInfo
         * DigestInfo::=SEQUENCE {
         *  digestAlgorithm  AlgorithmIdentifier,
         *  digest OCTET STRING }
         */
        if (id_cryptoctx->pkcs11_method == 1 &&
            id_cryptoctx->mech == CKM_RSA_PKCS) {
            pkiDebug("mech = CKM_RSA_PKCS\n");
            EVP_MD_CTX_init(&ctx2);
            /* if this is not draft9 request, include digest signed attribute */
            if (cms_msg_type != CMS_SIGN_DRAFT9)
                EVP_DigestInit_ex(&ctx2, md_tmp, NULL);
            else
                EVP_DigestInit_ex(&ctx2, EVP_sha1(), NULL);
            EVP_DigestUpdate(&ctx2, abuf, alen);
            EVP_DigestFinal_ex(&ctx2, md_data2, &md_len2);

            alg = X509_ALGOR_new();
            if (alg == NULL)
                goto cleanup2;
            alg->algorithm = OBJ_nid2obj(NID_sha1);
            alg->parameter = NULL;
            alg_len = i2d_X509_ALGOR(alg, NULL);
            alg_buf = malloc(alg_len);
            if (alg_buf == NULL)
                goto cleanup2;

            digest = ASN1_OCTET_STRING_new();
            if (digest == NULL)
                goto cleanup2;
            ASN1_OCTET_STRING_set(digest, md_data2, (int)md_len2);
            digest_len = i2d_ASN1_OCTET_STRING(digest, NULL);
            digest_buf = malloc(digest_len);
            if (digest_buf == NULL)
                goto cleanup2;

            digestInfo_len = ASN1_object_size(1, (int)(alg_len + digest_len),
                                              V_ASN1_SEQUENCE);
            y = digestInfo_buf = malloc(digestInfo_len);
            if (digestInfo_buf == NULL)
                goto cleanup2;
            ASN1_put_object(&y, 1, (int)(alg_len + digest_len), V_ASN1_SEQUENCE,
                            V_ASN1_UNIVERSAL);
            i2d_X509_ALGOR(alg, &y);
            i2d_ASN1_OCTET_STRING(digest, &y);
#ifdef DEBUG_SIG
            pkiDebug("signing buffer\n");
            print_buffer(digestInfo_buf, digestInfo_len);
            print_buffer_bin(digestInfo_buf, digestInfo_len, "/tmp/pkcs7_tosign");
#endif
            retval = pkinit_sign_data(context, id_cryptoctx, digestInfo_buf,
                                      digestInfo_len, &sig, &sig_len);
        } else
#endif
        {
            pkiDebug("mech = %s\n",
                     id_cryptoctx->pkcs11_method == 1 ? "CKM_SHA1_RSA_PKCS" : "FS");
            retval = pkinit_sign_data(context, id_cryptoctx, abuf, alen,
                                      &sig, &sig_len);
        }
#ifdef DEBUG_SIG
        print_buffer(sig, sig_len);
#endif
        if (cms_msg_type != CMS_SIGN_DRAFT9 )
            free(abuf);
        if (retval)
            goto cleanup2;

        /* Add signature */
        if (!ASN1_STRING_set(p7si->enc_digest, (unsigned char *) sig,
                             (int)sig_len)) {
            unsigned long err = ERR_peek_error();
            retval = KRB5KDC_ERR_PREAUTH_FAILED;
            krb5_set_error_message(context, retval, "%s\n",
                                   ERR_error_string(err, NULL));
            pkiDebug("failed to add a signed digest attribute\n");
            goto cleanup2;
        }
        /* adder signer_info to pkcs7 signed */
        if (!PKCS7_add_signer(p7, p7si))
            goto cleanup2;
    } /* we have a certificate */

    /* start on adding data to the pkcs7 signed */
    retval = create_contentinfo(context, plg_cryptoctx, oid,
                                data, data_len, &inner_p7);
    if (p7s->contents != NULL)
        PKCS7_free(p7s->contents);
    p7s->contents = inner_p7;

    *signed_data_len = i2d_PKCS7(p7, NULL);
    if (!(*signed_data_len)) {
        unsigned long err = ERR_peek_error();
        retval = KRB5KDC_ERR_PREAUTH_FAILED;
        krb5_set_error_message(context, retval, "%s\n",
                               ERR_error_string(err, NULL));
        pkiDebug("failed to der encode pkcs7\n");
        goto cleanup2;
    }
    retval = ENOMEM;
    if ((p = *signed_data = malloc(*signed_data_len)) == NULL)
        goto cleanup2;

    /* DER encode PKCS7 data */
    retval = i2d_PKCS7(p7, &p);
    if (!retval) {
        unsigned long err = ERR_peek_error();
        retval = KRB5KDC_ERR_PREAUTH_FAILED;
        krb5_set_error_message(context, retval, "%s\n",
                               ERR_error_string(err, NULL));
        pkiDebug("failed to der encode pkcs7\n");
        goto cleanup2;
    }
    retval = 0;

#ifdef DEBUG_ASN1
    if (cms_msg_type == CMS_SIGN_CLIENT) {
        print_buffer_bin(*signed_data, *signed_data_len,
                         "/tmp/client_pkcs7_signeddata");
    } else {
        if (cms_msg_type == CMS_SIGN_SERVER) {
            print_buffer_bin(*signed_data, *signed_data_len,
                             "/tmp/kdc_pkcs7_signeddata");
        } else {
            print_buffer_bin(*signed_data, *signed_data_len,
                             "/tmp/draft9_pkcs7_signeddata");
        }
    }
#endif

cleanup2:
    if (p7si) {
        if (cms_msg_type != CMS_SIGN_DRAFT9)
            EVP_MD_CTX_cleanup(&ctx);
#ifndef WITHOUT_PKCS11
        if (id_cryptoctx->pkcs11_method == 1 &&
            id_cryptoctx->mech == CKM_RSA_PKCS) {
            EVP_MD_CTX_cleanup(&ctx2);
            free(digest_buf);
            free(digestInfo_buf);
            free(alg_buf);
            if (digest != NULL)
                ASN1_OCTET_STRING_free(digest);
        }
#endif
        if (alg != NULL)
            X509_ALGOR_free(alg);
    }
cleanup:
    if (p7 != NULL)
        PKCS7_free(p7);
    free(sig);

    return retval;
}

krb5_error_code
cms_signeddata_verify(krb5_context context,
                      pkinit_plg_crypto_context plgctx,
                      pkinit_req_crypto_context reqctx,
                      pkinit_identity_crypto_context idctx,
                      int cms_msg_type,
                      int require_crl_checking,
                      unsigned char *signed_data,
                      unsigned int signed_data_len,
                      unsigned char **data,
                      unsigned int *data_len,
                      unsigned char **authz_data,
                      unsigned int *authz_data_len,
                      int *is_signed)
{
    /*
     * Warning: Since most openssl functions do not set retval, large chunks of
     * this function assume that retval is always a failure and may go to
     * cleanup without setting retval explicitly. Make sure retval is not set
     * to 0 or errors such as signature verification failure may be converted
     * to success with significant security consequences.
     */
    krb5_error_code retval = KRB5KDC_ERR_PREAUTH_FAILED;
    CMS_ContentInfo *cms = NULL;
    BIO *out = NULL;
    int flags = CMS_NO_SIGNER_CERT_VERIFY;
    int valid_oid = 0;
    unsigned int i = 0;
    unsigned int vflags = 0, size = 0;
    const unsigned char *p = signed_data;
    STACK_OF(CMS_SignerInfo) *si_sk = NULL;
    CMS_SignerInfo *si = NULL;
    X509 *x = NULL;
    X509_STORE *store = NULL;
    X509_STORE_CTX cert_ctx;
    STACK_OF(X509) *signerCerts = NULL;
    STACK_OF(X509) *intermediateCAs = NULL;
    STACK_OF(X509_CRL) *signerRevoked = NULL;
    STACK_OF(X509_CRL) *revoked = NULL;
    STACK_OF(X509) *verified_chain = NULL;
    ASN1_OBJECT *oid = NULL;
    const ASN1_OBJECT *type = NULL, *etype = NULL;
    ASN1_OCTET_STRING **octets;
    krb5_external_principal_identifier **krb5_verified_chain = NULL;
    krb5_data *authz = NULL;
    char buf[DN_BUF_LEN];

#ifdef DEBUG_ASN1
    print_buffer_bin(signed_data, signed_data_len,
                     "/tmp/client_received_pkcs7_signeddata");
#endif
    if (is_signed)
        *is_signed = 1;
    /* Do this early enough to create the shadow OID for pkcs7-data if needed */
    oid = pkinit_pkcs7type2oid(plgctx, cms_msg_type);
    if (oid == NULL)
        goto cleanup;

    /* decode received CMS message */
    if ((cms = d2i_CMS_ContentInfo(NULL, &p, (int)signed_data_len)) == NULL) {
        unsigned long err = ERR_peek_error();
        krb5_set_error_message(context, retval, "%s\n",
                               ERR_error_string(err, NULL));
        pkiDebug("%s: failed to decode message: %s\n",
                 __FUNCTION__, ERR_error_string(err, NULL));
        goto cleanup;
    }
    etype = CMS_get0_eContentType(cms);

    /*
     * Prior to 1.10 the MIT client incorrectly omitted the pkinit structure
     * directly in a CMS ContentInfo rather than using SignedData with no
     * signers. Handle that case.
     */
    type = CMS_get0_type(cms);
    if (is_signed && !OBJ_cmp(type, oid)) {
        unsigned char *d;
        *is_signed = 0;
        octets = pkinit_CMS_get0_content_data(cms);
        if (!octets || ((*octets)->type != V_ASN1_OCTET_STRING)) {
            retval = KRB5KDC_ERR_PREAUTH_FAILED;
            krb5_set_error_message(context, retval,
                                   _("Invalid pkinit packet: octet string "
                                     "expected"));
            goto cleanup;
        }
        *data_len = ASN1_STRING_length(*octets);
        d = malloc(*data_len);
        if (d == NULL) {
            retval = ENOMEM;
            goto cleanup;
        }
        memcpy(d, ASN1_STRING_data(*octets),
               *data_len);
        *data = d;
        goto out;
    } else {
        /* Verify that the received message is CMS SignedData message. */
        if (OBJ_obj2nid(type) != NID_pkcs7_signed) {
            pkiDebug("Expected id-signedData CMS msg (received type = %d)\n",
                     OBJ_obj2nid(type));
            krb5_set_error_message(context, retval, _("wrong oid\n"));
            goto cleanup;
        }
    }

    /* setup to verify X509 certificate used to sign CMS message */
    if (!(store = X509_STORE_new()))
        goto cleanup;

    /* check if we are inforcing CRL checking */
    vflags = X509_V_FLAG_CRL_CHECK|X509_V_FLAG_CRL_CHECK_ALL;
    if (require_crl_checking)
        X509_STORE_set_verify_cb_func(store, openssl_callback);
    else
        X509_STORE_set_verify_cb_func(store, openssl_callback_ignore_crls);
    X509_STORE_set_flags(store, vflags);

    /* get the signer's information from the CMS message */
    CMS_set1_signers_certs(cms, NULL, 0);
    if (((si_sk = CMS_get0_SignerInfos(cms)) == NULL) ||
        ((si = sk_CMS_SignerInfo_value(si_sk, 0)) == NULL)) {
        /* Not actually signed; anonymous case */
        if (!is_signed)
            goto cleanup;
        *is_signed = 0;
        /* We cannot use CMS_dataInit because there may be no digest */
        octets = pkinit_CMS_get0_content_signed(cms);
        if (octets)
            out = BIO_new_mem_buf((*octets)->data, (*octets)->length);
        if (out == NULL)
            goto cleanup;
    } else {
        pkinit_CMS_SignerInfo_get_cert(cms, si, &x);
        if (x == NULL)
            goto cleanup;

        /* create available CRL information (get local CRLs and include CRLs
         * received in the CMS message
         */
        signerRevoked = CMS_get1_crls(cms);
        if (idctx->revoked == NULL)
            revoked = signerRevoked;
        else if (signerRevoked == NULL)
            revoked = idctx->revoked;
        else {
            size = sk_X509_CRL_num(idctx->revoked);
            revoked = sk_X509_CRL_new_null();
            for (i = 0; i < size; i++)
                sk_X509_CRL_push(revoked, sk_X509_CRL_value(idctx->revoked, i));
            size = sk_X509_CRL_num(signerRevoked);
            for (i = 0; i < size; i++)
                sk_X509_CRL_push(revoked, sk_X509_CRL_value(signerRevoked, i));
        }

        /* create available intermediate CAs chains (get local intermediateCAs and
         * include the CA chain received in the CMS message
         */
        signerCerts = CMS_get1_certs(cms);
        if (idctx->intermediateCAs == NULL)
            intermediateCAs = signerCerts;
        else if (signerCerts == NULL)
            intermediateCAs = idctx->intermediateCAs;
        else {
            size = sk_X509_num(idctx->intermediateCAs);
            intermediateCAs = sk_X509_new_null();
            for (i = 0; i < size; i++) {
                sk_X509_push(intermediateCAs,
                             sk_X509_value(idctx->intermediateCAs, i));
            }
            size = sk_X509_num(signerCerts);
            for (i = 0; i < size; i++) {
                sk_X509_push(intermediateCAs, sk_X509_value(signerCerts, i));
            }
        }

        /* initialize x509 context with the received certificate and
         * trusted and intermediate CA chains and CRLs
         */
        if (!X509_STORE_CTX_init(&cert_ctx, store, x, intermediateCAs))
            goto cleanup;

        X509_STORE_CTX_set0_crls(&cert_ctx, revoked);

        /* add trusted CAs certificates for cert verification */
        if (idctx->trustedCAs != NULL)
            X509_STORE_CTX_trusted_stack(&cert_ctx, idctx->trustedCAs);
        else {
            pkiDebug("unable to find any trusted CAs\n");
            goto cleanup;
        }
#ifdef DEBUG_CERTCHAIN
        if (intermediateCAs != NULL) {
            size = sk_X509_num(intermediateCAs);
            pkiDebug("untrusted cert chain of size %d\n", size);
            for (i = 0; i < size; i++) {
                X509_NAME_oneline(X509_get_subject_name(
                                      sk_X509_value(intermediateCAs, i)), buf, sizeof(buf));
                pkiDebug("cert #%d: %s\n", i, buf);
            }
        }
        if (idctx->trustedCAs != NULL) {
            size = sk_X509_num(idctx->trustedCAs);
            pkiDebug("trusted cert chain of size %d\n", size);
            for (i = 0; i < size; i++) {
                X509_NAME_oneline(X509_get_subject_name(
                                      sk_X509_value(idctx->trustedCAs, i)), buf, sizeof(buf));
                pkiDebug("cert #%d: %s\n", i, buf);
            }
        }
        if (revoked != NULL) {
            size = sk_X509_CRL_num(revoked);
            pkiDebug("CRL chain of size %d\n", size);
            for (i = 0; i < size; i++) {
                X509_CRL *crl = sk_X509_CRL_value(revoked, i);
                X509_NAME_oneline(X509_CRL_get_issuer(crl), buf, sizeof(buf));
                pkiDebug("crls by CA #%d: %s\n", i , buf);
            }
        }
#endif

        i = X509_verify_cert(&cert_ctx);
        if (i <= 0) {
            int j = X509_STORE_CTX_get_error(&cert_ctx);

            reqctx->received_cert = X509_dup(cert_ctx.current_cert);
            switch(j) {
            case X509_V_ERR_CERT_REVOKED:
                retval = KRB5KDC_ERR_REVOKED_CERTIFICATE;
                break;
            case X509_V_ERR_UNABLE_TO_GET_CRL:
                retval = KRB5KDC_ERR_REVOCATION_STATUS_UNKNOWN;
                break;
            case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT:
            case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY:
                retval = KRB5KDC_ERR_CANT_VERIFY_CERTIFICATE;
                break;
            default:
                retval = KRB5KDC_ERR_INVALID_CERTIFICATE;
            }
            if (reqctx->received_cert == NULL)
                strlcpy(buf, "(none)", sizeof(buf));
            else
                X509_NAME_oneline(X509_get_subject_name(reqctx->received_cert),
                                  buf, sizeof(buf));
            pkiDebug("problem with cert DN = %s (error=%d) %s\n", buf, j,
                     X509_verify_cert_error_string(j));
            krb5_set_error_message(context, retval, "%s\n",
                                   X509_verify_cert_error_string(j));
#ifdef DEBUG_CERTCHAIN
            size = sk_X509_num(signerCerts);
            pkiDebug("received cert chain of size %d\n", size);
            for (j = 0; j < size; j++) {
                X509 *tmp_cert = sk_X509_value(signerCerts, j);
                X509_NAME_oneline(X509_get_subject_name(tmp_cert), buf, sizeof(buf));
                pkiDebug("cert #%d: %s\n", j, buf);
            }
#endif
        } else {
            /* retrieve verified certificate chain */
            if (cms_msg_type == CMS_SIGN_CLIENT || cms_msg_type == CMS_SIGN_DRAFT9)
                verified_chain = X509_STORE_CTX_get1_chain(&cert_ctx);
        }
        X509_STORE_CTX_cleanup(&cert_ctx);
        if (i <= 0)
            goto cleanup;
        out = BIO_new(BIO_s_mem());
        if (cms_msg_type == CMS_SIGN_DRAFT9)
            flags |= CMS_NOATTR;
        if (CMS_verify(cms, NULL, store, NULL, out, flags) == 0) {
            unsigned long err = ERR_peek_error();
            switch(ERR_GET_REASON(err)) {
            case PKCS7_R_DIGEST_FAILURE:
                retval = KRB5KDC_ERR_DIGEST_IN_SIGNED_DATA_NOT_ACCEPTED;
                break;
            case PKCS7_R_SIGNATURE_FAILURE:
            default:
                retval = KRB5KDC_ERR_INVALID_SIG;
            }
            pkiDebug("CMS Verification failure\n");
            krb5_set_error_message(context, retval, "%s\n",
                                   ERR_error_string(err, NULL));
            goto cleanup;
        }
    } /* message was signed */
    if (!OBJ_cmp(etype, oid))
        valid_oid = 1;
    else if (cms_msg_type == CMS_SIGN_DRAFT9) {
        /*
         * Various implementations of the pa-type 15 request use
         * different OIDS.  We check that the returned object
         * has any of the acceptable OIDs
         */
        ASN1_OBJECT *client_oid = NULL, *server_oid = NULL, *rsa_oid = NULL;
        client_oid = pkinit_pkcs7type2oid(plgctx, CMS_SIGN_CLIENT);
        server_oid = pkinit_pkcs7type2oid(plgctx, CMS_SIGN_SERVER);
        rsa_oid = pkinit_pkcs7type2oid(plgctx, CMS_ENVEL_SERVER);
        if (!OBJ_cmp(etype, client_oid) ||
            !OBJ_cmp(etype, server_oid) ||
            !OBJ_cmp(etype, rsa_oid))
            valid_oid = 1;
    }

    if (valid_oid)
        pkiDebug("CMS Verification successful\n");
    else {
        pkiDebug("wrong oid in eContentType\n");
        print_buffer(etype->data,
                     (unsigned int)etype->length);
        retval = KRB5KDC_ERR_PREAUTH_FAILED;
        krb5_set_error_message(context, retval, "wrong oid\n");
        goto cleanup;
    }

    /* transfer the data from CMS message into return buffer */
    for (size = 0;;) {
        int remain;
        retval = ENOMEM;
        if ((*data = realloc(*data, size + 1024 * 10)) == NULL)
            goto cleanup;
        remain = BIO_read(out, &((*data)[size]), 1024 * 10);
        if (remain <= 0)
            break;
        else
            size += remain;
    }
    *data_len = size;

    if (x) {
        reqctx->received_cert = X509_dup(x);

        /* generate authorization data */
        if (cms_msg_type == CMS_SIGN_CLIENT || cms_msg_type == CMS_SIGN_DRAFT9) {

            if (authz_data == NULL || authz_data_len == NULL)
                goto out;

            *authz_data = NULL;
            retval = create_identifiers_from_stack(verified_chain,
                                                   &krb5_verified_chain);
            if (retval) {
                pkiDebug("create_identifiers_from_stack failed\n");
                goto cleanup;
            }

            retval = k5int_encode_krb5_td_trusted_certifiers((const krb5_external_principal_identifier **)krb5_verified_chain, &authz);
            if (retval) {
                pkiDebug("encode_krb5_td_trusted_certifiers failed\n");
                goto cleanup;
            }
#ifdef DEBUG_ASN1
            print_buffer_bin((unsigned char *)authz->data, authz->length,
                             "/tmp/kdc_ad_initial_verified_cas");
#endif
            *authz_data = malloc(authz->length);
            if (*authz_data == NULL) {
                retval = ENOMEM;
                goto cleanup;
            }
            memcpy(*authz_data, authz->data, authz->length);
            *authz_data_len = authz->length;
        }
    }
out:
    retval = 0;

cleanup:
    if (out != NULL)
        BIO_free(out);
    if (store != NULL)
        X509_STORE_free(store);
    if (cms != NULL) {
        if (signerCerts != NULL)
            pkinit_CMS_free1_certs(signerCerts);
        if (idctx->intermediateCAs != NULL && signerCerts)
            sk_X509_free(intermediateCAs);
        if (signerRevoked != NULL)
            pkinit_CMS_free1_crls(signerRevoked);
        if (idctx->revoked != NULL && signerRevoked)
            sk_X509_CRL_free(revoked);
        CMS_ContentInfo_free(cms);
    }
    if (verified_chain != NULL)
        sk_X509_pop_free(verified_chain, X509_free);
    if (krb5_verified_chain != NULL)
        free_krb5_external_principal_identifier(&krb5_verified_chain);
    if (authz != NULL)
        krb5_free_data(context, authz);

    return retval;
}

krb5_error_code
cms_envelopeddata_create(krb5_context context,
                         pkinit_plg_crypto_context plgctx,
                         pkinit_req_crypto_context reqctx,
                         pkinit_identity_crypto_context idctx,
                         krb5_preauthtype pa_type,
                         int include_certchain,
                         unsigned char *key_pack,
                         unsigned int key_pack_len,
                         unsigned char **out,
                         unsigned int *out_len)
{

    krb5_error_code retval = ENOMEM;
    PKCS7 *p7 = NULL;
    BIO *in = NULL;
    unsigned char *p = NULL, *signed_data = NULL, *enc_data = NULL;
    int signed_data_len = 0, enc_data_len = 0, flags = PKCS7_BINARY;
    STACK_OF(X509) *encerts = NULL;
    const EVP_CIPHER *cipher = NULL;
    int cms_msg_type;

    /* create the PKCS7 SignedData portion of the PKCS7 EnvelopedData */
    switch ((int)pa_type) {
    case KRB5_PADATA_PK_AS_REQ_OLD:
    case KRB5_PADATA_PK_AS_REP_OLD:
        cms_msg_type = CMS_SIGN_DRAFT9;
        break;
    case KRB5_PADATA_PK_AS_REQ:
        cms_msg_type = CMS_ENVEL_SERVER;
        break;
    default:
        goto cleanup;
    }

    retval = cms_signeddata_create(context, plgctx, reqctx, idctx,
                                   cms_msg_type, include_certchain, key_pack, key_pack_len,
                                   &signed_data, (unsigned int *)&signed_data_len);
    if (retval) {
        pkiDebug("failed to create pkcs7 signed data\n");
        goto cleanup;
    }

    /* check we have client's certificate */
    if (reqctx->received_cert == NULL) {
        retval = KRB5KDC_ERR_PREAUTH_FAILED;
        goto cleanup;
    }
    encerts = sk_X509_new_null();
    sk_X509_push(encerts, reqctx->received_cert);

    cipher = EVP_des_ede3_cbc();
    in = BIO_new(BIO_s_mem());
    switch (pa_type) {
    case KRB5_PADATA_PK_AS_REQ:
        prepare_enc_data(signed_data, signed_data_len, &enc_data,
                         &enc_data_len);
        retval = BIO_write(in, enc_data, enc_data_len);
        if (retval != enc_data_len) {
            pkiDebug("BIO_write only wrote %d\n", retval);
            goto cleanup;
        }
        break;
    case KRB5_PADATA_PK_AS_REP_OLD:
    case KRB5_PADATA_PK_AS_REQ_OLD:
        retval = BIO_write(in, signed_data, signed_data_len);
        if (retval != signed_data_len) {
            pkiDebug("BIO_write only wrote %d\n", retval);
            goto cleanup;
        }
        break;
    default:
        retval = -1;
        goto cleanup;
    }

    p7 = PKCS7_encrypt(encerts, in, cipher, flags);
    if (p7 == NULL) {
        pkiDebug("failed to encrypt PKCS7 object\n");
        retval = -1;
        goto cleanup;
    }
    switch (pa_type) {
    case KRB5_PADATA_PK_AS_REQ:
        p7->d.enveloped->enc_data->content_type =
            OBJ_nid2obj(NID_pkcs7_signed);
        break;
    case KRB5_PADATA_PK_AS_REP_OLD:
    case KRB5_PADATA_PK_AS_REQ_OLD:
        p7->d.enveloped->enc_data->content_type =
            OBJ_nid2obj(NID_pkcs7_data);
        break;
        break;
        break;
        break;
    }

    *out_len = i2d_PKCS7(p7, NULL);
    if (!*out_len || (p = *out = malloc(*out_len)) == NULL) {
        retval = ENOMEM;
        goto cleanup;
    }
    retval = i2d_PKCS7(p7, &p);
    if (!retval) {
        pkiDebug("unable to write pkcs7 object\n");
        goto cleanup;
    }
    retval = 0;

#ifdef DEBUG_ASN1
    print_buffer_bin(*out, *out_len, "/tmp/kdc_enveloped_data");
#endif

cleanup:
    if (p7 != NULL)
        PKCS7_free(p7);
    if (in != NULL)
        BIO_free(in);
    free(signed_data);
    free(enc_data);
    if (encerts != NULL)
        sk_X509_free(encerts);

    return retval;
}

krb5_error_code
cms_envelopeddata_verify(krb5_context context,
                         pkinit_plg_crypto_context plg_cryptoctx,
                         pkinit_req_crypto_context req_cryptoctx,
                         pkinit_identity_crypto_context id_cryptoctx,
                         krb5_preauthtype pa_type,
                         int require_crl_checking,
                         unsigned char *enveloped_data,
                         unsigned int enveloped_data_len,
                         unsigned char **data,
                         unsigned int *data_len)
{
    krb5_error_code retval = KRB5KDC_ERR_PREAUTH_FAILED;
    PKCS7 *p7 = NULL;
    BIO *out = NULL;
    int i = 0;
    unsigned int size = 0;
    const unsigned char *p = enveloped_data;
    unsigned int tmp_buf_len = 0, tmp_buf2_len = 0, vfy_buf_len = 0;
    unsigned char *tmp_buf = NULL, *tmp_buf2 = NULL, *vfy_buf = NULL;
    int msg_type = 0;

#ifdef DEBUG_ASN1
    print_buffer_bin(enveloped_data, enveloped_data_len,
                     "/tmp/client_envelopeddata");
#endif
    /* decode received PKCS7 message */
    if ((p7 = d2i_PKCS7(NULL, &p, (int)enveloped_data_len)) == NULL) {
        unsigned long err = ERR_peek_error();
        pkiDebug("failed to decode pkcs7\n");
        krb5_set_error_message(context, retval, "%s\n",
                               ERR_error_string(err, NULL));
        goto cleanup;
    }

    /* verify that the received message is PKCS7 EnvelopedData message */
    if (OBJ_obj2nid(p7->type) != NID_pkcs7_enveloped) {
        pkiDebug("Expected id-enveloped PKCS7 msg (received type = %d)\n",
                 OBJ_obj2nid(p7->type));
        krb5_set_error_message(context, retval, "wrong oid\n");
        goto cleanup;
    }

    /* decrypt received PKCS7 message */
    out = BIO_new(BIO_s_mem());
    if (pkcs7_decrypt(context, id_cryptoctx, p7, out)) {
        pkiDebug("PKCS7 decryption successful\n");
    } else {
        unsigned long err = ERR_peek_error();
        if (err != 0)
            krb5_set_error_message(context, retval, "%s\n",
                                   ERR_error_string(err, NULL));
        pkiDebug("PKCS7 decryption failed\n");
        goto cleanup;
    }

    /* transfer the decoded PKCS7 SignedData message into a separate buffer */
    for (;;) {
        if ((tmp_buf = realloc(tmp_buf, size + 1024 * 10)) == NULL)
            goto cleanup;
        i = BIO_read(out, &(tmp_buf[size]), 1024 * 10);
        if (i <= 0)
            break;
        else
            size += i;
    }
    tmp_buf_len = size;

#ifdef DEBUG_ASN1
    print_buffer_bin(tmp_buf, tmp_buf_len, "/tmp/client_enc_keypack");
#endif
    /* verify PKCS7 SignedData message */
    switch (pa_type) {
    case KRB5_PADATA_PK_AS_REP:
        msg_type = CMS_ENVEL_SERVER;

        break;
    case KRB5_PADATA_PK_AS_REP_OLD:
        msg_type = CMS_SIGN_DRAFT9;
        break;
    default:
        pkiDebug("%s: unrecognized pa_type = %d\n", __FUNCTION__, pa_type);
        retval = KRB5KDC_ERR_PREAUTH_FAILED;
        goto cleanup;
    }
    /*
     * If this is the RFC style, wrap the signed data to make
     * decoding easier in the verify routine.
     * For draft9-compatible, we don't do anything because it
     * is already wrapped.
     */
#ifdef LONGHORN_BETA_COMPAT
    /*
     * The Longhorn server returns the expected RFC-style data, but
     * it is missing the sequence tag and length, so it requires
     * special processing when wrapping.
     * This will hopefully be fixed before the final release and
     * this can all be removed.
     */
    if (msg_type == CMS_ENVEL_SERVER || longhorn == 1) {
        retval = wrap_signeddata(tmp_buf, tmp_buf_len,
                                 &tmp_buf2, &tmp_buf2_len, longhorn);
        if (retval) {
            pkiDebug("failed to encode signeddata\n");
            goto cleanup;
        }
        vfy_buf = tmp_buf2;
        vfy_buf_len = tmp_buf2_len;

    } else {
        vfy_buf = tmp_buf;
        vfy_buf_len = tmp_buf_len;
    }
#else
    if (msg_type == CMS_ENVEL_SERVER) {
        retval = wrap_signeddata(tmp_buf, tmp_buf_len,
                                 &tmp_buf2, &tmp_buf2_len);
        if (retval) {
            pkiDebug("failed to encode signeddata\n");
            goto cleanup;
        }
        vfy_buf = tmp_buf2;
        vfy_buf_len = tmp_buf2_len;

    } else {
        vfy_buf = tmp_buf;
        vfy_buf_len = tmp_buf_len;
    }
#endif

#ifdef DEBUG_ASN1
    print_buffer_bin(vfy_buf, vfy_buf_len, "/tmp/client_enc_keypack2");
#endif

    retval = cms_signeddata_verify(context, plg_cryptoctx, req_cryptoctx,
                                   id_cryptoctx, msg_type,
                                   require_crl_checking,
                                   vfy_buf, vfy_buf_len,
                                   data, data_len, NULL, NULL, NULL);

    if (!retval)
        pkiDebug("PKCS7 Verification Success\n");
    else {
        pkiDebug("PKCS7 Verification Failure\n");
        goto cleanup;
    }

    retval = 0;

cleanup:

    if (p7 != NULL)
        PKCS7_free(p7);
    if (out != NULL)
        BIO_free(out);
    free(tmp_buf);
    free(tmp_buf2);

    return retval;
}

static krb5_error_code
crypto_retrieve_X509_sans(krb5_context context,
                          pkinit_plg_crypto_context plgctx,
                          pkinit_req_crypto_context reqctx,
                          X509 *cert,
                          krb5_principal **princs_ret,
                          krb5_principal **upn_ret,
                          unsigned char ***dns_ret)
{
    krb5_error_code retval = EINVAL;
    char buf[DN_BUF_LEN];
    int p = 0, u = 0, d = 0, l;
    krb5_principal *princs = NULL;
    krb5_principal *upns = NULL;
    unsigned char **dnss = NULL;
    unsigned int i, num_found = 0;

    if (princs_ret == NULL && upn_ret == NULL && dns_ret == NULL) {
        pkiDebug("%s: nowhere to return any values!\n", __FUNCTION__);
        return retval;
    }

    if (cert == NULL) {
        pkiDebug("%s: no certificate!\n", __FUNCTION__);
        return retval;
    }

    X509_NAME_oneline(X509_get_subject_name(cert),
                      buf, sizeof(buf));
    pkiDebug("%s: looking for SANs in cert = %s\n", __FUNCTION__, buf);

    if ((l = X509_get_ext_by_NID(cert, NID_subject_alt_name, -1)) >= 0) {
        X509_EXTENSION *ext = NULL;
        GENERAL_NAMES *ialt = NULL;
        GENERAL_NAME *gen = NULL;
        int ret = 0;
        unsigned int num_sans = 0;

        if (!(ext = X509_get_ext(cert, l)) || !(ialt = X509V3_EXT_d2i(ext))) {
            pkiDebug("%s: found no subject alt name extensions\n",
                     __FUNCTION__);
            goto cleanup;
        }
        num_sans = sk_GENERAL_NAME_num(ialt);

        pkiDebug("%s: found %d subject alt name extension(s)\n",
                 __FUNCTION__, num_sans);

        /* OK, we're likely returning something. Allocate return values */
        if (princs_ret != NULL) {
            princs = calloc(num_sans + 1, sizeof(krb5_principal));
            if (princs == NULL) {
                retval = ENOMEM;
                goto cleanup;
            }
        }
        if (upn_ret != NULL) {
            upns = calloc(num_sans + 1, sizeof(krb5_principal));
            if (upns == NULL) {
                retval = ENOMEM;
                goto cleanup;
            }
        }
        if (dns_ret != NULL) {
            dnss = calloc(num_sans + 1, sizeof(*dnss));
            if (dnss == NULL) {
                retval = ENOMEM;
                goto cleanup;
            }
        }

        for (i = 0; i < num_sans; i++) {
            krb5_data name = { 0, 0, NULL };

            gen = sk_GENERAL_NAME_value(ialt, i);
            switch (gen->type) {
            case GEN_OTHERNAME:
                name.length = gen->d.otherName->value->value.sequence->length;
                name.data = (char *)gen->d.otherName->value->value.sequence->data;
                if (princs != NULL
                    && OBJ_cmp(plgctx->id_pkinit_san,
                               gen->d.otherName->type_id) == 0) {
#ifdef DEBUG_ASN1
                    print_buffer_bin((unsigned char *)name.data, name.length,
                                     "/tmp/pkinit_san");
#endif
                    ret = k5int_decode_krb5_principal_name(&name, &princs[p]);
                    if (ret) {
                        pkiDebug("%s: failed decoding pkinit san value\n",
                                 __FUNCTION__);
                    } else {
                        p++;
                        num_found++;
                    }
                } else if (upns != NULL
                           && OBJ_cmp(plgctx->id_ms_san_upn,
                                      gen->d.otherName->type_id) == 0) {
                    /* Prevent abuse of embedded null characters. */
                    if (memchr(name.data, '\0', name.length))
                        break;
                    ret = krb5_parse_name(context, name.data, &upns[u]);
                    if (ret) {
                        pkiDebug("%s: failed parsing ms-upn san value\n",
                                 __FUNCTION__);
                    } else {
                        u++;
                        num_found++;
                    }
                } else {
                    pkiDebug("%s: unrecognized othername oid in SAN\n",
                             __FUNCTION__);
                    continue;
                }

                break;
            case GEN_DNS:
                if (dnss != NULL) {
                    /* Prevent abuse of embedded null characters. */
                    if (memchr(gen->d.dNSName->data, '\0',
                               gen->d.dNSName->length))
                        break;
                    pkiDebug("%s: found dns name = %s\n",
                             __FUNCTION__, gen->d.dNSName->data);
                    dnss[d] = (unsigned char *)
                        strdup((char *)gen->d.dNSName->data);
                    if (dnss[d] == NULL) {
                        pkiDebug("%s: failed to duplicate dns name\n",
                                 __FUNCTION__);
                    } else {
                        d++;
                        num_found++;
                    }
                }
                break;
            default:
                pkiDebug("%s: SAN type = %d expecting %d\n",
                         __FUNCTION__, gen->type, GEN_OTHERNAME);
            }
        }
        sk_GENERAL_NAME_pop_free(ialt, GENERAL_NAME_free);
    }

    retval = 0;
    if (princs)
        *princs_ret = princs;
    if (upns)
        *upn_ret = upns;
    if (dnss)
        *dns_ret = dnss;

cleanup:
    if (retval) {
        if (princs != NULL) {
            for (i = 0; princs[i] != NULL; i++)
                krb5_free_principal(context, princs[i]);
            free(princs);
        }
        if (upns != NULL) {
            for (i = 0; upns[i] != NULL; i++)
                krb5_free_principal(context, upns[i]);
            free(upns);
        }
        if (dnss != NULL) {
            for (i = 0; dnss[i] != NULL; i++)
                free(dnss[i]);
            free(dnss);
        }
    }
    return retval;
}

krb5_error_code
crypto_retrieve_cert_sans(krb5_context context,
                          pkinit_plg_crypto_context plgctx,
                          pkinit_req_crypto_context reqctx,
                          pkinit_identity_crypto_context idctx,
                          krb5_principal **princs_ret,
                          krb5_principal **upn_ret,
                          unsigned char ***dns_ret)
{
    krb5_error_code retval = EINVAL;

    if (reqctx->received_cert == NULL) {
        pkiDebug("%s: No certificate!\n", __FUNCTION__);
        return retval;
    }

    return crypto_retrieve_X509_sans(context, plgctx, reqctx,
                                     reqctx->received_cert, princs_ret,
                                     upn_ret, dns_ret);
}

krb5_error_code
crypto_check_cert_eku(krb5_context context,
                      pkinit_plg_crypto_context plgctx,
                      pkinit_req_crypto_context reqctx,
                      pkinit_identity_crypto_context idctx,
                      int checking_kdc_cert,
                      int allow_secondary_usage,
                      int *valid_eku)
{
    char buf[DN_BUF_LEN];
    int found_eku = 0;
    krb5_error_code retval = EINVAL;
    int i;

    *valid_eku = 0;
    if (reqctx->received_cert == NULL)
        goto cleanup;

    X509_NAME_oneline(X509_get_subject_name(reqctx->received_cert),
                      buf, sizeof(buf));
    pkiDebug("%s: looking for EKUs in cert = %s\n", __FUNCTION__, buf);

    if ((i = X509_get_ext_by_NID(reqctx->received_cert,
                                 NID_ext_key_usage, -1)) >= 0) {
        EXTENDED_KEY_USAGE *extusage;

        extusage = X509_get_ext_d2i(reqctx->received_cert, NID_ext_key_usage,
                                    NULL, NULL);
        if (extusage) {
            pkiDebug("%s: found eku info in the cert\n", __FUNCTION__);
            for (i = 0; found_eku == 0 && i < sk_ASN1_OBJECT_num(extusage); i++) {
                ASN1_OBJECT *tmp_oid;

                tmp_oid = sk_ASN1_OBJECT_value(extusage, i);
                pkiDebug("%s: checking eku %d of %d, allow_secondary = %d\n",
                         __FUNCTION__, i+1, sk_ASN1_OBJECT_num(extusage),
                         allow_secondary_usage);
                if (checking_kdc_cert) {
                    if ((OBJ_cmp(tmp_oid, plgctx->id_pkinit_KPKdc) == 0)
                        || (allow_secondary_usage
                            && OBJ_cmp(tmp_oid, plgctx->id_kp_serverAuth) == 0))
                        found_eku = 1;
                } else {
                    if ((OBJ_cmp(tmp_oid, plgctx->id_pkinit_KPClientAuth) == 0)
                        || (allow_secondary_usage
                            && OBJ_cmp(tmp_oid, plgctx->id_ms_kp_sc_logon) == 0))
                        found_eku = 1;
                }
            }
        }
        EXTENDED_KEY_USAGE_free(extusage);

        if (found_eku) {
            ASN1_BIT_STRING *usage = NULL;
            pkiDebug("%s: found acceptable EKU, checking for digitalSignature\n", __FUNCTION__);

            /* check that digitalSignature KeyUsage is present */
            X509_check_ca(reqctx->received_cert);
            if ((usage = X509_get_ext_d2i(reqctx->received_cert,
                                          NID_key_usage, NULL, NULL))) {

                if (!ku_reject(reqctx->received_cert,
                               X509v3_KU_DIGITAL_SIGNATURE)) {
                    pkiDebug("%s: found digitalSignature KU\n",
                             __FUNCTION__);
                    *valid_eku = 1;
                } else
                    pkiDebug("%s: didn't find digitalSignature KU\n",
                             __FUNCTION__);
            }
            ASN1_BIT_STRING_free(usage);
        }
    }
    retval = 0;
cleanup:
    pkiDebug("%s: returning retval %d, valid_eku %d\n",
             __FUNCTION__, retval, *valid_eku);
    return retval;
}

krb5_error_code
pkinit_octetstring2key(krb5_context context,
                       krb5_enctype etype,
                       unsigned char *key,
                       unsigned int dh_key_len,
                       krb5_keyblock *key_block)
{
    krb5_error_code retval;
    unsigned char *buf = NULL;
    unsigned char md[SHA_DIGEST_LENGTH];
    unsigned char counter;
    size_t keybytes, keylength, offset;
    krb5_data random_data;

    if ((buf = malloc(dh_key_len)) == NULL) {
        retval = ENOMEM;
        goto cleanup;
    }
    memset(buf, 0, dh_key_len);

    counter = 0;
    offset = 0;
    do {
        SHA_CTX c;

        SHA1_Init(&c);
        SHA1_Update(&c, &counter, 1);
        SHA1_Update(&c, key, dh_key_len);
        SHA1_Final(md, &c);

        if (dh_key_len - offset < sizeof(md))
            memcpy(buf + offset, md, dh_key_len - offset);
        else
            memcpy(buf + offset, md, sizeof(md));

        offset += sizeof(md);
        counter++;
    } while (offset < dh_key_len);

    key_block->magic = 0;
    key_block->enctype = etype;

    retval = krb5_c_keylengths(context, etype, &keybytes, &keylength);
    if (retval)
        goto cleanup;

    key_block->length = keylength;
    key_block->contents = malloc(keylength);
    if (key_block->contents == NULL) {
        retval = ENOMEM;
        goto cleanup;
    }

    random_data.length = keybytes;
    random_data.data = (char *)buf;

    retval = krb5_c_random_to_key(context, etype, &random_data, key_block);

cleanup:
    free(buf);
    /* If this is an error return, free the allocated keyblock, if any */
    if (retval) {
        krb5_free_keyblock_contents(context, key_block);
    }

    return retval;
}


/**
 * Given an algorithm_identifier, this function returns the hash length
 * and EVP function associated with that algorithm.
 */
static krb5_error_code
pkinit_alg_values(krb5_context context,
                  const krb5_octet_data *alg_id,
                  size_t *hash_bytes,
                  const EVP_MD *(**func)(void))
{
    *hash_bytes = 0;
    *func = NULL;
    if ((alg_id->length == krb5_pkinit_sha1_oid_len) &&
        (0 == memcmp(alg_id->data, &krb5_pkinit_sha1_oid,
                     krb5_pkinit_sha1_oid_len))) {
        *hash_bytes = 20;
        *func = &EVP_sha1;
        return 0;
    } else if ((alg_id->length == krb5_pkinit_sha256_oid_len) &&
               (0 == memcmp(alg_id->data, krb5_pkinit_sha256_oid,
                            krb5_pkinit_sha256_oid_len))) {
        *hash_bytes = 32;
        *func = &EVP_sha256;
        return 0;
    } else if ((alg_id->length == krb5_pkinit_sha512_oid_len) &&
               (0 == memcmp(alg_id->data, krb5_pkinit_sha512_oid,
                            krb5_pkinit_sha512_oid_len))) {
        *hash_bytes = 64;
        *func = &EVP_sha512;
        return 0;
    } else {
        krb5_set_error_message(context, KRB5_ERR_BAD_S2K_PARAMS,
                               "Bad algorithm ID passed to PK-INIT KDF.");
        return KRB5_ERR_BAD_S2K_PARAMS;
    }
} /* pkinit_alg_values() */


/* pkinit_alg_agility_kdf() --
 * This function generates a key using the KDF described in
 * draft_ietf_krb_wg_pkinit_alg_agility-04.txt.  The algorithm is
 * described as follows:
 *
 *     1.  reps = keydatalen (K) / hash length (H)
 *
 *     2.  Initialize a 32-bit, big-endian bit string counter as 1.
 *
 *     3.  For i = 1 to reps by 1, do the following:
 *
 *         -  Compute Hashi = H(counter || Z || OtherInfo).
 *
 *         -  Increment counter (modulo 2^32)
 *
 *     4.  Set key = Hash1 || Hash2 || ... so that length of key is K bytes.
 */
krb5_error_code
pkinit_alg_agility_kdf(krb5_context context,
                       krb5_octet_data *secret,
                       krb5_octet_data *alg_oid,
                       krb5_const_principal party_u_info,
                       krb5_const_principal party_v_info,
                       krb5_enctype enctype,
                       krb5_octet_data *as_req,
                       krb5_octet_data *pk_as_rep,
                       krb5_keyblock *key_block)
{
    krb5_error_code retval = 0;

    unsigned int reps = 0;
    uint32_t counter = 1;       /* Does this type work on Windows? */
    size_t offset = 0;
    size_t hash_len = 0;
    size_t rand_len = 0;
    size_t key_len = 0;
    krb5_data random_data;
    krb5_sp80056a_other_info other_info_fields;
    krb5_pkinit_supp_pub_info supp_pub_info_fields;
    krb5_data *other_info = NULL;
    krb5_data *supp_pub_info = NULL;
    krb5_algorithm_identifier alg_id;
    const EVP_MD *(*EVP_func)(void);

    /* initialize random_data here to make clean-up safe */
    random_data.length = 0;
    random_data.data = NULL;

    /* allocate and initialize the key block */
    key_block->magic = 0;
    key_block->enctype = enctype;
    if (0 != (retval = krb5_c_keylengths(context, enctype, &rand_len,
                                         &key_len)))
        goto cleanup;

    random_data.length = rand_len;
    key_block->length = key_len;

    if (NULL == (key_block->contents = malloc(key_block->length))) {
        retval = ENOMEM;
        goto cleanup;
    }

    memset (key_block->contents, 0, key_block->length);

    /* If this is anonymous pkinit, use the anonymous principle for party_u_info */
    if (party_u_info && krb5_principal_compare_any_realm(context, party_u_info,
                                                         krb5_anonymous_principal()))
        party_u_info = (krb5_principal)krb5_anonymous_principal();

    if (0 != (retval = pkinit_alg_values(context, alg_oid, &hash_len, &EVP_func)))
        goto cleanup;

    /* 1.  reps = keydatalen (K) / hash length (H) */
    reps = key_block->length/hash_len;

    /* ... and round up, if necessary */
    if (key_block->length > (reps * hash_len))
        reps++;

    /* Allocate enough space in the random data buffer to hash directly into
     * it, even if the last hash will make it bigger than the key length. */
    if (NULL == (random_data.data = malloc(reps * hash_len))) {
        retval = ENOMEM;
        goto cleanup;
    }

    /* Encode the ASN.1 octet string for "SuppPubInfo" */
    supp_pub_info_fields.enctype = enctype;
    supp_pub_info_fields.as_req = *as_req;
    supp_pub_info_fields.pk_as_rep = *pk_as_rep;
    if (0 != ((retval = encode_krb5_pkinit_supp_pub_info(&supp_pub_info_fields,
                                                         &supp_pub_info))))
        goto cleanup;

    /* Now encode the ASN.1 octet string for "OtherInfo" */
    memset(&alg_id, 0, sizeof alg_id);
    alg_id.algorithm = *alg_oid; /*alias*/

    other_info_fields.algorithm_identifier = alg_id;
    other_info_fields.party_u_info = (krb5_principal) party_u_info;
    other_info_fields.party_v_info = (krb5_principal) party_v_info;
    other_info_fields.supp_pub_info = *supp_pub_info;
    if (0 != (retval = encode_krb5_sp80056a_other_info(&other_info_fields, &other_info)))
        goto cleanup;

    /* 2.  Initialize a 32-bit, big-endian bit string counter as 1.
     * 3.  For i = 1 to reps by 1, do the following:
     *     -   Compute Hashi = H(counter || Z || OtherInfo).
     *     -   Increment counter (modulo 2^32)
     */
    for (counter = 1; counter <= reps; counter++) {
        EVP_MD_CTX c;
        uint s = 0;
        uint32_t be_counter = htonl(counter);

        EVP_MD_CTX_init(&c);

        /* -   Compute Hashi = H(counter || Z || OtherInfo). */
        if (0 == EVP_DigestInit(&c, EVP_func())) {
            krb5_set_error_message(context, KRB5_CRYPTO_INTERNAL,
                                   "Call to OpenSSL EVP_DigestInit() returned an error.");
            retval = KRB5_CRYPTO_INTERNAL;
            goto cleanup;
        }

        if ((0 == EVP_DigestUpdate(&c, &be_counter, 4)) ||
            (0 == EVP_DigestUpdate(&c, secret->data, secret->length)) ||
            (0 == EVP_DigestUpdate(&c, other_info->data, other_info->length))) {
            krb5_set_error_message(context, KRB5_CRYPTO_INTERNAL,
                                   "Call to OpenSSL EVP_DigestUpdate() returned an error.");
            retval = KRB5_CRYPTO_INTERNAL;
            goto cleanup;
        }

        /* 4.  Set key = Hash1 || Hash2 || ... so that length of key is K bytes. */
        if (0 == EVP_DigestFinal(&c, (unsigned char *)(random_data.data + offset), &s)) {
            krb5_set_error_message(context, KRB5_CRYPTO_INTERNAL,
                                   "Call to OpenSSL EVP_DigestUpdate() returned an error.");
            retval = KRB5_CRYPTO_INTERNAL;
            goto cleanup;
        }
        offset += s;
        assert(s == hash_len);

        EVP_MD_CTX_cleanup(&c);
    }

    retval = krb5_c_random_to_key(context, enctype, &random_data,
                                  key_block);

cleanup:
    /* If this has been an error, free the allocated key_block, if any */
    if (retval) {
        krb5_free_keyblock_contents(context, key_block);
    }

    /* free other allocated resources, either way */
    if (random_data.data)
        free(random_data.data);
    krb5_free_data(context, other_info);
    krb5_free_data(context, supp_pub_info);

    return retval;
} /*pkinit_alg_agility_kdf() */

/* Call DH_compute_key() and ensure that we left-pad short results instead of
 * leaving junk bytes at the end of the buffer. */
static void
compute_dh(unsigned char *buf, int size, BIGNUM *server_pub_key, DH *dh)
{
    int len, pad;

    len = DH_compute_key(buf, server_pub_key, dh);
    assert(len >= 0 && len <= size);
    if (len < size) {
        pad = size - len;
        memmove(buf + pad, buf, len);
        memset(buf, 0, pad);
    }
}

krb5_error_code
client_create_dh(krb5_context context,
                 pkinit_plg_crypto_context plg_cryptoctx,
                 pkinit_req_crypto_context cryptoctx,
                 pkinit_identity_crypto_context id_cryptoctx,
                 int dh_size,
                 unsigned char **dh_params,
                 unsigned int *dh_params_len,
                 unsigned char **dh_pubkey,
                 unsigned int *dh_pubkey_len)
{
    krb5_error_code retval = KRB5KDC_ERR_PREAUTH_FAILED;
    unsigned char *buf = NULL;
    int dh_err = 0;
    ASN1_INTEGER *pub_key = NULL;

    if (cryptoctx->dh == NULL) {
        if ((cryptoctx->dh = DH_new()) == NULL)
            goto cleanup;
        if ((cryptoctx->dh->g = BN_new()) == NULL ||
            (cryptoctx->dh->q = BN_new()) == NULL)
            goto cleanup;

        switch(dh_size) {
        case 1024:
            pkiDebug("client uses 1024 DH keys\n");
            cryptoctx->dh->p = get_rfc2409_prime_1024(NULL);
            break;
        case 2048:
            pkiDebug("client uses 2048 DH keys\n");
            cryptoctx->dh->p = BN_bin2bn(pkinit_2048_dhprime,
                                         sizeof(pkinit_2048_dhprime), NULL);
            break;
        case 4096:
            pkiDebug("client uses 4096 DH keys\n");
            cryptoctx->dh->p = BN_bin2bn(pkinit_4096_dhprime,
                                         sizeof(pkinit_4096_dhprime), NULL);
            break;
        default:
            goto cleanup;
        }

        BN_set_word((cryptoctx->dh->g), DH_GENERATOR_2);
        BN_rshift1(cryptoctx->dh->q, cryptoctx->dh->p);
    }

    DH_generate_key(cryptoctx->dh);
    DH_check(cryptoctx->dh, &dh_err);
    if (dh_err != 0) {
        pkiDebug("Warning: dh_check failed with %d\n", dh_err);
        if (dh_err & DH_CHECK_P_NOT_PRIME)
            pkiDebug("p value is not prime\n");
        if (dh_err & DH_CHECK_P_NOT_SAFE_PRIME)
            pkiDebug("p value is not a safe prime\n");
        if (dh_err & DH_UNABLE_TO_CHECK_GENERATOR)
            pkiDebug("unable to check the generator value\n");
        if (dh_err & DH_NOT_SUITABLE_GENERATOR)
            pkiDebug("the g value is not a generator\n");
    }
#ifdef DEBUG_DH
    print_dh(cryptoctx->dh, "client's DH params\n");
    print_pubkey(cryptoctx->dh->pub_key, "client's pub_key=");
#endif

    DH_check_pub_key(cryptoctx->dh, cryptoctx->dh->pub_key, &dh_err);
    if (dh_err != 0) {
        pkiDebug("dh_check_pub_key failed with %d\n", dh_err);
        goto cleanup;
    }

    /* pack DHparams */
    /* aglo: usually we could just call i2d_DHparams to encode DH params
     * however, PKINIT requires RFC3279 encoding and openssl does pkcs#3.
     */
    retval = pkinit_encode_dh_params(cryptoctx->dh->p, cryptoctx->dh->g,
                                     cryptoctx->dh->q, dh_params, dh_params_len);
    if (retval)
        goto cleanup;

    /* pack DH public key */
    /* Diffie-Hellman public key must be ASN1 encoded as an INTEGER; this
     * encoding shall be used as the contents (the value) of the
     * subjectPublicKey component (a BIT STRING) of the SubjectPublicKeyInfo
     * data element
     */
    if ((pub_key = BN_to_ASN1_INTEGER(cryptoctx->dh->pub_key, NULL)) == NULL)
        goto cleanup;
    *dh_pubkey_len = i2d_ASN1_INTEGER(pub_key, NULL);
    if ((buf = *dh_pubkey = malloc(*dh_pubkey_len)) == NULL) {
        retval  = ENOMEM;
        goto cleanup;
    }
    i2d_ASN1_INTEGER(pub_key, &buf);

    if (pub_key != NULL)
        ASN1_INTEGER_free(pub_key);

    retval = 0;
    return retval;

cleanup:
    if (cryptoctx->dh != NULL)
        DH_free(cryptoctx->dh);
    cryptoctx->dh = NULL;
    free(*dh_params);
    *dh_params = NULL;
    free(*dh_pubkey);
    *dh_pubkey = NULL;
    if (pub_key != NULL)
        ASN1_INTEGER_free(pub_key);

    return retval;
}

krb5_error_code
client_process_dh(krb5_context context,
                  pkinit_plg_crypto_context plg_cryptoctx,
                  pkinit_req_crypto_context cryptoctx,
                  pkinit_identity_crypto_context id_cryptoctx,
                  unsigned char *subjectPublicKey_data,
                  unsigned int subjectPublicKey_length,
                  unsigned char **client_key,
                  unsigned int *client_key_len)
{
    krb5_error_code retval = KRB5KDC_ERR_PREAUTH_FAILED;
    BIGNUM *server_pub_key = NULL;
    ASN1_INTEGER *pub_key = NULL;
    const unsigned char *p = NULL;
    unsigned char *data = NULL;
    long data_len;

    /* decode subjectPublicKey (retrieve INTEGER from OCTET_STRING) */

    if (der_decode_data(subjectPublicKey_data, (long)subjectPublicKey_length,
                        &data, &data_len) != 0) {
        pkiDebug("failed to decode subjectPublicKey\n");
        retval = -1;
        goto cleanup;
    }

    *client_key_len = DH_size(cryptoctx->dh);
    if ((*client_key = malloc(*client_key_len)) == NULL) {
        retval = ENOMEM;
        goto cleanup;
    }
    p = data;
    if ((pub_key = d2i_ASN1_INTEGER(NULL, &p, data_len)) == NULL)
        goto cleanup;
    if ((server_pub_key = ASN1_INTEGER_to_BN(pub_key, NULL)) == NULL)
        goto cleanup;

    compute_dh(*client_key, *client_key_len, server_pub_key, cryptoctx->dh);
#ifdef DEBUG_DH
    print_pubkey(server_pub_key, "server's pub_key=");
    pkiDebug("client computed key (%d)= ", *client_key_len);
    print_buffer(*client_key, *client_key_len);
#endif

    retval = 0;
    if (server_pub_key != NULL)
        BN_free(server_pub_key);
    if (pub_key != NULL)
        ASN1_INTEGER_free(pub_key);
    if (data != NULL)
        free (data);

    return retval;

cleanup:
    free(*client_key);
    *client_key = NULL;
    if (pub_key != NULL)
        ASN1_INTEGER_free(pub_key);
    if (data != NULL)
        free (data);

    return retval;
}

krb5_error_code
server_check_dh(krb5_context context,
                pkinit_plg_crypto_context cryptoctx,
                pkinit_req_crypto_context req_cryptoctx,
                pkinit_identity_crypto_context id_cryptoctx,
                krb5_octet_data *dh_params,
                int minbits)
{
    DH *dh = NULL;
    unsigned char *tmp = NULL;
    int dh_prime_bits;
    krb5_error_code retval = KRB5KDC_ERR_DH_KEY_PARAMETERS_NOT_ACCEPTED;

    tmp = dh_params->data;
    dh = DH_new();
    dh = pkinit_decode_dh_params(&dh, &tmp, dh_params->length);
    if (dh == NULL) {
        pkiDebug("failed to decode dhparams\n");
        goto cleanup;
    }

    /* KDC SHOULD check to see if the key parameters satisfy its policy */
    dh_prime_bits = BN_num_bits(dh->p);
    if (minbits && dh_prime_bits < minbits) {
        pkiDebug("client sent dh params with %d bits, we require %d\n",
                 dh_prime_bits, minbits);
        goto cleanup;
    }

    /* check dhparams is group 2 */
    if (pkinit_check_dh_params(cryptoctx->dh_1024->p,
                               dh->p, dh->g, dh->q) == 0) {
        retval = 0;
        goto cleanup;
    }

    /* check dhparams is group 14 */
    if (pkinit_check_dh_params(cryptoctx->dh_2048->p,
                               dh->p, dh->g, dh->q) == 0) {
        retval = 0;
        goto cleanup;
    }

    /* check dhparams is group 16 */
    if (pkinit_check_dh_params(cryptoctx->dh_4096->p,
                               dh->p, dh->g, dh->q) == 0) {
        retval = 0;
        goto cleanup;
    }

cleanup:
    if (retval == 0)
        req_cryptoctx->dh = dh;
    else
        DH_free(dh);

    return retval;
}

/* kdc's dh function */
krb5_error_code
server_process_dh(krb5_context context,
                  pkinit_plg_crypto_context plg_cryptoctx,
                  pkinit_req_crypto_context cryptoctx,
                  pkinit_identity_crypto_context id_cryptoctx,
                  unsigned char *data,
                  unsigned int data_len,
                  unsigned char **dh_pubkey,
                  unsigned int *dh_pubkey_len,
                  unsigned char **server_key,
                  unsigned int *server_key_len)
{
    krb5_error_code retval = ENOMEM;
    DH *dh = NULL, *dh_server = NULL;
    unsigned char *p = NULL;
    ASN1_INTEGER *pub_key = NULL;

    *dh_pubkey = *server_key = NULL;
    *dh_pubkey_len = *server_key_len = 0;

    /* get client's received DH parameters that we saved in server_check_dh */
    dh = cryptoctx->dh;

    dh_server = DH_new();
    if (dh_server == NULL)
        goto cleanup;
    dh_server->p = BN_dup(dh->p);
    dh_server->g = BN_dup(dh->g);
    dh_server->q = BN_dup(dh->q);

    /* decode client's public key */
    p = data;
    pub_key = d2i_ASN1_INTEGER(NULL, (const unsigned char **)&p, (int)data_len);
    if (pub_key == NULL)
        goto cleanup;
    dh->pub_key = ASN1_INTEGER_to_BN(pub_key, NULL);
    if (dh->pub_key == NULL)
        goto cleanup;
    ASN1_INTEGER_free(pub_key);

    if (!DH_generate_key(dh_server))
        goto cleanup;

    /* generate DH session key */
    *server_key_len = DH_size(dh_server);
    if ((*server_key = malloc(*server_key_len)) == NULL)
        goto cleanup;
    compute_dh(*server_key, *server_key_len, dh->pub_key, dh_server);

#ifdef DEBUG_DH
    print_dh(dh_server, "client&server's DH params\n");
    print_pubkey(dh->pub_key, "client's pub_key=");
    print_pubkey(dh_server->pub_key, "server's pub_key=");
    pkiDebug("server computed key=");
    print_buffer(*server_key, *server_key_len);
#endif

    /* KDC reply */
    /* pack DH public key */
    /* Diffie-Hellman public key must be ASN1 encoded as an INTEGER; this
     * encoding shall be used as the contents (the value) of the
     * subjectPublicKey component (a BIT STRING) of the SubjectPublicKeyInfo
     * data element
     */
    if ((pub_key = BN_to_ASN1_INTEGER(dh_server->pub_key, NULL)) == NULL)
        goto cleanup;
    *dh_pubkey_len = i2d_ASN1_INTEGER(pub_key, NULL);
    if ((p = *dh_pubkey = malloc(*dh_pubkey_len)) == NULL)
        goto cleanup;
    i2d_ASN1_INTEGER(pub_key, &p);
    if (pub_key != NULL)
        ASN1_INTEGER_free(pub_key);

    retval = 0;

    if (dh_server != NULL)
        DH_free(dh_server);
    return retval;

cleanup:
    if (dh_server != NULL)
        DH_free(dh_server);
    free(*dh_pubkey);
    free(*server_key);

    return retval;
}

static void
openssl_init()
{
    static int did_init = 0;

    if (!did_init) {
        /* initialize openssl routines */
        CRYPTO_malloc_init();
        ERR_load_crypto_strings();
        OpenSSL_add_all_algorithms();
        did_init++;
    }
}

static krb5_error_code
pkinit_encode_dh_params(BIGNUM *p, BIGNUM *g, BIGNUM *q,
                        unsigned char **buf, unsigned int *buf_len)
{
    krb5_error_code retval = ENOMEM;
    int bufsize = 0, r = 0;
    unsigned char *tmp = NULL;
    ASN1_INTEGER *ap = NULL, *ag = NULL, *aq = NULL;

    if ((ap = BN_to_ASN1_INTEGER(p, NULL)) == NULL)
        goto cleanup;
    if ((ag = BN_to_ASN1_INTEGER(g, NULL)) == NULL)
        goto cleanup;
    if ((aq = BN_to_ASN1_INTEGER(q, NULL)) == NULL)
        goto cleanup;
    bufsize = i2d_ASN1_INTEGER(ap, NULL);
    bufsize += i2d_ASN1_INTEGER(ag, NULL);
    bufsize += i2d_ASN1_INTEGER(aq, NULL);

    r = ASN1_object_size(1, bufsize, V_ASN1_SEQUENCE);

    tmp = *buf = malloc((size_t) r);
    if (tmp == NULL)
        goto cleanup;

    ASN1_put_object(&tmp, 1, bufsize, V_ASN1_SEQUENCE, V_ASN1_UNIVERSAL);

    i2d_ASN1_INTEGER(ap, &tmp);
    i2d_ASN1_INTEGER(ag, &tmp);
    i2d_ASN1_INTEGER(aq, &tmp);

    *buf_len = r;

    retval = 0;

cleanup:
    if (ap != NULL)
        ASN1_INTEGER_free(ap);
    if (ag != NULL)
        ASN1_INTEGER_free(ag);
    if (aq != NULL)
        ASN1_INTEGER_free(aq);

    return retval;
}

static DH *
pkinit_decode_dh_params(DH ** a, unsigned char **pp, unsigned int len)
{
    ASN1_INTEGER ai, *aip = NULL;
    long length = (long) len;

    M_ASN1_D2I_vars(a, DH *, DH_new);

    M_ASN1_D2I_Init();
    M_ASN1_D2I_start_sequence();
    aip = &ai;
    ai.data = NULL;
    ai.length = 0;
    M_ASN1_D2I_get_x(ASN1_INTEGER, aip, d2i_ASN1_INTEGER);
    if (aip == NULL)
        return NULL;
    else {
        (*a)->p = ASN1_INTEGER_to_BN(aip, NULL);
        if ((*a)->p == NULL)
            return NULL;
        if (ai.data != NULL) {
            OPENSSL_free(ai.data);
            ai.data = NULL;
            ai.length = 0;
        }
    }
    M_ASN1_D2I_get_x(ASN1_INTEGER, aip, d2i_ASN1_INTEGER);
    if (aip == NULL)
        return NULL;
    else {
        (*a)->g = ASN1_INTEGER_to_BN(aip, NULL);
        if ((*a)->g == NULL)
            return NULL;
        if (ai.data != NULL) {
            OPENSSL_free(ai.data);
            ai.data = NULL;
            ai.length = 0;
        }

    }
    M_ASN1_D2I_get_x(ASN1_INTEGER, aip, d2i_ASN1_INTEGER);
    if (aip == NULL)
        return NULL;
    else {
        (*a)->q = ASN1_INTEGER_to_BN(aip, NULL);
        if ((*a)->q == NULL)
            return NULL;
        if (ai.data != NULL) {
            OPENSSL_free(ai.data);
            ai.data = NULL;
            ai.length = 0;
        }

    }
    M_ASN1_D2I_end_sequence();
    M_ASN1_D2I_Finish(a, DH_free, 0);

}

static krb5_error_code
pkinit_create_sequence_of_principal_identifiers(
    krb5_context context,
    pkinit_plg_crypto_context plg_cryptoctx,
    pkinit_req_crypto_context req_cryptoctx,
    pkinit_identity_crypto_context id_cryptoctx,
    int type,
    krb5_pa_data ***e_data_out)
{
    krb5_error_code retval = KRB5KRB_ERR_GENERIC;
    krb5_external_principal_identifier **krb5_trusted_certifiers = NULL;
    krb5_data *td_certifiers = NULL;
    krb5_pa_data **pa_data = NULL;

    switch(type) {
    case TD_TRUSTED_CERTIFIERS:
        retval = create_krb5_trustedCertifiers(context, plg_cryptoctx,
                                               req_cryptoctx, id_cryptoctx, &krb5_trusted_certifiers);
        if (retval) {
            pkiDebug("create_krb5_trustedCertifiers failed\n");
            goto cleanup;
        }
        break;
    case TD_INVALID_CERTIFICATES:
        retval = create_krb5_invalidCertificates(context, plg_cryptoctx,
                                                 req_cryptoctx, id_cryptoctx, &krb5_trusted_certifiers);
        if (retval) {
            pkiDebug("create_krb5_invalidCertificates failed\n");
            goto cleanup;
        }
        break;
    default:
        retval = -1;
        goto cleanup;
    }

    retval = k5int_encode_krb5_td_trusted_certifiers((const krb5_external_principal_identifier **)krb5_trusted_certifiers, &td_certifiers);
    if (retval) {
        pkiDebug("encode_krb5_td_trusted_certifiers failed\n");
        goto cleanup;
    }
#ifdef DEBUG_ASN1
    print_buffer_bin((unsigned char *)td_certifiers->data,
                     td_certifiers->length, "/tmp/kdc_td_certifiers");
#endif
    pa_data = malloc(2 * sizeof(krb5_pa_data *));
    if (pa_data == NULL) {
        retval = ENOMEM;
        goto cleanup;
    }
    pa_data[1] = NULL;
    pa_data[0] = malloc(sizeof(krb5_pa_data));
    if (pa_data[0] == NULL) {
        retval = ENOMEM;
        goto cleanup;
    }
    pa_data[0]->pa_type = type;
    pa_data[0]->length = td_certifiers->length;
    pa_data[0]->contents = (krb5_octet *)td_certifiers->data;
    *e_data_out = pa_data;
    retval = 0;

cleanup:
    if (krb5_trusted_certifiers != NULL)
        free_krb5_external_principal_identifier(&krb5_trusted_certifiers);
    free(td_certifiers);
    return retval;
}

krb5_error_code
pkinit_create_td_trusted_certifiers(krb5_context context,
                                    pkinit_plg_crypto_context plg_cryptoctx,
                                    pkinit_req_crypto_context req_cryptoctx,
                                    pkinit_identity_crypto_context id_cryptoctx,
                                    krb5_pa_data ***e_data_out)
{
    krb5_error_code retval = KRB5KRB_ERR_GENERIC;

    retval = pkinit_create_sequence_of_principal_identifiers(context,
                                                             plg_cryptoctx, req_cryptoctx, id_cryptoctx,
                                                             TD_TRUSTED_CERTIFIERS, e_data_out);

    return retval;
}

krb5_error_code
pkinit_create_td_invalid_certificate(
    krb5_context context,
    pkinit_plg_crypto_context plg_cryptoctx,
    pkinit_req_crypto_context req_cryptoctx,
    pkinit_identity_crypto_context id_cryptoctx,
    krb5_pa_data ***e_data_out)
{
    krb5_error_code retval = KRB5KRB_ERR_GENERIC;

    retval = pkinit_create_sequence_of_principal_identifiers(context,
                                                             plg_cryptoctx, req_cryptoctx, id_cryptoctx,
                                                             TD_INVALID_CERTIFICATES, e_data_out);

    return retval;
}

krb5_error_code
pkinit_create_td_dh_parameters(krb5_context context,
                               pkinit_plg_crypto_context plg_cryptoctx,
                               pkinit_req_crypto_context req_cryptoctx,
                               pkinit_identity_crypto_context id_cryptoctx,
                               pkinit_plg_opts *opts,
                               krb5_pa_data ***e_data_out)
{
    krb5_error_code retval = ENOMEM;
    unsigned int buf1_len = 0, buf2_len = 0, buf3_len = 0, i = 0;
    unsigned char *buf1 = NULL, *buf2 = NULL, *buf3 = NULL;
    krb5_pa_data **pa_data = NULL;
    krb5_data *encoded_algId = NULL;
    krb5_algorithm_identifier **algId = NULL;

    if (opts->dh_min_bits > 4096)
        goto cleanup;

    if (opts->dh_min_bits <= 1024) {
        retval = pkinit_encode_dh_params(plg_cryptoctx->dh_1024->p,
                                         plg_cryptoctx->dh_1024->g, plg_cryptoctx->dh_1024->q,
                                         &buf1, &buf1_len);
        if (retval)
            goto cleanup;
    }
    if (opts->dh_min_bits <= 2048) {
        retval = pkinit_encode_dh_params(plg_cryptoctx->dh_2048->p,
                                         plg_cryptoctx->dh_2048->g, plg_cryptoctx->dh_2048->q,
                                         &buf2, &buf2_len);
        if (retval)
            goto cleanup;
    }
    retval = pkinit_encode_dh_params(plg_cryptoctx->dh_4096->p,
                                     plg_cryptoctx->dh_4096->g, plg_cryptoctx->dh_4096->q,
                                     &buf3, &buf3_len);
    if (retval)
        goto cleanup;

    if (opts->dh_min_bits <= 1024) {
        algId = malloc(4 * sizeof(krb5_algorithm_identifier *));
        if (algId == NULL)
            goto cleanup;
        algId[3] = NULL;
        algId[0] = malloc(sizeof(krb5_algorithm_identifier));
        if (algId[0] == NULL)
            goto cleanup;
        algId[0]->parameters.data = malloc(buf2_len);
        if (algId[0]->parameters.data == NULL)
            goto cleanup;
        memcpy(algId[0]->parameters.data, buf2, buf2_len);
        algId[0]->parameters.length = buf2_len;
        algId[0]->algorithm = dh_oid;

        algId[1] = malloc(sizeof(krb5_algorithm_identifier));
        if (algId[1] == NULL)
            goto cleanup;
        algId[1]->parameters.data = malloc(buf3_len);
        if (algId[1]->parameters.data == NULL)
            goto cleanup;
        memcpy(algId[1]->parameters.data, buf3, buf3_len);
        algId[1]->parameters.length = buf3_len;
        algId[1]->algorithm = dh_oid;

        algId[2] = malloc(sizeof(krb5_algorithm_identifier));
        if (algId[2] == NULL)
            goto cleanup;
        algId[2]->parameters.data = malloc(buf1_len);
        if (algId[2]->parameters.data == NULL)
            goto cleanup;
        memcpy(algId[2]->parameters.data, buf1, buf1_len);
        algId[2]->parameters.length = buf1_len;
        algId[2]->algorithm = dh_oid;

    } else if (opts->dh_min_bits <= 2048) {
        algId = malloc(3 * sizeof(krb5_algorithm_identifier *));
        if (algId == NULL)
            goto cleanup;
        algId[2] = NULL;
        algId[0] = malloc(sizeof(krb5_algorithm_identifier));
        if (algId[0] == NULL)
            goto cleanup;
        algId[0]->parameters.data = malloc(buf2_len);
        if (algId[0]->parameters.data == NULL)
            goto cleanup;
        memcpy(algId[0]->parameters.data, buf2, buf2_len);
        algId[0]->parameters.length = buf2_len;
        algId[0]->algorithm = dh_oid;

        algId[1] = malloc(sizeof(krb5_algorithm_identifier));
        if (algId[1] == NULL)
            goto cleanup;
        algId[1]->parameters.data = malloc(buf3_len);
        if (algId[1]->parameters.data == NULL)
            goto cleanup;
        memcpy(algId[1]->parameters.data, buf3, buf3_len);
        algId[1]->parameters.length = buf3_len;
        algId[1]->algorithm = dh_oid;

    } else if (opts->dh_min_bits <= 4096) {
        algId = malloc(2 * sizeof(krb5_algorithm_identifier *));
        if (algId == NULL)
            goto cleanup;
        algId[1] = NULL;
        algId[0] = malloc(sizeof(krb5_algorithm_identifier));
        if (algId[0] == NULL)
            goto cleanup;
        algId[0]->parameters.data = malloc(buf3_len);
        if (algId[0]->parameters.data == NULL)
            goto cleanup;
        memcpy(algId[0]->parameters.data, buf3, buf3_len);
        algId[0]->parameters.length = buf3_len;
        algId[0]->algorithm = dh_oid;

    }
    retval = k5int_encode_krb5_td_dh_parameters((const krb5_algorithm_identifier **)algId, &encoded_algId);
    if (retval)
        goto cleanup;
#ifdef DEBUG_ASN1
    print_buffer_bin((unsigned char *)encoded_algId->data,
                     encoded_algId->length, "/tmp/kdc_td_dh_params");
#endif
    pa_data = malloc(2 * sizeof(krb5_pa_data *));
    if (pa_data == NULL) {
        retval = ENOMEM;
        goto cleanup;
    }
    pa_data[1] = NULL;
    pa_data[0] = malloc(sizeof(krb5_pa_data));
    if (pa_data[0] == NULL) {
        retval = ENOMEM;
        goto cleanup;
    }
    pa_data[0]->pa_type = TD_DH_PARAMETERS;
    pa_data[0]->length = encoded_algId->length;
    pa_data[0]->contents = (krb5_octet *)encoded_algId->data;
    *e_data_out = pa_data;
    retval = 0;
cleanup:

    free(buf1);
    free(buf2);
    free(buf3);
    free(encoded_algId);

    if (algId != NULL) {
        while(algId[i] != NULL) {
            free(algId[i]->parameters.data);
            free(algId[i]);
            i++;
        }
        free(algId);
    }

    return retval;
}

krb5_error_code
pkinit_check_kdc_pkid(krb5_context context,
                      pkinit_plg_crypto_context plg_cryptoctx,
                      pkinit_req_crypto_context req_cryptoctx,
                      pkinit_identity_crypto_context id_cryptoctx,
                      unsigned char *pdid_buf,
                      unsigned int pkid_len,
                      int *valid_kdcPkId)
{
    krb5_error_code retval = KRB5KDC_ERR_PREAUTH_FAILED;
    PKCS7_ISSUER_AND_SERIAL *is = NULL;
    const unsigned char *p = pdid_buf;
    int status = 1;
    X509 *kdc_cert = sk_X509_value(id_cryptoctx->my_certs, id_cryptoctx->cert_index);

    *valid_kdcPkId = 0;
    pkiDebug("found kdcPkId in AS REQ\n");
    is = d2i_PKCS7_ISSUER_AND_SERIAL(NULL, &p, (int)pkid_len);
    if (is == NULL)
        return retval;

    status = X509_NAME_cmp(X509_get_issuer_name(kdc_cert), is->issuer);
    if (!status) {
        status = ASN1_INTEGER_cmp(X509_get_serialNumber(kdc_cert), is->serial);
        if (!status)
            *valid_kdcPkId = 1;
    }

    retval = 0;
    X509_NAME_free(is->issuer);
    ASN1_INTEGER_free(is->serial);
    free(is);

    return retval;
}

static int
pkinit_check_dh_params(BIGNUM * p1, BIGNUM * p2, BIGNUM * g1, BIGNUM * q1)
{
    BIGNUM *g2 = NULL, *q2 = NULL;
    int retval = -1;

    if (!BN_cmp(p1, p2)) {
        g2 = BN_new();
        BN_set_word(g2, DH_GENERATOR_2);
        if (!BN_cmp(g1, g2)) {
            q2 = BN_new();
            BN_rshift1(q2, p1);
            if (!BN_cmp(q1, q2)) {
                pkiDebug("good %d dhparams\n", BN_num_bits(p1));
                retval = 0;
            } else
                pkiDebug("bad group 2 q dhparameter\n");
            BN_free(q2);
        } else
            pkiDebug("bad g dhparameter\n");
        BN_free(g2);
    } else
        pkiDebug("p is not well-known group 2 dhparameter\n");

    return retval;
}

krb5_error_code
pkinit_process_td_dh_params(krb5_context context,
                            pkinit_plg_crypto_context cryptoctx,
                            pkinit_req_crypto_context req_cryptoctx,
                            pkinit_identity_crypto_context id_cryptoctx,
                            krb5_algorithm_identifier **algId,
                            int *new_dh_size)
{
    krb5_error_code retval = KRB5KDC_ERR_DH_KEY_PARAMETERS_NOT_ACCEPTED;
    int i = 0, use_sent_dh = 0, ok = 0;

    pkiDebug("dh parameters\n");

    while (algId[i] != NULL) {
        DH *dh = NULL;
        unsigned char *tmp = NULL;
        int dh_prime_bits = 0;

        if (algId[i]->algorithm.length != dh_oid.length ||
            memcmp(algId[i]->algorithm.data, dh_oid.data, dh_oid.length))
            goto cleanup;

        tmp = algId[i]->parameters.data;
        dh = DH_new();
        dh = pkinit_decode_dh_params(&dh, &tmp, algId[i]->parameters.length);
        dh_prime_bits = BN_num_bits(dh->p);
        pkiDebug("client sent %d DH bits server prefers %d DH bits\n",
                 *new_dh_size, dh_prime_bits);
        switch(dh_prime_bits) {
        case 1024:
            if (pkinit_check_dh_params(cryptoctx->dh_1024->p, dh->p,
                                       dh->g, dh->q) == 0) {
                *new_dh_size = 1024;
                ok = 1;
            }
            break;
        case 2048:
            if (pkinit_check_dh_params(cryptoctx->dh_2048->p, dh->p,
                                       dh->g, dh->q) == 0) {
                *new_dh_size = 2048;
                ok = 1;
            }
            break;
        case 4096:
            if (pkinit_check_dh_params(cryptoctx->dh_4096->p, dh->p,
                                       dh->g, dh->q) == 0) {
                *new_dh_size = 4096;
                ok = 1;
            }
            break;
        default:
            break;
        }
        if (!ok) {
            DH_check(dh, &retval);
            if (retval != 0) {
                pkiDebug("DH parameters provided by server are unacceptable\n");
                retval = KRB5KDC_ERR_DH_KEY_PARAMETERS_NOT_ACCEPTED;
            }
            else {
                use_sent_dh = 1;
                ok = 1;
            }
        }
        if (!use_sent_dh)
            DH_free(dh);
        if (ok) {
            if (req_cryptoctx->dh != NULL) {
                DH_free(req_cryptoctx->dh);
                req_cryptoctx->dh = NULL;
            }
            if (use_sent_dh)
                req_cryptoctx->dh = dh;
            break;
        }
        i++;
    }

    if (ok)
        retval = 0;

cleanup:
    return retval;
}

static int
openssl_callback(int ok, X509_STORE_CTX * ctx)
{
#ifdef DEBUG
    if (!ok) {
        char buf[DN_BUF_LEN];

        X509_NAME_oneline(X509_get_subject_name(ctx->current_cert), buf, sizeof(buf));
        pkiDebug("cert = %s\n", buf);
        pkiDebug("callback function: %d (%s)\n", ctx->error,
                 X509_verify_cert_error_string(ctx->error));
    }
#endif
    return ok;
}

static int
openssl_callback_ignore_crls(int ok, X509_STORE_CTX * ctx)
{
    if (!ok) {
        switch (ctx->error) {
        case X509_V_ERR_UNABLE_TO_GET_CRL:
            return 1;
        default:
            return 0;
        }
    }
    return ok;
}

static ASN1_OBJECT *
pkinit_pkcs7type2oid(pkinit_plg_crypto_context cryptoctx, int pkcs7_type)
{
    int nid;

    switch (pkcs7_type) {
    case CMS_SIGN_CLIENT:
        return cryptoctx->id_pkinit_authData;
    case CMS_SIGN_DRAFT9:
        /*
         * Delay creating this OID until we know we need it.
         * It shadows an existing OpenSSL oid.  If it
         * is created too early, it breaks things like
         * the use of pkcs12 (which uses pkcs7 structures).
         * We need this shadow version because our code
         * depends on the "other" type to be unknown to the
         * OpenSSL code.
         */
        if (cryptoctx->id_pkinit_authData9 == NULL) {
            pkiDebug("%s: Creating shadow instance of pkcs7-data oid\n",
                     __FUNCTION__);
            nid = OBJ_create("1.2.840.113549.1.7.1", "id-pkcs7-data",
                             "PKCS7 data");
            if (nid == NID_undef)
                return NULL;
            cryptoctx->id_pkinit_authData9 = OBJ_nid2obj(nid);
        }
        return cryptoctx->id_pkinit_authData9;
    case CMS_SIGN_SERVER:
        return cryptoctx->id_pkinit_DHKeyData;
    case CMS_ENVEL_SERVER:
        return cryptoctx->id_pkinit_rkeyData;
    default:
        return NULL;
    }

}

#ifdef LONGHORN_BETA_COMPAT
#if 0
/*
 * This is a version that worked with Longhorn Beta 3.
 */
static int
wrap_signeddata(unsigned char *data, unsigned int data_len,
                unsigned char **out, unsigned int *out_len,
                int is_longhorn_server)
{

    unsigned int orig_len = 0, oid_len = 0, tot_len = 0;
    ASN1_OBJECT *oid = NULL;
    unsigned char *p = NULL;

    pkiDebug("%s: This is the Longhorn version and is_longhorn_server = %d\n",
             __FUNCTION__, is_longhorn_server);

    /* Get length to wrap the original data with SEQUENCE tag */
    tot_len = orig_len = ASN1_object_size(1, (int)data_len, V_ASN1_SEQUENCE);

    if (is_longhorn_server == 0) {
        /* Add the signedData OID and adjust lengths */
        oid = OBJ_nid2obj(NID_pkcs7_signed);
        oid_len = i2d_ASN1_OBJECT(oid, NULL);

        tot_len = ASN1_object_size(1, (int)(orig_len+oid_len), V_ASN1_SEQUENCE);
    }

    p = *out = malloc(tot_len);
    if (p == NULL) return -1;

    if (is_longhorn_server == 0) {
        ASN1_put_object(&p, 1, (int)(orig_len+oid_len),
                        V_ASN1_SEQUENCE, V_ASN1_UNIVERSAL);

        i2d_ASN1_OBJECT(oid, &p);

        ASN1_put_object(&p, 1, (int)data_len, 0, V_ASN1_CONTEXT_SPECIFIC);
    } else {
        ASN1_put_object(&p, 1, (int)data_len, V_ASN1_SEQUENCE, V_ASN1_UNIVERSAL);
    }
    memcpy(p, data, data_len);

    *out_len = tot_len;

    return 0;
}
#else
/*
 * This is a version that works with a patched Longhorn KDC.
 * (Which should match SP1 ??).
 */
static int
wrap_signeddata(unsigned char *data, unsigned int data_len,
                unsigned char **out, unsigned int *out_len,
                int is_longhorn_server)
{

    unsigned int oid_len = 0, tot_len = 0, wrap_len = 0, tag_len = 0;
    ASN1_OBJECT *oid = NULL;
    unsigned char *p = NULL;

    pkiDebug("%s: This is the Longhorn version and is_longhorn_server = %d\n",
             __FUNCTION__, is_longhorn_server);

    /* New longhorn is missing another sequence */
    if (is_longhorn_server == 1)
        wrap_len = ASN1_object_size(1, (int)(data_len), V_ASN1_SEQUENCE);
    else
        wrap_len = data_len;

    /* Get length to wrap the original data with SEQUENCE tag */
    tag_len = ASN1_object_size(1, (int)wrap_len, V_ASN1_SEQUENCE);

    /* Always add oid */
    oid = OBJ_nid2obj(NID_pkcs7_signed);
    oid_len = i2d_ASN1_OBJECT(oid, NULL);
    oid_len += tag_len;

    tot_len = ASN1_object_size(1, (int)(oid_len), V_ASN1_SEQUENCE);

    p = *out = malloc(tot_len);
    if (p == NULL)
        return -1;

    ASN1_put_object(&p, 1, (int)(oid_len),
                    V_ASN1_SEQUENCE, V_ASN1_UNIVERSAL);

    i2d_ASN1_OBJECT(oid, &p);

    ASN1_put_object(&p, 1, (int)wrap_len, 0, V_ASN1_CONTEXT_SPECIFIC);

    /* Wrap in extra seq tag */
    if (is_longhorn_server == 1) {
        ASN1_put_object(&p, 1, (int)data_len, V_ASN1_SEQUENCE, V_ASN1_UNIVERSAL);
    }
    memcpy(p, data, data_len);

    *out_len = tot_len;

    return 0;
}

#endif
#else
static int
wrap_signeddata(unsigned char *data, unsigned int data_len,
                unsigned char **out, unsigned int *out_len)
{

    unsigned int orig_len = 0, oid_len = 0, tot_len = 0;
    ASN1_OBJECT *oid = NULL;
    unsigned char *p = NULL;

    /* Get length to wrap the original data with SEQUENCE tag */
    tot_len = orig_len = ASN1_object_size(1, (int)data_len, V_ASN1_SEQUENCE);

    /* Add the signedData OID and adjust lengths */
    oid = OBJ_nid2obj(NID_pkcs7_signed);
    oid_len = i2d_ASN1_OBJECT(oid, NULL);

    tot_len = ASN1_object_size(1, (int)(orig_len+oid_len), V_ASN1_SEQUENCE);

    p = *out = malloc(tot_len);
    if (p == NULL) return -1;

    ASN1_put_object(&p, 1, (int)(orig_len+oid_len),
                    V_ASN1_SEQUENCE, V_ASN1_UNIVERSAL);

    i2d_ASN1_OBJECT(oid, &p);

    ASN1_put_object(&p, 1, (int)data_len, 0, V_ASN1_CONTEXT_SPECIFIC);
    memcpy(p, data, data_len);

    *out_len = tot_len;

    return 0;
}
#endif

static int
prepare_enc_data(unsigned char *indata,
                 int indata_len,
                 unsigned char **outdata,
                 int *outdata_len)
{
    int retval = -1;
    ASN1_const_CTX c;
    long length = indata_len;
    int Ttag, Tclass;
    long Tlen;

    c.pp = (const unsigned char **)&indata;
    c.q = *(const unsigned char **)&indata;
    c.error = ERR_R_NESTED_ASN1_ERROR;
    c.p= *(const unsigned char **)&indata;
    c.max = (length == 0)?0:(c.p+length);

    asn1_GetSequence(&c,&length);

    ASN1_get_object(&c.p,&Tlen,&Ttag,&Tclass,c.slen);
    c.p += Tlen;
    ASN1_get_object(&c.p,&Tlen,&Ttag,&Tclass,c.slen);

    asn1_const_Finish(&c);

    *outdata = malloc((size_t)Tlen);
    if (outdata == NULL) {
        retval = ENOMEM;
        goto cleanup;
    }
    memcpy(*outdata, c.p, (size_t)Tlen);
    *outdata_len = Tlen;

    retval = 0;
cleanup:

    return retval;
}

#ifndef WITHOUT_PKCS11
static void *
pkinit_C_LoadModule(const char *modname, CK_FUNCTION_LIST_PTR_PTR p11p)
{
    void *handle;
    CK_RV (*getflist)(CK_FUNCTION_LIST_PTR_PTR);

    pkiDebug("loading module \"%s\"... ", modname);
    handle = dlopen(modname, RTLD_NOW);
    if (handle == NULL) {
        pkiDebug("not found\n");
        return NULL;
    }
    getflist = (CK_RV (*)(CK_FUNCTION_LIST_PTR_PTR)) dlsym(handle, "C_GetFunctionList");
    if (getflist == NULL || (*getflist)(p11p) != CKR_OK) {
        dlclose(handle);
        pkiDebug("failed\n");
        return NULL;
    }
    pkiDebug("ok\n");
    return handle;
}

static CK_RV
pkinit_C_UnloadModule(void *handle)
{
    dlclose(handle);
    return CKR_OK;
}

static krb5_error_code
pkinit_login(krb5_context context,
             pkinit_identity_crypto_context id_cryptoctx,
             CK_TOKEN_INFO *tip)
{
    krb5_data rdat;
    char *prompt;
    const char *warning;
    krb5_prompt kprompt;
    krb5_prompt_type prompt_type;
    int r = 0;

    if (tip->flags & CKF_PROTECTED_AUTHENTICATION_PATH) {
        rdat.data = NULL;
        rdat.length = 0;
    } else {
        if (tip->flags & CKF_USER_PIN_LOCKED)
            warning = " (Warning: PIN locked)";
        else if (tip->flags & CKF_USER_PIN_FINAL_TRY)
            warning = " (Warning: PIN final try)";
        else if (tip->flags & CKF_USER_PIN_COUNT_LOW)
            warning = " (Warning: PIN count low)";
        else
            warning = "";
        if (asprintf(&prompt, "%.*s PIN%s", (int) sizeof (tip->label),
                     tip->label, warning) < 0)
            return ENOMEM;
        rdat.data = malloc(tip->ulMaxPinLen + 2);
        rdat.length = tip->ulMaxPinLen + 1;

        kprompt.prompt = prompt;
        kprompt.hidden = 1;
        kprompt.reply = &rdat;
        prompt_type = KRB5_PROMPT_TYPE_PREAUTH;

        /* PROMPTER_INVOCATION */
        k5int_set_prompt_types(context, &prompt_type);
        r = (*id_cryptoctx->prompter)(context, id_cryptoctx->prompter_data,
                                      NULL, NULL, 1, &kprompt);
        k5int_set_prompt_types(context, 0);
        free(prompt);
    }

    if (r == 0) {
        r = id_cryptoctx->p11->C_Login(id_cryptoctx->session, CKU_USER,
                                       (u_char *) rdat.data, rdat.length);

        if (r != CKR_OK) {
            pkiDebug("C_Login: %s\n", pkinit_pkcs11_code_to_text(r));
            r = KRB5KDC_ERR_PREAUTH_FAILED;
        }
    }
    free(rdat.data);

    return r;
}

static krb5_error_code
pkinit_open_session(krb5_context context,
                    pkinit_identity_crypto_context cctx)
{
    CK_ULONG i, r;
    unsigned char *cp;
    CK_ULONG count = 0;
    CK_SLOT_ID_PTR slotlist;
    CK_TOKEN_INFO tinfo;

    if (cctx->p11_module != NULL)
        return 0; /* session already open */

    /* Load module */
    cctx->p11_module =
        pkinit_C_LoadModule(cctx->p11_module_name, &cctx->p11);
    if (cctx->p11_module == NULL)
        return KRB5KDC_ERR_PREAUTH_FAILED;

    /* Init */
    if ((r = cctx->p11->C_Initialize(NULL)) != CKR_OK) {
        pkiDebug("C_Initialize: %s\n", pkinit_pkcs11_code_to_text(r));
        return KRB5KDC_ERR_PREAUTH_FAILED;
    }

    /* Get the list of available slots */
    if (cctx->slotid != PK_NOSLOT) {
        /* A slot was specified, so that's the only one in the list */
        count = 1;
        slotlist = malloc(sizeof(CK_SLOT_ID));
        slotlist[0] = cctx->slotid;
    } else {
        if (cctx->p11->C_GetSlotList(TRUE, NULL, &count) != CKR_OK)
            return KRB5KDC_ERR_PREAUTH_FAILED;
        if (count == 0)
            return KRB5KDC_ERR_PREAUTH_FAILED;
        slotlist = malloc(count * sizeof (CK_SLOT_ID));
        if (cctx->p11->C_GetSlotList(TRUE, slotlist, &count) != CKR_OK)
            return KRB5KDC_ERR_PREAUTH_FAILED;
    }

    /* Look for the given token label, or if none given take the first one */
    for (i = 0; i < count; i++) {
        /* Open session */
        if ((r = cctx->p11->C_OpenSession(slotlist[i], CKF_SERIAL_SESSION,
                                          NULL, NULL, &cctx->session)) != CKR_OK) {
            pkiDebug("C_OpenSession: %s\n", pkinit_pkcs11_code_to_text(r));
            return KRB5KDC_ERR_PREAUTH_FAILED;
        }

        /* Get token info */
        if ((r = cctx->p11->C_GetTokenInfo(slotlist[i], &tinfo)) != CKR_OK) {
            pkiDebug("C_GetTokenInfo: %s\n", pkinit_pkcs11_code_to_text(r));
            return KRB5KDC_ERR_PREAUTH_FAILED;
        }
        for (cp = tinfo.label + sizeof (tinfo.label) - 1;
             *cp == '\0' || *cp == ' '; cp--)
            *cp = '\0';
        pkiDebug("open_session: slotid %d token \"%s\"\n",
                 (int) slotlist[i], tinfo.label);
        if (cctx->token_label == NULL ||
            !strcmp((char *) cctx->token_label, (char *) tinfo.label))
            break;
        cctx->p11->C_CloseSession(cctx->session);
    }
    if (i >= count) {
        free(slotlist);
        pkiDebug("open_session: no matching token found\n");
        return KRB5KDC_ERR_PREAUTH_FAILED;
    }
    cctx->slotid = slotlist[i];
    free(slotlist);
    pkiDebug("open_session: slotid %d (%d of %d)\n", (int) cctx->slotid,
             i + 1, (int) count);

    /* Login if needed */
    if (tinfo.flags & CKF_LOGIN_REQUIRED)
        r = pkinit_login(context, cctx, &tinfo);

    return r;
}

/*
 * Look for a key that's:
 * 1. private
 * 2. capable of the specified operation (usually signing or decrypting)
 * 3. RSA (this may be wrong but it's all we can do for now)
 * 4. matches the id of the cert we chose
 *
 * You must call pkinit_get_certs before calling pkinit_find_private_key
 * (that's because we need the ID of the private key)
 *
 * pkcs11 says the id of the key doesn't have to match that of the cert, but
 * I can't figure out any other way to decide which key to use.
 *
 * We should only find one key that fits all the requirements.
 * If there are more than one, we just take the first one.
 */

krb5_error_code
pkinit_find_private_key(pkinit_identity_crypto_context id_cryptoctx,
                        CK_ATTRIBUTE_TYPE usage,
                        CK_OBJECT_HANDLE *objp)
{
    CK_OBJECT_CLASS cls;
    CK_ATTRIBUTE attrs[4];
    CK_ULONG count;
    CK_KEY_TYPE keytype;
    unsigned int nattrs = 0;
    int r;
#ifdef PKINIT_USE_KEY_USAGE
    CK_BBOOL true_false;
#endif

    cls = CKO_PRIVATE_KEY;
    attrs[nattrs].type = CKA_CLASS;
    attrs[nattrs].pValue = &cls;
    attrs[nattrs].ulValueLen = sizeof cls;
    nattrs++;

#ifdef PKINIT_USE_KEY_USAGE
    /*
     * Some cards get confused if you try to specify a key usage,
     * so don't, and hope for the best. This will fail if you have
     * several keys with the same id and different usages but I have
     * not seen this on real cards.
     */
    true_false = TRUE;
    attrs[nattrs].type = usage;
    attrs[nattrs].pValue = &true_false;
    attrs[nattrs].ulValueLen = sizeof true_false;
    nattrs++;
#endif

    keytype = CKK_RSA;
    attrs[nattrs].type = CKA_KEY_TYPE;
    attrs[nattrs].pValue = &keytype;
    attrs[nattrs].ulValueLen = sizeof keytype;
    nattrs++;

    attrs[nattrs].type = CKA_ID;
    attrs[nattrs].pValue = id_cryptoctx->cert_id;
    attrs[nattrs].ulValueLen = id_cryptoctx->cert_id_len;
    nattrs++;

    r = id_cryptoctx->p11->C_FindObjectsInit(id_cryptoctx->session, attrs, nattrs);
    if (r != CKR_OK) {
        pkiDebug("krb5_pkinit_sign_data: C_FindObjectsInit: %s\n",
                 pkinit_pkcs11_code_to_text(r));
        return KRB5KDC_ERR_PREAUTH_FAILED;
    }

    r = id_cryptoctx->p11->C_FindObjects(id_cryptoctx->session, objp, 1, &count);
    id_cryptoctx->p11->C_FindObjectsFinal(id_cryptoctx->session);
    pkiDebug("found %d private keys (%s)\n", (int) count, pkinit_pkcs11_code_to_text(r));
    if (r != CKR_OK || count < 1)
        return KRB5KDC_ERR_PREAUTH_FAILED;
    return 0;
}
#endif

static krb5_error_code
pkinit_decode_data_fs(krb5_context context,
                      pkinit_identity_crypto_context id_cryptoctx,
                      unsigned char *data,
                      unsigned int data_len,
                      unsigned char **decoded_data,
                      unsigned int *decoded_data_len)
{
    if (decode_data(decoded_data, decoded_data_len, data, data_len,
                    id_cryptoctx->my_key, sk_X509_value(id_cryptoctx->my_certs,
                                                        id_cryptoctx->cert_index)) <= 0) {
        pkiDebug("failed to decode data\n");
        return KRB5KDC_ERR_PREAUTH_FAILED;
    }
    return 0;
}

#ifndef WITHOUT_PKCS11
/*
 * When using the ActivCard Linux pkcs11 library (v2.0.1), the decrypt function
 * fails.  By inserting an extra function call, which serves nothing but to
 * change the stack, we were able to work around the issue.  If the ActivCard
 * library is fixed in the future, this function can be inlined back into the
 * caller.
 */
static CK_RV
pkinit_C_Decrypt(pkinit_identity_crypto_context id_cryptoctx,
                 CK_BYTE_PTR pEncryptedData,
                 CK_ULONG  ulEncryptedDataLen,
                 CK_BYTE_PTR pData,
                 CK_ULONG_PTR pulDataLen)
{
    CK_RV rv = CKR_OK;

    rv = id_cryptoctx->p11->C_Decrypt(id_cryptoctx->session, pEncryptedData,
                                      ulEncryptedDataLen, pData, pulDataLen);
    if (rv == CKR_OK) {
        pkiDebug("pData %p *pulDataLen %d\n", (void *) pData,
                 (int) *pulDataLen);
    }
    return rv;
}

static krb5_error_code
pkinit_decode_data_pkcs11(krb5_context context,
                          pkinit_identity_crypto_context id_cryptoctx,
                          unsigned char *data,
                          unsigned int data_len,
                          unsigned char **decoded_data,
                          unsigned int *decoded_data_len)
{
    CK_OBJECT_HANDLE obj;
    CK_ULONG len;
    CK_MECHANISM mech;
    unsigned char *cp;
    int r;

    if (pkinit_open_session(context, id_cryptoctx)) {
        pkiDebug("can't open pkcs11 session\n");
        return KRB5KDC_ERR_PREAUTH_FAILED;
    }

    pkinit_find_private_key(id_cryptoctx, CKA_DECRYPT, &obj);

    mech.mechanism = CKM_RSA_PKCS;
    mech.pParameter = NULL;
    mech.ulParameterLen = 0;

    if ((r = id_cryptoctx->p11->C_DecryptInit(id_cryptoctx->session, &mech,
                                              obj)) != CKR_OK) {
        pkiDebug("C_DecryptInit: 0x%x\n", (int) r);
        return KRB5KDC_ERR_PREAUTH_FAILED;
    }
    pkiDebug("data_len = %d\n", data_len);
    cp = malloc((size_t) data_len);
    if (cp == NULL)
        return ENOMEM;
    len = data_len;
    pkiDebug("session %p edata %p edata_len %d data %p datalen @%p %d\n",
             (void *) id_cryptoctx->session, (void *) data, (int) data_len,
             (void *) cp, (void *) &len, (int) len);
    if ((r = pkinit_C_Decrypt(id_cryptoctx, data, (CK_ULONG) data_len,
                              cp, &len)) != CKR_OK) {
        pkiDebug("C_Decrypt: %s\n", pkinit_pkcs11_code_to_text(r));
        if (r == CKR_BUFFER_TOO_SMALL)
            pkiDebug("decrypt %d needs %d\n", (int) data_len, (int) len);
        return KRB5KDC_ERR_PREAUTH_FAILED;
    }
    pkiDebug("decrypt %d -> %d\n", (int) data_len, (int) len);
    *decoded_data_len = len;
    *decoded_data = cp;

    return 0;
}
#endif

krb5_error_code
pkinit_decode_data(krb5_context context,
                   pkinit_identity_crypto_context id_cryptoctx,
                   unsigned char *data,
                   unsigned int data_len,
                   unsigned char **decoded_data,
                   unsigned int *decoded_data_len)
{
    krb5_error_code retval = KRB5KDC_ERR_PREAUTH_FAILED;

    if (id_cryptoctx->pkcs11_method != 1)
        retval = pkinit_decode_data_fs(context, id_cryptoctx, data, data_len,
                                       decoded_data, decoded_data_len);
#ifndef WITHOUT_PKCS11
    else
        retval = pkinit_decode_data_pkcs11(context, id_cryptoctx, data,
                                           data_len, decoded_data, decoded_data_len);
#endif

    return retval;
}

static krb5_error_code
pkinit_sign_data_fs(krb5_context context,
                    pkinit_identity_crypto_context id_cryptoctx,
                    unsigned char *data,
                    unsigned int data_len,
                    unsigned char **sig,
                    unsigned int *sig_len)
{
    if (create_signature(sig, sig_len, data, data_len,
                         id_cryptoctx->my_key) != 0) {
        pkiDebug("failed to create the signature\n");
        return KRB5KDC_ERR_PREAUTH_FAILED;
    }
    return 0;
}

#ifndef WITHOUT_PKCS11
static krb5_error_code
pkinit_sign_data_pkcs11(krb5_context context,
                        pkinit_identity_crypto_context id_cryptoctx,
                        unsigned char *data,
                        unsigned int data_len,
                        unsigned char **sig,
                        unsigned int *sig_len)
{
    CK_OBJECT_HANDLE obj;
    CK_ULONG len;
    CK_MECHANISM mech;
    unsigned char *cp;
    int r;

    if (pkinit_open_session(context, id_cryptoctx)) {
        pkiDebug("can't open pkcs11 session\n");
        return KRB5KDC_ERR_PREAUTH_FAILED;
    }

    pkinit_find_private_key(id_cryptoctx, CKA_SIGN, &obj);

    mech.mechanism = id_cryptoctx->mech;
    mech.pParameter = NULL;
    mech.ulParameterLen = 0;

    if ((r = id_cryptoctx->p11->C_SignInit(id_cryptoctx->session, &mech,
                                           obj)) != CKR_OK) {
        pkiDebug("C_SignInit: %s\n", pkinit_pkcs11_code_to_text(r));
        return KRB5KDC_ERR_PREAUTH_FAILED;
    }

    /*
     * Key len would give an upper bound on sig size, but there's no way to
     * get that. So guess, and if it's too small, re-malloc.
     */
    len = PK_SIGLEN_GUESS;
    cp = malloc((size_t) len);
    if (cp == NULL)
        return ENOMEM;

    r = id_cryptoctx->p11->C_Sign(id_cryptoctx->session, data,
                                  (CK_ULONG) data_len, cp, &len);
    if (r == CKR_BUFFER_TOO_SMALL || (r == CKR_OK && len >= PK_SIGLEN_GUESS)) {
        free(cp);
        pkiDebug("C_Sign realloc %d\n", (int) len);
        cp = malloc((size_t) len);
        r = id_cryptoctx->p11->C_Sign(id_cryptoctx->session, data,
                                      (CK_ULONG) data_len, cp, &len);
    }
    if (r != CKR_OK) {
        pkiDebug("C_Sign: %s\n", pkinit_pkcs11_code_to_text(r));
        return KRB5KDC_ERR_PREAUTH_FAILED;
    }
    pkiDebug("sign %d -> %d\n", (int) data_len, (int) len);
    *sig_len = len;
    *sig = cp;

    return 0;
}
#endif

krb5_error_code
pkinit_sign_data(krb5_context context,
                 pkinit_identity_crypto_context id_cryptoctx,
                 unsigned char *data,
                 unsigned int data_len,
                 unsigned char **sig,
                 unsigned int *sig_len)
{
    krb5_error_code retval = KRB5KDC_ERR_PREAUTH_FAILED;

    if (id_cryptoctx == NULL || id_cryptoctx->pkcs11_method != 1)
        retval = pkinit_sign_data_fs(context, id_cryptoctx, data, data_len,
                                     sig, sig_len);
#ifndef WITHOUT_PKCS11
    else
        retval = pkinit_sign_data_pkcs11(context, id_cryptoctx, data, data_len,
                                         sig, sig_len);
#endif

    return retval;
}


static krb5_error_code
decode_data(unsigned char **out_data, unsigned int *out_data_len,
            unsigned char *data, unsigned int data_len,
            EVP_PKEY *pkey, X509 *cert)
{
    krb5_error_code retval = ENOMEM;
    unsigned char *buf = NULL;
    int buf_len = 0;

    if (cert && !X509_check_private_key(cert, pkey)) {
        pkiDebug("private key does not match certificate\n");
        goto cleanup;
    }

    buf_len = EVP_PKEY_size(pkey);
    buf = malloc((size_t) buf_len + 10);
    if (buf == NULL)
        goto cleanup;

#if OPENSSL_VERSION_NUMBER >= 0x00909000L
    retval = EVP_PKEY_decrypt_old(buf, data, (int)data_len, pkey);
#else
    retval = EVP_PKEY_decrypt(buf, data, (int)data_len, pkey);
#endif
    if (retval <= 0) {
        pkiDebug("unable to decrypt received data (len=%d)\n", data_len);
        goto cleanup;
    }
    *out_data = buf;
    *out_data_len = retval;

cleanup:
    if (retval == ENOMEM)
        free(buf);

    return retval;
}

static krb5_error_code
create_signature(unsigned char **sig, unsigned int *sig_len,
                 unsigned char *data, unsigned int data_len, EVP_PKEY *pkey)
{
    krb5_error_code retval = ENOMEM;
    EVP_MD_CTX md_ctx;

    if (pkey == NULL)
        return retval;

    EVP_VerifyInit(&md_ctx, EVP_sha1());
    EVP_SignUpdate(&md_ctx, data, data_len);
    *sig_len = EVP_PKEY_size(pkey);
    if ((*sig = malloc(*sig_len)) == NULL)
        goto cleanup;
    EVP_SignFinal(&md_ctx, *sig, sig_len, pkey);

    retval = 0;

cleanup:
    EVP_MD_CTX_cleanup(&md_ctx);

    return retval;
}

/*
 * Note:
 * This is not the routine the KDC uses to get its certificate.
 * This routine is intended to be called by the client
 * to obtain the KDC's certificate from some local storage
 * to be sent as a hint in its request to the KDC.
 */
krb5_error_code
pkinit_get_kdc_cert(krb5_context context,
                    pkinit_plg_crypto_context plg_cryptoctx,
                    pkinit_req_crypto_context req_cryptoctx,
                    pkinit_identity_crypto_context id_cryptoctx,
                    krb5_principal princ)
{
    krb5_error_code retval = KRB5KDC_ERR_PREAUTH_FAILED;

    req_cryptoctx->received_cert = NULL;
    retval = 0;
    return retval;
}

static krb5_error_code
pkinit_get_certs_pkcs12(krb5_context context,
                        pkinit_plg_crypto_context plg_cryptoctx,
                        pkinit_req_crypto_context req_cryptoctx,
                        pkinit_identity_opts *idopts,
                        pkinit_identity_crypto_context id_cryptoctx,
                        krb5_principal princ)
{
    krb5_error_code retval = KRB5KDC_ERR_PREAUTH_FAILED;
    X509 *x = NULL;
    PKCS12 *p12 = NULL;
    int ret;
    FILE *fp;
    EVP_PKEY *y = NULL;

    if (idopts->cert_filename == NULL) {
        pkiDebug("%s: failed to get user's cert location\n", __FUNCTION__);
        goto cleanup;
    }

    if (idopts->key_filename == NULL) {
        pkiDebug("%s: failed to get user's private key location\n", __FUNCTION__);
        goto cleanup;
    }

    fp = fopen(idopts->cert_filename, "rb");
    if (fp == NULL) {
        pkiDebug("Failed to open PKCS12 file '%s', error %d\n",
                 idopts->cert_filename, errno);
        goto cleanup;
    }
    set_cloexec_file(fp);

    p12 = d2i_PKCS12_fp(fp, NULL);
    fclose(fp);
    if (p12 == NULL) {
        pkiDebug("Failed to decode PKCS12 file '%s' contents\n",
                 idopts->cert_filename);
        goto cleanup;
    }
    /*
     * Try parsing with no pass phrase first.  If that fails,
     * prompt for the pass phrase and try again.
     */
    ret = PKCS12_parse(p12, NULL, &y, &x, NULL);
    if (ret == 0) {
        krb5_data rdat;
        krb5_prompt kprompt;
        krb5_prompt_type prompt_type;
        int r = 0;
        char prompt_string[128];
        char prompt_reply[128];
        char *prompt_prefix = _("Pass phrase for");

        pkiDebug("Initial PKCS12_parse with no password failed\n");

        memset(prompt_reply, '\0', sizeof(prompt_reply));
        rdat.data = prompt_reply;
        rdat.length = sizeof(prompt_reply);

        r = snprintf(prompt_string, sizeof(prompt_string), "%s %s",
                     prompt_prefix, idopts->cert_filename);
        if (r >= (int) sizeof(prompt_string)) {
            pkiDebug("Prompt string, '%s %s', is too long!\n",
                     prompt_prefix, idopts->cert_filename);
            goto cleanup;
        }
        kprompt.prompt = prompt_string;
        kprompt.hidden = 1;
        kprompt.reply = &rdat;
        prompt_type = KRB5_PROMPT_TYPE_PREAUTH;

        /* PROMPTER_INVOCATION */
        k5int_set_prompt_types(context, &prompt_type);
        r = (*id_cryptoctx->prompter)(context, id_cryptoctx->prompter_data,
                                      NULL, NULL, 1, &kprompt);
        k5int_set_prompt_types(context, 0);

        ret = PKCS12_parse(p12, rdat.data, &y, &x, NULL);
        if (ret == 0) {
            pkiDebug("Seconde PKCS12_parse with password failed\n");
            goto cleanup;
        }
    }
    id_cryptoctx->creds[0] = malloc(sizeof(struct _pkinit_cred_info));
    if (id_cryptoctx->creds[0] == NULL)
        goto cleanup;
    id_cryptoctx->creds[0]->cert = x;
#ifndef WITHOUT_PKCS11
    id_cryptoctx->creds[0]->cert_id = NULL;
    id_cryptoctx->creds[0]->cert_id_len = 0;
#endif
    id_cryptoctx->creds[0]->key = y;
    id_cryptoctx->creds[1] = NULL;

    retval = 0;

cleanup:
    if (p12)
        PKCS12_free(p12);
    if (retval) {
        if (x != NULL)
            X509_free(x);
        if (y != NULL)
            EVP_PKEY_free(y);
    }
    return retval;
}

static krb5_error_code
pkinit_load_fs_cert_and_key(krb5_context context,
                            pkinit_identity_crypto_context id_cryptoctx,
                            char *certname,
                            char *keyname,
                            int cindex)
{
    krb5_error_code retval;
    X509 *x = NULL;
    EVP_PKEY *y = NULL;

    /* load the certificate */
    retval = get_cert(certname, &x);
    if (retval != 0 || x == NULL) {
        pkiDebug("failed to load user's certificate from '%s'\n", certname);
        goto cleanup;
    }
    retval = get_key(keyname, &y);
    if (retval != 0 || y == NULL) {
        pkiDebug("failed to load user's private key from '%s'\n", keyname);
        goto cleanup;
    }

    id_cryptoctx->creds[cindex] = malloc(sizeof(struct _pkinit_cred_info));
    if (id_cryptoctx->creds[cindex] == NULL) {
        retval = ENOMEM;
        goto cleanup;
    }
    id_cryptoctx->creds[cindex]->cert = x;
#ifndef WITHOUT_PKCS11
    id_cryptoctx->creds[cindex]->cert_id = NULL;
    id_cryptoctx->creds[cindex]->cert_id_len = 0;
#endif
    id_cryptoctx->creds[cindex]->key = y;
    id_cryptoctx->creds[cindex+1] = NULL;

    retval = 0;

cleanup:
    if (retval) {
        if (x != NULL)
            X509_free(x);
        if (y != NULL)
            EVP_PKEY_free(y);
    }
    return retval;
}

static krb5_error_code
pkinit_get_certs_fs(krb5_context context,
                    pkinit_plg_crypto_context plg_cryptoctx,
                    pkinit_req_crypto_context req_cryptoctx,
                    pkinit_identity_opts *idopts,
                    pkinit_identity_crypto_context id_cryptoctx,
                    krb5_principal princ)
{
    krb5_error_code retval = KRB5KDC_ERR_PREAUTH_FAILED;

    if (idopts->cert_filename == NULL) {
        pkiDebug("%s: failed to get user's cert location\n", __FUNCTION__);
        goto cleanup;
    }

    if (idopts->key_filename == NULL) {
        pkiDebug("%s: failed to get user's private key location\n",
                 __FUNCTION__);
        goto cleanup;
    }

    retval = pkinit_load_fs_cert_and_key(context, id_cryptoctx,
                                         idopts->cert_filename,
                                         idopts->key_filename, 0);
cleanup:
    return retval;
}

static krb5_error_code
pkinit_get_certs_dir(krb5_context context,
                     pkinit_plg_crypto_context plg_cryptoctx,
                     pkinit_req_crypto_context req_cryptoctx,
                     pkinit_identity_opts *idopts,
                     pkinit_identity_crypto_context id_cryptoctx,
                     krb5_principal princ)
{
    krb5_error_code retval = ENOMEM;
    DIR *d = NULL;
    struct dirent *dentry = NULL;
    char certname[1024];
    char keyname[1024];
    int i = 0, len;
    char *dirname, *suf;

    if (idopts->cert_filename == NULL) {
        pkiDebug("%s: failed to get user's certificate directory location\n",
                 __FUNCTION__);
        return ENOENT;
    }

    dirname = idopts->cert_filename;
    d = opendir(dirname);
    if (d == NULL)
        return errno;

    /*
     * We'll assume that certs are named XXX.crt and the corresponding
     * key is named XXX.key
     */
    while ((i < MAX_CREDS_ALLOWED) &&  (dentry = readdir(d)) != NULL) {
        /* Ignore subdirectories and anything starting with a dot */
#ifdef DT_DIR
        if (dentry->d_type == DT_DIR)
            continue;
#endif
        if (dentry->d_name[0] == '.')
            continue;
        len = strlen(dentry->d_name);
        if (len < 5)
            continue;
        suf = dentry->d_name + (len - 4);
        if (strncmp(suf, ".crt", 4) != 0)
            continue;

        /* Checked length */
        if (strlen(dirname) + strlen(dentry->d_name) + 2 > sizeof(certname)) {
            pkiDebug("%s: Path too long -- directory '%s' and file '%s'\n",
                     __FUNCTION__, dirname, dentry->d_name);
            continue;
        }
        snprintf(certname, sizeof(certname), "%s/%s", dirname, dentry->d_name);
        snprintf(keyname, sizeof(keyname), "%s/%s", dirname, dentry->d_name);
        len = strlen(keyname);
        keyname[len - 3] = 'k';
        keyname[len - 2] = 'e';
        keyname[len - 1] = 'y';

        retval = pkinit_load_fs_cert_and_key(context, id_cryptoctx,
                                             certname, keyname, i);
        if (retval == 0) {
            pkiDebug("%s: Successfully loaded cert (and key) for %s\n",
                     __FUNCTION__, dentry->d_name);
            i++;
        }
        else
            continue;
    }

    if (i == 0) {
        pkiDebug("%s: No cert/key pairs found in directory '%s'\n",
                 __FUNCTION__, idopts->cert_filename);
        retval = ENOENT;
        goto cleanup;
    }

    retval = 0;

cleanup:
    if (d)
        closedir(d);

    return retval;
}

#ifndef WITHOUT_PKCS11
static krb5_error_code
pkinit_get_certs_pkcs11(krb5_context context,
                        pkinit_plg_crypto_context plg_cryptoctx,
                        pkinit_req_crypto_context req_cryptoctx,
                        pkinit_identity_opts *idopts,
                        pkinit_identity_crypto_context id_cryptoctx,
                        krb5_principal princ)
{
#ifdef PKINIT_USE_MECH_LIST
    CK_MECHANISM_TYPE_PTR mechp;
    CK_MECHANISM_INFO info;
#endif
    CK_OBJECT_CLASS cls;
    CK_OBJECT_HANDLE obj;
    CK_ATTRIBUTE attrs[4];
    CK_ULONG count;
    CK_CERTIFICATE_TYPE certtype;
    CK_BYTE_PTR cert = NULL, cert_id;
    const unsigned char *cp;
    int i, r;
    unsigned int nattrs;
    X509 *x = NULL;

    /* Copy stuff from idopts -> id_cryptoctx */
    if (idopts->p11_module_name != NULL) {
        id_cryptoctx->p11_module_name = strdup(idopts->p11_module_name);
        if (id_cryptoctx->p11_module_name == NULL)
            return ENOMEM;
    }
    if (idopts->token_label != NULL) {
        id_cryptoctx->token_label = strdup(idopts->token_label);
        if (id_cryptoctx->token_label == NULL)
            return ENOMEM;
    }
    if (idopts->cert_label != NULL) {
        id_cryptoctx->cert_label = strdup(idopts->cert_label);
        if (id_cryptoctx->cert_label == NULL)
            return ENOMEM;
    }
    /* Convert the ascii cert_id string into a binary blob */
    if (idopts->cert_id_string != NULL) {
        BIGNUM *bn = NULL;
        BN_hex2bn(&bn, idopts->cert_id_string);
        if (bn == NULL)
            return ENOMEM;
        id_cryptoctx->cert_id_len = BN_num_bytes(bn);
        id_cryptoctx->cert_id = malloc((size_t) id_cryptoctx->cert_id_len);
        if (id_cryptoctx->cert_id == NULL) {
            BN_free(bn);
            return ENOMEM;
        }
        BN_bn2bin(bn, id_cryptoctx->cert_id);
        BN_free(bn);
    }
    id_cryptoctx->slotid = idopts->slotid;
    id_cryptoctx->pkcs11_method = 1;



    if (pkinit_open_session(context, id_cryptoctx)) {
        pkiDebug("can't open pkcs11 session\n");
        return KRB5KDC_ERR_PREAUTH_FAILED;
    }

#ifndef PKINIT_USE_MECH_LIST
    /*
     * We'd like to use CKM_SHA1_RSA_PKCS for signing if it's available, but
     * many cards seems to be confused about whether they are capable of
     * this or not. The safe thing seems to be to ignore the mechanism list,
     * always use CKM_RSA_PKCS and calculate the sha1 digest ourselves.
     */

    id_cryptoctx->mech = CKM_RSA_PKCS;
#else
    if ((r = id_cryptoctx->p11->C_GetMechanismList(id_cryptoctx->slotid, NULL,
                                                   &count)) != CKR_OK || count <= 0) {
        pkiDebug("C_GetMechanismList: %s\n", pkinit_pkcs11_code_to_text(r));
        return KRB5KDC_ERR_PREAUTH_FAILED;
    }
    mechp = malloc(count * sizeof (CK_MECHANISM_TYPE));
    if (mechp == NULL)
        return ENOMEM;
    if ((r = id_cryptoctx->p11->C_GetMechanismList(id_cryptoctx->slotid,
                                                   mechp, &count)) != CKR_OK)
        return KRB5KDC_ERR_PREAUTH_FAILED;
    for (i = 0; i < count; i++) {
        if ((r = id_cryptoctx->p11->C_GetMechanismInfo(id_cryptoctx->slotid,
                                                       mechp[i], &info)) != CKR_OK)
            return KRB5KDC_ERR_PREAUTH_FAILED;
#ifdef DEBUG_MECHINFO
        pkiDebug("mech %x flags %x\n", (int) mechp[i], (int) info.flags);
        if ((info.flags & (CKF_SIGN|CKF_DECRYPT)) == (CKF_SIGN|CKF_DECRYPT))
            pkiDebug("  this mech is good for sign & decrypt\n");
#endif
        if (mechp[i] == CKM_RSA_PKCS) {
            /* This seems backwards... */
            id_cryptoctx->mech =
                (info.flags & CKF_SIGN) ? CKM_SHA1_RSA_PKCS : CKM_RSA_PKCS;
        }
    }
    free(mechp);

    pkiDebug("got %d mechs from card\n", (int) count);
#endif

    cls = CKO_CERTIFICATE;
    attrs[0].type = CKA_CLASS;
    attrs[0].pValue = &cls;
    attrs[0].ulValueLen = sizeof cls;

    certtype = CKC_X_509;
    attrs[1].type = CKA_CERTIFICATE_TYPE;
    attrs[1].pValue = &certtype;
    attrs[1].ulValueLen = sizeof certtype;

    nattrs = 2;

    /* If a cert id and/or label were given, use them too */
    if (id_cryptoctx->cert_id_len > 0) {
        attrs[nattrs].type = CKA_ID;
        attrs[nattrs].pValue = id_cryptoctx->cert_id;
        attrs[nattrs].ulValueLen = id_cryptoctx->cert_id_len;
        nattrs++;
    }
    if (id_cryptoctx->cert_label != NULL) {
        attrs[nattrs].type = CKA_LABEL;
        attrs[nattrs].pValue = id_cryptoctx->cert_label;
        attrs[nattrs].ulValueLen = strlen(id_cryptoctx->cert_label);
        nattrs++;
    }

    r = id_cryptoctx->p11->C_FindObjectsInit(id_cryptoctx->session, attrs, nattrs);
    if (r != CKR_OK) {
        pkiDebug("C_FindObjectsInit: %s\n", pkinit_pkcs11_code_to_text(r));
        return KRB5KDC_ERR_PREAUTH_FAILED;
    }

    for (i = 0; ; i++) {
        if (i >= MAX_CREDS_ALLOWED)
            return KRB5KDC_ERR_PREAUTH_FAILED;

        /* Look for x.509 cert */
        if ((r = id_cryptoctx->p11->C_FindObjects(id_cryptoctx->session,
                                                  &obj, 1, &count)) != CKR_OK || count <= 0) {
            id_cryptoctx->creds[i] = NULL;
            break;
        }

        /* Get cert and id len */
        attrs[0].type = CKA_VALUE;
        attrs[0].pValue = NULL;
        attrs[0].ulValueLen = 0;

        attrs[1].type = CKA_ID;
        attrs[1].pValue = NULL;
        attrs[1].ulValueLen = 0;

        if ((r = id_cryptoctx->p11->C_GetAttributeValue(id_cryptoctx->session,
                                                        obj, attrs, 2)) != CKR_OK && r != CKR_BUFFER_TOO_SMALL) {
            pkiDebug("C_GetAttributeValue: %s\n", pkinit_pkcs11_code_to_text(r));
            return KRB5KDC_ERR_PREAUTH_FAILED;
        }
        cert = (CK_BYTE_PTR) malloc((size_t) attrs[0].ulValueLen + 1);
        cert_id = (CK_BYTE_PTR) malloc((size_t) attrs[1].ulValueLen + 1);
        if (cert == NULL || cert_id == NULL)
            return ENOMEM;

        /* Read the cert and id off the card */

        attrs[0].type = CKA_VALUE;
        attrs[0].pValue = cert;

        attrs[1].type = CKA_ID;
        attrs[1].pValue = cert_id;

        if ((r = id_cryptoctx->p11->C_GetAttributeValue(id_cryptoctx->session,
                                                        obj, attrs, 2)) != CKR_OK) {
            pkiDebug("C_GetAttributeValue: %s\n", pkinit_pkcs11_code_to_text(r));
            return KRB5KDC_ERR_PREAUTH_FAILED;
        }

        pkiDebug("cert %d size %d id %d idlen %d\n", i,
                 (int) attrs[0].ulValueLen, (int) cert_id[0],
                 (int) attrs[1].ulValueLen);

        cp = (unsigned char *) cert;
        x = d2i_X509(NULL, &cp, (int) attrs[0].ulValueLen);
        if (x == NULL)
            return KRB5KDC_ERR_PREAUTH_FAILED;
        id_cryptoctx->creds[i] = malloc(sizeof(struct _pkinit_cred_info));
        if (id_cryptoctx->creds[i] == NULL)
            return KRB5KDC_ERR_PREAUTH_FAILED;
        id_cryptoctx->creds[i]->cert = x;
        id_cryptoctx->creds[i]->key = NULL;
        id_cryptoctx->creds[i]->cert_id = cert_id;
        id_cryptoctx->creds[i]->cert_id_len = attrs[1].ulValueLen;
        free(cert);
    }
    id_cryptoctx->p11->C_FindObjectsFinal(id_cryptoctx->session);
    if (cert == NULL)
        return KRB5KDC_ERR_PREAUTH_FAILED;
    return 0;
}
#endif


static void
free_cred_info(krb5_context context,
               pkinit_identity_crypto_context id_cryptoctx,
               struct _pkinit_cred_info *cred)
{
    if (cred != NULL) {
        if (cred->cert != NULL)
            X509_free(cred->cert);
        if (cred->key != NULL)
            EVP_PKEY_free(cred->key);
#ifndef WITHOUT_PKCS11
        free(cred->cert_id);
#endif
        free(cred);
    }
}

krb5_error_code
crypto_free_cert_info(krb5_context context,
                      pkinit_plg_crypto_context plg_cryptoctx,
                      pkinit_req_crypto_context req_cryptoctx,
                      pkinit_identity_crypto_context id_cryptoctx)
{
    int i;

    if (id_cryptoctx == NULL)
        return EINVAL;

    for (i = 0; i < MAX_CREDS_ALLOWED; i++) {
        if (id_cryptoctx->creds[i] != NULL) {
            free_cred_info(context, id_cryptoctx, id_cryptoctx->creds[i]);
            id_cryptoctx->creds[i] = NULL;
        }
    }
    return 0;
}

krb5_error_code
crypto_load_certs(krb5_context context,
                  pkinit_plg_crypto_context plg_cryptoctx,
                  pkinit_req_crypto_context req_cryptoctx,
                  pkinit_identity_opts *idopts,
                  pkinit_identity_crypto_context id_cryptoctx,
                  krb5_principal princ)
{
    krb5_error_code retval;

    switch(idopts->idtype) {
    case IDTYPE_FILE:
        retval = pkinit_get_certs_fs(context, plg_cryptoctx,
                                     req_cryptoctx, idopts,
                                     id_cryptoctx, princ);
        break;
    case IDTYPE_DIR:
        retval = pkinit_get_certs_dir(context, plg_cryptoctx,
                                      req_cryptoctx, idopts,
                                      id_cryptoctx, princ);
        break;
#ifndef WITHOUT_PKCS11
    case IDTYPE_PKCS11:
        retval = pkinit_get_certs_pkcs11(context, plg_cryptoctx,
                                         req_cryptoctx, idopts,
                                         id_cryptoctx, princ);
        break;
#endif
    case IDTYPE_PKCS12:
        retval = pkinit_get_certs_pkcs12(context, plg_cryptoctx,
                                         req_cryptoctx, idopts,
                                         id_cryptoctx, princ);
        break;
    default:
        retval = EINVAL;
    }
    if (retval)
        goto cleanup;

cleanup:
    return retval;
}

/*
 * Get number of certificates available after crypto_load_certs()
 */
krb5_error_code
crypto_cert_get_count(krb5_context context,
                      pkinit_plg_crypto_context plg_cryptoctx,
                      pkinit_req_crypto_context req_cryptoctx,
                      pkinit_identity_crypto_context id_cryptoctx,
                      int *cert_count)
{
    int count;

    if (id_cryptoctx == NULL || id_cryptoctx->creds[0] == NULL)
        return EINVAL;

    for (count = 0;
         count <= MAX_CREDS_ALLOWED && id_cryptoctx->creds[count] != NULL;
         count++);
    *cert_count = count;
    return 0;
}


/*
 * Begin iteration over the certs loaded in crypto_load_certs()
 */
krb5_error_code
crypto_cert_iteration_begin(krb5_context context,
                            pkinit_plg_crypto_context plg_cryptoctx,
                            pkinit_req_crypto_context req_cryptoctx,
                            pkinit_identity_crypto_context id_cryptoctx,
                            pkinit_cert_iter_handle *ih_ret)
{
    struct _pkinit_cert_iter_data *id;

    if (id_cryptoctx == NULL || ih_ret == NULL)
        return EINVAL;
    if (id_cryptoctx->creds[0] == NULL) /* No cred info available */
        return ENOENT;

    id = calloc(1, sizeof(*id));
    if (id == NULL)
        return ENOMEM;
    id->magic = ITER_MAGIC;
    id->plgctx = plg_cryptoctx,
        id->reqctx = req_cryptoctx,
        id->idctx = id_cryptoctx;
    id->index = 0;
    *ih_ret = (pkinit_cert_iter_handle) id;
    return 0;
}

/*
 * End iteration over the certs loaded in crypto_load_certs()
 */
krb5_error_code
crypto_cert_iteration_end(krb5_context context,
                          pkinit_cert_iter_handle ih)
{
    struct _pkinit_cert_iter_data *id = (struct _pkinit_cert_iter_data *)ih;

    if (id == NULL || id->magic != ITER_MAGIC)
        return EINVAL;
    free(ih);
    return 0;
}

/*
 * Get next certificate handle
 */
krb5_error_code
crypto_cert_iteration_next(krb5_context context,
                           pkinit_cert_iter_handle ih,
                           pkinit_cert_handle *ch_ret)
{
    struct _pkinit_cert_iter_data *id = (struct _pkinit_cert_iter_data *)ih;
    struct _pkinit_cert_data *cd;
    pkinit_identity_crypto_context id_cryptoctx;

    if (id == NULL || id->magic != ITER_MAGIC)
        return EINVAL;

    if (ch_ret == NULL)
        return EINVAL;

    id_cryptoctx = id->idctx;
    if (id_cryptoctx == NULL)
        return EINVAL;

    if (id_cryptoctx->creds[id->index] == NULL)
        return PKINIT_ITER_NO_MORE;

    cd = calloc(1, sizeof(*cd));
    if (cd == NULL)
        return ENOMEM;

    cd->magic = CERT_MAGIC;
    cd->plgctx = id->plgctx;
    cd->reqctx = id->reqctx;
    cd->idctx = id->idctx;
    cd->index = id->index;
    cd->cred = id_cryptoctx->creds[id->index++];
    *ch_ret = (pkinit_cert_handle)cd;
    return 0;
}

/*
 * Release cert handle
 */
krb5_error_code
crypto_cert_release(krb5_context context,
                    pkinit_cert_handle ch)
{
    struct _pkinit_cert_data *cd = (struct _pkinit_cert_data *)ch;
    if (cd == NULL || cd->magic != CERT_MAGIC)
        return EINVAL;
    free(cd);
    return 0;
}

/*
 * Get certificate Key Usage and Extended Key Usage
 */
static krb5_error_code
crypto_retieve_X509_key_usage(krb5_context context,
                              pkinit_plg_crypto_context plgcctx,
                              pkinit_req_crypto_context reqcctx,
                              X509 *x,
                              unsigned int *ret_ku_bits,
                              unsigned int *ret_eku_bits)
{
    krb5_error_code retval = 0;
    int i;
    unsigned int eku_bits = 0, ku_bits = 0;
    ASN1_BIT_STRING *usage = NULL;

    if (ret_ku_bits == NULL && ret_eku_bits == NULL)
        return EINVAL;

    if (ret_eku_bits)
        *ret_eku_bits = 0;
    else {
        pkiDebug("%s: EKUs not requested, not checking\n", __FUNCTION__);
        goto check_kus;
    }

    /* Start with Extended Key usage */
    i = X509_get_ext_by_NID(x, NID_ext_key_usage, -1);
    if (i >= 0) {
        EXTENDED_KEY_USAGE *eku;

        eku = X509_get_ext_d2i(x, NID_ext_key_usage, NULL, NULL);
        if (eku) {
            for (i = 0; i < sk_ASN1_OBJECT_num(eku); i++) {
                ASN1_OBJECT *certoid;
                certoid = sk_ASN1_OBJECT_value(eku, i);
                if ((OBJ_cmp(certoid, plgcctx->id_pkinit_KPClientAuth)) == 0)
                    eku_bits |= PKINIT_EKU_PKINIT;
                else if ((OBJ_cmp(certoid, OBJ_nid2obj(NID_ms_smartcard_login))) == 0)
                    eku_bits |= PKINIT_EKU_MSSCLOGIN;
                else if ((OBJ_cmp(certoid, OBJ_nid2obj(NID_client_auth))) == 0)
                    eku_bits |= PKINIT_EKU_CLIENTAUTH;
                else if ((OBJ_cmp(certoid, OBJ_nid2obj(NID_email_protect))) == 0)
                    eku_bits |= PKINIT_EKU_EMAILPROTECTION;
            }
            EXTENDED_KEY_USAGE_free(eku);
        }
    }
    pkiDebug("%s: returning eku 0x%08x\n", __FUNCTION__, eku_bits);
    *ret_eku_bits = eku_bits;

check_kus:
    /* Now the Key Usage bits */
    if (ret_ku_bits)
        *ret_ku_bits = 0;
    else {
        pkiDebug("%s: KUs not requested, not checking\n", __FUNCTION__);
        goto out;
    }

    /* Make sure usage exists before checking bits */
    X509_check_ca(x);
    usage = X509_get_ext_d2i(x, NID_key_usage, NULL, NULL);
    if (usage) {
        if (!ku_reject(x, X509v3_KU_DIGITAL_SIGNATURE))
            ku_bits |= PKINIT_KU_DIGITALSIGNATURE;
        if (!ku_reject(x, X509v3_KU_KEY_ENCIPHERMENT))
            ku_bits |= PKINIT_KU_KEYENCIPHERMENT;
        ASN1_BIT_STRING_free(usage);
    }

    pkiDebug("%s: returning ku 0x%08x\n", __FUNCTION__, ku_bits);
    *ret_ku_bits = ku_bits;
    retval = 0;
out:
    return retval;
}

/*
 * Return a string format of an X509_NAME in buf where
 * size is an in/out parameter.  On input it is the size
 * of the buffer, and on output it is the actual length
 * of the name.
 * If buf is NULL, returns the length req'd to hold name
 */
static char *
X509_NAME_oneline_ex(X509_NAME * a,
                     char *buf,
                     unsigned int *size,
                     unsigned long flag)
{
    BIO *out = NULL;

    out = BIO_new(BIO_s_mem ());
    if (X509_NAME_print_ex(out, a, 0, flag) > 0) {
        if (buf != NULL && (*size) >  (unsigned int) BIO_number_written(out)) {
            memset(buf, 0, *size);
            BIO_read(out, buf, (int) BIO_number_written(out));
        }
        else {
            *size = BIO_number_written(out);
        }
    }
    BIO_free(out);
    return (buf);
}

/*
 * Get certificate information
 */
krb5_error_code
crypto_cert_get_matching_data(krb5_context context,
                              pkinit_cert_handle ch,
                              pkinit_cert_matching_data **ret_md)
{
    krb5_error_code retval;
    pkinit_cert_matching_data *md;
    krb5_principal *pkinit_sans =NULL, *upn_sans = NULL;
    struct _pkinit_cert_data *cd = (struct _pkinit_cert_data *)ch;
    unsigned int i, j;
    char buf[DN_BUF_LEN];
    unsigned int bufsize = sizeof(buf);

    if (cd == NULL || cd->magic != CERT_MAGIC)
        return EINVAL;
    if (ret_md == NULL)
        return EINVAL;

    md = calloc(1, sizeof(*md));
    if (md == NULL)
        return ENOMEM;

    md->ch = ch;

    /* get the subject name (in rfc2253 format) */
    X509_NAME_oneline_ex(X509_get_subject_name(cd->cred->cert),
                         buf, &bufsize, XN_FLAG_SEP_COMMA_PLUS);
    md->subject_dn = strdup(buf);
    if (md->subject_dn == NULL) {
        retval = ENOMEM;
        goto cleanup;
    }

    /* get the issuer name (in rfc2253 format) */
    X509_NAME_oneline_ex(X509_get_issuer_name(cd->cred->cert),
                         buf, &bufsize, XN_FLAG_SEP_COMMA_PLUS);
    md->issuer_dn = strdup(buf);
    if (md->issuer_dn == NULL) {
        retval = ENOMEM;
        goto cleanup;
    }

    /* get the san data */
    retval = crypto_retrieve_X509_sans(context, cd->plgctx, cd->reqctx,
                                       cd->cred->cert, &pkinit_sans,
                                       &upn_sans, NULL);
    if (retval)
        goto cleanup;

    j = 0;
    if (pkinit_sans != NULL) {
        for (i = 0; pkinit_sans[i] != NULL; i++)
            j++;
    }
    if (upn_sans != NULL) {
        for (i = 0; upn_sans[i] != NULL; i++)
            j++;
    }
    if (j != 0) {
        md->sans = calloc((size_t)j+1, sizeof(*md->sans));
        if (md->sans == NULL) {
            retval = ENOMEM;
            goto cleanup;
        }
        j = 0;
        if (pkinit_sans != NULL) {
            for (i = 0; pkinit_sans[i] != NULL; i++)
                md->sans[j++] = pkinit_sans[i];
            free(pkinit_sans);
        }
        if (upn_sans != NULL) {
            for (i = 0; upn_sans[i] != NULL; i++)
                md->sans[j++] = upn_sans[i];
            free(upn_sans);
        }
        md->sans[j] = NULL;
    } else
        md->sans = NULL;

    /* get the KU and EKU data */

    retval = crypto_retieve_X509_key_usage(context, cd->plgctx, cd->reqctx,
                                           cd->cred->cert,
                                           &md->ku_bits, &md->eku_bits);
    if (retval)
        goto cleanup;

    *ret_md = md;
    retval = 0;
cleanup:
    if (retval) {
        if (md)
            crypto_cert_free_matching_data(context, md);
    }
    return retval;
}

/*
 * Free certificate information
 */
krb5_error_code
crypto_cert_free_matching_data(krb5_context context,
                               pkinit_cert_matching_data *md)
{
    krb5_principal p;
    int i;

    if (md == NULL)
        return EINVAL;
    if (md->subject_dn)
        free(md->subject_dn);
    if (md->issuer_dn)
        free(md->issuer_dn);
    if (md->sans) {
        for (i = 0, p = md->sans[i]; p != NULL; p = md->sans[++i])
            krb5_free_principal(context, p);
        free(md->sans);
    }
    free(md);
    return 0;
}

/*
 * Make this matching certificate "the chosen one"
 */
krb5_error_code
crypto_cert_select(krb5_context context,
                   pkinit_cert_matching_data *md)
{
    struct _pkinit_cert_data *cd;
    if (md == NULL)
        return EINVAL;

    cd = (struct _pkinit_cert_data *)md->ch;
    if (cd == NULL || cd->magic != CERT_MAGIC)
        return EINVAL;

    /* copy the selected cert into our id_cryptoctx */
    if (cd->idctx->my_certs != NULL) {
        sk_X509_pop_free(cd->idctx->my_certs, X509_free);
    }
    cd->idctx->my_certs = sk_X509_new_null();
    sk_X509_push(cd->idctx->my_certs, cd->cred->cert);
    cd->idctx->creds[cd->index]->cert = NULL;       /* Don't free it twice */
    cd->idctx->cert_index = 0;

    if (cd->idctx->pkcs11_method != 1) {
        cd->idctx->my_key = cd->cred->key;
        cd->idctx->creds[cd->index]->key = NULL;    /* Don't free it twice */
    }
#ifndef WITHOUT_PKCS11
    else {
        cd->idctx->cert_id = cd->cred->cert_id;
        cd->idctx->creds[cd->index]->cert_id = NULL; /* Don't free it twice */
        cd->idctx->cert_id_len = cd->cred->cert_id_len;
    }
#endif
    return 0;
}

/*
 * Choose the default certificate as "the chosen one"
 */
krb5_error_code
crypto_cert_select_default(krb5_context context,
                           pkinit_plg_crypto_context plg_cryptoctx,
                           pkinit_req_crypto_context req_cryptoctx,
                           pkinit_identity_crypto_context id_cryptoctx)
{
    krb5_error_code retval;
    int cert_count = 0;

    retval = crypto_cert_get_count(context, plg_cryptoctx, req_cryptoctx,
                                   id_cryptoctx, &cert_count);
    if (retval) {
        pkiDebug("%s: crypto_cert_get_count error %d, %s\n",
                 __FUNCTION__, retval, error_message(retval));
        goto errout;
    }
    if (cert_count != 1) {
        pkiDebug("%s: ERROR: There are %d certs to choose from, "
                 "but there must be exactly one.\n",
                 __FUNCTION__, cert_count);
        retval = EINVAL;
        goto errout;
    }
    /* copy the selected cert into our id_cryptoctx */
    if (id_cryptoctx->my_certs != NULL) {
        sk_X509_pop_free(id_cryptoctx->my_certs, X509_free);
    }
    id_cryptoctx->my_certs = sk_X509_new_null();
    sk_X509_push(id_cryptoctx->my_certs, id_cryptoctx->creds[0]->cert);
    id_cryptoctx->creds[0]->cert = NULL;        /* Don't free it twice */
    id_cryptoctx->cert_index = 0;

    if (id_cryptoctx->pkcs11_method != 1) {
        id_cryptoctx->my_key = id_cryptoctx->creds[0]->key;
        id_cryptoctx->creds[0]->key = NULL;     /* Don't free it twice */
    }
#ifndef WITHOUT_PKCS11
    else {
        id_cryptoctx->cert_id = id_cryptoctx->creds[0]->cert_id;
        id_cryptoctx->creds[0]->cert_id = NULL; /* Don't free it twice */
        id_cryptoctx->cert_id_len = id_cryptoctx->creds[0]->cert_id_len;
    }
#endif
    retval = 0;
errout:
    return retval;
}



static krb5_error_code
load_cas_and_crls(krb5_context context,
                  pkinit_plg_crypto_context plg_cryptoctx,
                  pkinit_req_crypto_context req_cryptoctx,
                  pkinit_identity_crypto_context id_cryptoctx,
                  int catype,
                  char *filename)
{
    STACK_OF(X509_INFO) *sk = NULL;
    STACK_OF(X509) *ca_certs = NULL;
    STACK_OF(X509_CRL) *ca_crls = NULL;
    BIO *in = NULL;
    krb5_error_code retval = ENOMEM;
    int i = 0;

    /* If there isn't already a stack in the context,
     * create a temporary one now */
    switch(catype) {
    case CATYPE_ANCHORS:
        if (id_cryptoctx->trustedCAs != NULL)
            ca_certs = id_cryptoctx->trustedCAs;
        else {
            ca_certs = sk_X509_new_null();
            if (ca_certs == NULL)
                return ENOMEM;
        }
        break;
    case CATYPE_INTERMEDIATES:
        if (id_cryptoctx->intermediateCAs != NULL)
            ca_certs = id_cryptoctx->intermediateCAs;
        else {
            ca_certs = sk_X509_new_null();
            if (ca_certs == NULL)
                return ENOMEM;
        }
        break;
    case CATYPE_CRLS:
        if (id_cryptoctx->revoked != NULL)
            ca_crls = id_cryptoctx->revoked;
        else {
            ca_crls = sk_X509_CRL_new_null();
            if (ca_crls == NULL)
                return ENOMEM;
        }
        break;
    default:
        return ENOTSUP;
    }

    if (!(in = BIO_new_file(filename, "r"))) {
        retval = errno;
        pkiDebug("%s: error opening file '%s': %s\n", __FUNCTION__,
                 filename, error_message(errno));
        goto cleanup;
    }

    /* This loads from a file, a stack of x509/crl/pkey sets */
    if ((sk = PEM_X509_INFO_read_bio(in, NULL, NULL, NULL)) == NULL) {
        pkiDebug("%s: error reading file '%s'\n", __FUNCTION__, filename);
        retval = EIO;
        goto cleanup;
    }

    /* scan over the stack created from loading the file contents,
     * weed out duplicates, and push new ones onto the return stack
     */
    for (i = 0; i < sk_X509_INFO_num(sk); i++) {
        X509_INFO *xi = sk_X509_INFO_value(sk, i);
        if (xi != NULL && xi->x509 != NULL && catype != CATYPE_CRLS) {
            int j = 0, size = sk_X509_num(ca_certs), flag = 0;

            if (!size) {
                sk_X509_push(ca_certs, xi->x509);
                xi->x509 = NULL;
                continue;
            }
            for (j = 0; j < size; j++) {
                X509 *x = sk_X509_value(ca_certs, j);
                flag = X509_cmp(x, xi->x509);
                if (flag == 0)
                    break;
                else
                    continue;
            }
            if (flag != 0) {
                sk_X509_push(ca_certs, X509_dup(xi->x509));
            }
        } else if (xi != NULL && xi->crl != NULL && catype == CATYPE_CRLS) {
            int j = 0, size = sk_X509_CRL_num(ca_crls), flag = 0;
            if (!size) {
                sk_X509_CRL_push(ca_crls, xi->crl);
                xi->crl = NULL;
                continue;
            }
            for (j = 0; j < size; j++) {
                X509_CRL *x = sk_X509_CRL_value(ca_crls, j);
                flag = X509_CRL_cmp(x, xi->crl);
                if (flag == 0)
                    break;
                else
                    continue;
            }
            if (flag != 0) {
                sk_X509_CRL_push(ca_crls, X509_CRL_dup(xi->crl));
            }
        }
    }

    /* If we added something and there wasn't a stack in the
     * context before, add the temporary stack to the context.
     */
    switch(catype) {
    case CATYPE_ANCHORS:
        if (sk_X509_num(ca_certs) == 0) {
            pkiDebug("no anchors in file, %s\n", filename);
            if (id_cryptoctx->trustedCAs == NULL)
                sk_X509_free(ca_certs);
        } else {
            if (id_cryptoctx->trustedCAs == NULL)
                id_cryptoctx->trustedCAs = ca_certs;
        }
        break;
    case CATYPE_INTERMEDIATES:
        if (sk_X509_num(ca_certs) == 0) {
            pkiDebug("no intermediates in file, %s\n", filename);
            if (id_cryptoctx->intermediateCAs == NULL)
                sk_X509_free(ca_certs);
        } else {
            if (id_cryptoctx->intermediateCAs == NULL)
                id_cryptoctx->intermediateCAs = ca_certs;
        }
        break;
    case CATYPE_CRLS:
        if (sk_X509_CRL_num(ca_crls) == 0) {
            pkiDebug("no crls in file, %s\n", filename);
            if (id_cryptoctx->revoked == NULL)
                sk_X509_CRL_free(ca_crls);
        } else {
            if (id_cryptoctx->revoked == NULL)
                id_cryptoctx->revoked = ca_crls;
        }
        break;
    default:
        /* Should have been caught above! */
        retval = EINVAL;
        goto cleanup;
        break;
    }

    retval = 0;

cleanup:
    if (in != NULL)
        BIO_free(in);
    if (sk != NULL)
        sk_X509_INFO_pop_free(sk, X509_INFO_free);

    return retval;
}

static krb5_error_code
load_cas_and_crls_dir(krb5_context context,
                      pkinit_plg_crypto_context plg_cryptoctx,
                      pkinit_req_crypto_context req_cryptoctx,
                      pkinit_identity_crypto_context id_cryptoctx,
                      int catype,
                      char *dirname)
{
    krb5_error_code retval = EINVAL;
    DIR *d = NULL;
    struct dirent *dentry = NULL;
    char filename[1024];

    if (dirname == NULL)
        return EINVAL;

    d = opendir(dirname);
    if (d == NULL)
        return ENOENT;

    while ((dentry = readdir(d))) {
        if (strlen(dirname) + strlen(dentry->d_name) + 2 > sizeof(filename)) {
            pkiDebug("%s: Path too long -- directory '%s' and file '%s'\n",
                     __FUNCTION__, dirname, dentry->d_name);
            goto cleanup;
        }
        /* Ignore subdirectories and anything starting with a dot */
#ifdef DT_DIR
        if (dentry->d_type == DT_DIR)
            continue;
#endif
        if (dentry->d_name[0] == '.')
            continue;
        snprintf(filename, sizeof(filename), "%s/%s", dirname, dentry->d_name);

        retval = load_cas_and_crls(context, plg_cryptoctx, req_cryptoctx,
                                   id_cryptoctx, catype, filename);
        if (retval)
            goto cleanup;
    }

    retval = 0;

cleanup:
    if (d != NULL)
        closedir(d);

    return retval;
}

krb5_error_code
crypto_load_cas_and_crls(krb5_context context,
                         pkinit_plg_crypto_context plg_cryptoctx,
                         pkinit_req_crypto_context req_cryptoctx,
                         pkinit_identity_opts *idopts,
                         pkinit_identity_crypto_context id_cryptoctx,
                         int idtype,
                         int catype,
                         char *id)
{
    pkiDebug("%s: called with idtype %s and catype %s\n",
             __FUNCTION__, idtype2string(idtype), catype2string(catype));
    switch (idtype) {
    case IDTYPE_FILE:
        return load_cas_and_crls(context, plg_cryptoctx, req_cryptoctx,
                                 id_cryptoctx, catype, id);
        break;
    case IDTYPE_DIR:
        return load_cas_and_crls_dir(context, plg_cryptoctx, req_cryptoctx,
                                     id_cryptoctx, catype, id);
        break;
    default:
        return ENOTSUP;
        break;
    }
}

static krb5_error_code
create_identifiers_from_stack(STACK_OF(X509) *sk,
                              krb5_external_principal_identifier *** ids)
{
    krb5_error_code retval = ENOMEM;
    int i = 0, sk_size = sk_X509_num(sk);
    krb5_external_principal_identifier **krb5_cas = NULL;
    X509 *x = NULL;
    X509_NAME *xn = NULL;
    unsigned char *p = NULL;
    int len = 0;
    PKCS7_ISSUER_AND_SERIAL *is = NULL;
    char buf[DN_BUF_LEN];

    *ids = NULL;

    krb5_cas =
        malloc((sk_size + 1) * sizeof(krb5_external_principal_identifier *));
    if (krb5_cas == NULL)
        return ENOMEM;
    krb5_cas[sk_size] = NULL;

    for (i = 0; i < sk_size; i++) {
        krb5_cas[i] = malloc(sizeof(krb5_external_principal_identifier));

        x = sk_X509_value(sk, i);

        X509_NAME_oneline(X509_get_subject_name(x), buf, sizeof(buf));
        pkiDebug("#%d cert= %s\n", i, buf);

        /* fill-in subjectName */
        krb5_cas[i]->subjectName.magic = 0;
        krb5_cas[i]->subjectName.length = 0;
        krb5_cas[i]->subjectName.data = NULL;

        xn = X509_get_subject_name(x);
        len = i2d_X509_NAME(xn, NULL);
        if ((p = krb5_cas[i]->subjectName.data = malloc((size_t) len)) == NULL)
            goto cleanup;
        i2d_X509_NAME(xn, &p);
        krb5_cas[i]->subjectName.length = len;

        /* fill-in issuerAndSerialNumber */
        krb5_cas[i]->issuerAndSerialNumber.length = 0;
        krb5_cas[i]->issuerAndSerialNumber.magic = 0;
        krb5_cas[i]->issuerAndSerialNumber.data = NULL;

#ifdef LONGHORN_BETA_COMPAT
        if (longhorn == 0) { /* XXX Longhorn doesn't like this */
#endif
            is = PKCS7_ISSUER_AND_SERIAL_new();
            X509_NAME_set(&is->issuer, X509_get_issuer_name(x));
            M_ASN1_INTEGER_free(is->serial);
            is->serial = M_ASN1_INTEGER_dup(X509_get_serialNumber(x));
            len = i2d_PKCS7_ISSUER_AND_SERIAL(is, NULL);
            if ((p = krb5_cas[i]->issuerAndSerialNumber.data =
                 malloc((size_t) len)) == NULL)
                goto cleanup;
            i2d_PKCS7_ISSUER_AND_SERIAL(is, &p);
            krb5_cas[i]->issuerAndSerialNumber.length = len;
#ifdef LONGHORN_BETA_COMPAT
        }
#endif

        /* fill-in subjectKeyIdentifier */
        krb5_cas[i]->subjectKeyIdentifier.length = 0;
        krb5_cas[i]->subjectKeyIdentifier.magic = 0;
        krb5_cas[i]->subjectKeyIdentifier.data = NULL;


#ifdef LONGHORN_BETA_COMPAT
        if (longhorn == 0) {    /* XXX Longhorn doesn't like this */
#endif
            if (X509_get_ext_by_NID(x, NID_subject_key_identifier, -1) >= 0) {
                ASN1_OCTET_STRING *ikeyid = NULL;

                if ((ikeyid = X509_get_ext_d2i(x, NID_subject_key_identifier, NULL,
                                               NULL))) {
                    len = i2d_ASN1_OCTET_STRING(ikeyid, NULL);
                    if ((p = krb5_cas[i]->subjectKeyIdentifier.data =
                         malloc((size_t) len)) == NULL)
                        goto cleanup;
                    i2d_ASN1_OCTET_STRING(ikeyid, &p);
                    krb5_cas[i]->subjectKeyIdentifier.length = len;
                }
                if (ikeyid != NULL)
                    ASN1_OCTET_STRING_free(ikeyid);
            }
#ifdef LONGHORN_BETA_COMPAT
        }
#endif
        if (is != NULL) {
            if (is->issuer != NULL)
                X509_NAME_free(is->issuer);
            if (is->serial != NULL)
                ASN1_INTEGER_free(is->serial);
            free(is);
        }
    }

    *ids = krb5_cas;

    retval = 0;
cleanup:
    if (retval)
        free_krb5_external_principal_identifier(&krb5_cas);

    return retval;
}

static krb5_error_code
create_krb5_invalidCertificates(krb5_context context,
                                pkinit_plg_crypto_context plg_cryptoctx,
                                pkinit_req_crypto_context req_cryptoctx,
                                pkinit_identity_crypto_context id_cryptoctx,
                                krb5_external_principal_identifier *** ids)
{

    krb5_error_code retval = ENOMEM;
    STACK_OF(X509) *sk = NULL;

    *ids = NULL;
    if (req_cryptoctx->received_cert == NULL)
        return KRB5KDC_ERR_PREAUTH_FAILED;

    sk = sk_X509_new_null();
    if (sk == NULL)
        goto cleanup;
    sk_X509_push(sk, req_cryptoctx->received_cert);

    retval = create_identifiers_from_stack(sk, ids);

    sk_X509_free(sk);
cleanup:

    return retval;
}

krb5_error_code
create_krb5_supportedCMSTypes(krb5_context context,
                              pkinit_plg_crypto_context plg_cryptoctx,
                              pkinit_req_crypto_context req_cryptoctx,
                              pkinit_identity_crypto_context id_cryptoctx,
                              krb5_algorithm_identifier ***oids)
{

    krb5_error_code retval = ENOMEM;
    krb5_algorithm_identifier **loids = NULL;
    krb5_octet_data des3oid = {0, 8, (unsigned char *)"\x2A\x86\x48\x86\xF7\x0D\x03\x07" };

    *oids = NULL;
    loids = malloc(2 * sizeof(krb5_algorithm_identifier *));
    if (loids == NULL)
        goto cleanup;
    loids[1] = NULL;
    loids[0] = malloc(sizeof(krb5_algorithm_identifier));
    if (loids[0] == NULL) {
        free(loids);
        goto cleanup;
    }
    retval = pkinit_copy_krb5_octet_data(&loids[0]->algorithm, &des3oid);
    if (retval) {
        free(loids[0]);
        free(loids);
        goto cleanup;
    }
    loids[0]->parameters.length = 0;
    loids[0]->parameters.data = NULL;

    *oids = loids;
    retval = 0;
cleanup:

    return retval;
}

krb5_error_code
create_krb5_trustedCertifiers(krb5_context context,
                              pkinit_plg_crypto_context plg_cryptoctx,
                              pkinit_req_crypto_context req_cryptoctx,
                              pkinit_identity_crypto_context id_cryptoctx,
                              krb5_external_principal_identifier *** ids)
{

    krb5_error_code retval = ENOMEM;
    STACK_OF(X509) *sk = id_cryptoctx->trustedCAs;

    *ids = NULL;
    if (id_cryptoctx->trustedCAs == NULL)
        return KRB5KDC_ERR_PREAUTH_FAILED;

    retval = create_identifiers_from_stack(sk, ids);

    return retval;
}

krb5_error_code
create_krb5_trustedCas(krb5_context context,
                       pkinit_plg_crypto_context plg_cryptoctx,
                       pkinit_req_crypto_context req_cryptoctx,
                       pkinit_identity_crypto_context id_cryptoctx,
                       int flag,
                       krb5_trusted_ca *** ids)
{
    krb5_error_code retval = ENOMEM;
    STACK_OF(X509) *sk = id_cryptoctx->trustedCAs;
    int i = 0, len = 0, sk_size = sk_X509_num(sk);
    krb5_trusted_ca **krb5_cas = NULL;
    X509 *x = NULL;
    char buf[DN_BUF_LEN];
    X509_NAME *xn = NULL;
    unsigned char *p = NULL;
    PKCS7_ISSUER_AND_SERIAL *is = NULL;

    *ids = NULL;
    if (id_cryptoctx->trustedCAs == NULL)
        return KRB5KDC_ERR_PREAUTH_FAILED;

    krb5_cas = malloc((sk_size + 1) * sizeof(krb5_trusted_ca *));
    if (krb5_cas == NULL)
        return ENOMEM;
    krb5_cas[sk_size] = NULL;

    for (i = 0; i < sk_size; i++) {
        krb5_cas[i] = malloc(sizeof(krb5_trusted_ca));
        if (krb5_cas[i] == NULL)
            goto cleanup;
        x = sk_X509_value(sk, i);

        X509_NAME_oneline(X509_get_subject_name(x), buf, sizeof(buf));
        pkiDebug("#%d cert= %s\n", i, buf);

        switch (flag) {
        case choice_trusted_cas_principalName:
            krb5_cas[i]->choice = choice_trusted_cas_principalName;
            break;
        case choice_trusted_cas_caName:
            krb5_cas[i]->choice = choice_trusted_cas_caName;
            krb5_cas[i]->u.caName.data = NULL;
            krb5_cas[i]->u.caName.length = 0;
            xn = X509_get_subject_name(x);
            len = i2d_X509_NAME(xn, NULL);
            if ((p = krb5_cas[i]->u.caName.data =
                 malloc((size_t) len)) == NULL)
                goto cleanup;
            i2d_X509_NAME(xn, &p);
            krb5_cas[i]->u.caName.length = len;
            break;
        case choice_trusted_cas_issuerAndSerial:
            krb5_cas[i]->choice = choice_trusted_cas_issuerAndSerial;
            krb5_cas[i]->u.issuerAndSerial.data = NULL;
            krb5_cas[i]->u.issuerAndSerial.length = 0;
            is = PKCS7_ISSUER_AND_SERIAL_new();
            X509_NAME_set(&is->issuer, X509_get_issuer_name(x));
            M_ASN1_INTEGER_free(is->serial);
            is->serial = M_ASN1_INTEGER_dup(X509_get_serialNumber(x));
            len = i2d_PKCS7_ISSUER_AND_SERIAL(is, NULL);
            if ((p = krb5_cas[i]->u.issuerAndSerial.data =
                 malloc((size_t) len)) == NULL)
                goto cleanup;
            i2d_PKCS7_ISSUER_AND_SERIAL(is, &p);
            krb5_cas[i]->u.issuerAndSerial.length = len;
            if (is != NULL) {
                if (is->issuer != NULL)
                    X509_NAME_free(is->issuer);
                if (is->serial != NULL)
                    ASN1_INTEGER_free(is->serial);
                free(is);
            }
            break;
        default: break;
        }
    }
    retval = 0;
    *ids = krb5_cas;
cleanup:
    if (retval)
        free_krb5_trusted_ca(&krb5_cas);

    return retval;
}

krb5_error_code
create_issuerAndSerial(krb5_context context,
                       pkinit_plg_crypto_context plg_cryptoctx,
                       pkinit_req_crypto_context req_cryptoctx,
                       pkinit_identity_crypto_context id_cryptoctx,
                       unsigned char **out,
                       unsigned int *out_len)
{
    unsigned char *p = NULL;
    PKCS7_ISSUER_AND_SERIAL *is = NULL;
    int len = 0;
    krb5_error_code retval = ENOMEM;
    X509 *cert = req_cryptoctx->received_cert;

    *out = NULL;
    *out_len = 0;
    if (req_cryptoctx->received_cert == NULL)
        return 0;

    is = PKCS7_ISSUER_AND_SERIAL_new();
    X509_NAME_set(&is->issuer, X509_get_issuer_name(cert));
    M_ASN1_INTEGER_free(is->serial);
    is->serial = M_ASN1_INTEGER_dup(X509_get_serialNumber(cert));
    len = i2d_PKCS7_ISSUER_AND_SERIAL(is, NULL);
    if ((p = *out = malloc((size_t) len)) == NULL)
        goto cleanup;
    i2d_PKCS7_ISSUER_AND_SERIAL(is, &p);
    *out_len = len;
    retval = 0;

cleanup:
    X509_NAME_free(is->issuer);
    ASN1_INTEGER_free(is->serial);
    free(is);

    return retval;
}

static int
pkcs7_decrypt(krb5_context context,
              pkinit_identity_crypto_context id_cryptoctx,
              PKCS7 *p7,
              BIO *data)
{
    BIO *tmpmem = NULL;
    int retval = 0, i = 0;
    char buf[4096];

    if(p7 == NULL)
        return 0;

    if(!PKCS7_type_is_enveloped(p7)) {
        pkiDebug("wrong pkcs7 content type\n");
        return 0;
    }

    if(!(tmpmem = pkcs7_dataDecode(context, id_cryptoctx, p7))) {
        pkiDebug("unable to decrypt pkcs7 object\n");
        return 0;
    }

    for(;;) {
        i = BIO_read(tmpmem, buf, sizeof(buf));
        if (i <= 0) break;
        BIO_write(data, buf, i);
        BIO_free_all(tmpmem);
        return 1;
    }
    return retval;
}

krb5_error_code
pkinit_process_td_trusted_certifiers(
    krb5_context context,
    pkinit_plg_crypto_context plg_cryptoctx,
    pkinit_req_crypto_context req_cryptoctx,
    pkinit_identity_crypto_context id_cryptoctx,
    krb5_external_principal_identifier **krb5_trusted_certifiers,
    int td_type)
{
    krb5_error_code retval = ENOMEM;
    STACK_OF(X509_NAME) *sk_xn = NULL;
    X509_NAME *xn = NULL;
    PKCS7_ISSUER_AND_SERIAL *is = NULL;
    ASN1_OCTET_STRING *id = NULL;
    const unsigned char *p = NULL;
    char buf[DN_BUF_LEN];
    int i = 0;

    if (td_type == TD_TRUSTED_CERTIFIERS)
        pkiDebug("received trusted certifiers\n");
    else
        pkiDebug("received invalid certificate\n");

    sk_xn = sk_X509_NAME_new_null();
    while(krb5_trusted_certifiers[i] != NULL) {
        if (krb5_trusted_certifiers[i]->subjectName.data != NULL) {
            p = krb5_trusted_certifiers[i]->subjectName.data;
            xn = d2i_X509_NAME(NULL, &p,
                               (int)krb5_trusted_certifiers[i]->subjectName.length);
            if (xn == NULL)
                goto cleanup;
            X509_NAME_oneline(xn, buf, sizeof(buf));
            if (td_type == TD_TRUSTED_CERTIFIERS)
                pkiDebug("#%d cert = %s is trusted by kdc\n", i, buf);
            else
                pkiDebug("#%d cert = %s is invalid\n", i, buf);
            sk_X509_NAME_push(sk_xn, xn);
        }

        if (krb5_trusted_certifiers[i]->issuerAndSerialNumber.data != NULL) {
            p = krb5_trusted_certifiers[i]->issuerAndSerialNumber.data;
            is = d2i_PKCS7_ISSUER_AND_SERIAL(NULL, &p,
                                             (int)krb5_trusted_certifiers[i]->issuerAndSerialNumber.length);
            if (is == NULL)
                goto cleanup;
            X509_NAME_oneline(is->issuer, buf, sizeof(buf));
            if (td_type == TD_TRUSTED_CERTIFIERS)
                pkiDebug("#%d issuer = %s serial = %ld is trusted bu kdc\n", i,
                         buf, ASN1_INTEGER_get(is->serial));
            else
                pkiDebug("#%d issuer = %s serial = %ld is invalid\n", i, buf,
                         ASN1_INTEGER_get(is->serial));
            PKCS7_ISSUER_AND_SERIAL_free(is);
        }

        if (krb5_trusted_certifiers[i]->subjectKeyIdentifier.data != NULL) {
            p = krb5_trusted_certifiers[i]->subjectKeyIdentifier.data;
            id = d2i_ASN1_OCTET_STRING(NULL, &p,
                                       (int)krb5_trusted_certifiers[i]->subjectKeyIdentifier.length);
            if (id == NULL)
                goto cleanup;
            /* XXX */
            ASN1_OCTET_STRING_free(id);
        }
        i++;
    }
    /* XXX Since we not doing anything with received trusted certifiers
     * return an error. this is the place where we can pick a different
     * client certificate based on the information in td_trusted_certifiers
     */
    retval = KRB5KDC_ERR_PREAUTH_FAILED;
cleanup:
    if (sk_xn != NULL)
        sk_X509_NAME_pop_free(sk_xn, X509_NAME_free);

    return retval;
}

static BIO *
pkcs7_dataDecode(krb5_context context,
                 pkinit_identity_crypto_context id_cryptoctx,
                 PKCS7 *p7)
{
    int i = 0;
    unsigned int jj = 0, tmp_len = 0;
    BIO *out=NULL,*etmp=NULL,*bio=NULL;
    unsigned char *tmp=NULL;
    ASN1_OCTET_STRING *data_body=NULL;
    const EVP_CIPHER *evp_cipher=NULL;
    EVP_CIPHER_CTX *evp_ctx=NULL;
    X509_ALGOR *enc_alg=NULL;
    STACK_OF(PKCS7_RECIP_INFO) *rsk=NULL;
    PKCS7_RECIP_INFO *ri=NULL;
    X509 *cert = sk_X509_value(id_cryptoctx->my_certs,
                               id_cryptoctx->cert_index);

    p7->state=PKCS7_S_HEADER;

    rsk=p7->d.enveloped->recipientinfo;
    enc_alg=p7->d.enveloped->enc_data->algorithm;
    data_body=p7->d.enveloped->enc_data->enc_data;
    evp_cipher=EVP_get_cipherbyobj(enc_alg->algorithm);
    if (evp_cipher == NULL) {
        PKCS7err(PKCS7_F_PKCS7_DATADECODE,PKCS7_R_UNSUPPORTED_CIPHER_TYPE);
        goto cleanup;
    }

    if ((etmp=BIO_new(BIO_f_cipher())) == NULL) {
        PKCS7err(PKCS7_F_PKCS7_DATADECODE,ERR_R_BIO_LIB);
        goto cleanup;
    }

    /* It was encrypted, we need to decrypt the secret key
     * with the private key */

    /* Find the recipientInfo which matches the passed certificate
     * (if any)
     */

    if (cert) {
        for (i=0; i<sk_PKCS7_RECIP_INFO_num(rsk); i++) {
            int tmp_ret = 0;
            ri=sk_PKCS7_RECIP_INFO_value(rsk,i);
            tmp_ret = X509_NAME_cmp(ri->issuer_and_serial->issuer,
                                    cert->cert_info->issuer);
            if (!tmp_ret) {
                tmp_ret = M_ASN1_INTEGER_cmp(cert->cert_info->serialNumber,
                                             ri->issuer_and_serial->serial);
                if (!tmp_ret)
                    break;
            }
            ri=NULL;
        }
        if (ri == NULL) {
            PKCS7err(PKCS7_F_PKCS7_DATADECODE,
                     PKCS7_R_NO_RECIPIENT_MATCHES_CERTIFICATE);
            goto cleanup;
        }

    }

    /* If we haven't got a certificate try each ri in turn */

    if (cert == NULL) {
        for (i=0; i<sk_PKCS7_RECIP_INFO_num(rsk); i++) {
            ri=sk_PKCS7_RECIP_INFO_value(rsk,i);
            jj = pkinit_decode_data(context, id_cryptoctx,
                                    M_ASN1_STRING_data(ri->enc_key),
                                    (unsigned int) M_ASN1_STRING_length(ri->enc_key),
                                    &tmp, &tmp_len);
            if (jj) {
                PKCS7err(PKCS7_F_PKCS7_DATADECODE, ERR_R_EVP_LIB);
                goto cleanup;
            }

            if (!jj && tmp_len > 0) {
                jj = tmp_len;
                break;
            }

            ERR_clear_error();
            ri = NULL;
        }

        if (ri == NULL) {
            PKCS7err(PKCS7_F_PKCS7_DATADECODE, PKCS7_R_NO_RECIPIENT_MATCHES_KEY);
            goto cleanup;
        }
    }
    else {
        jj = pkinit_decode_data(context, id_cryptoctx,
                                M_ASN1_STRING_data(ri->enc_key),
                                (unsigned int) M_ASN1_STRING_length(ri->enc_key),
                                &tmp, &tmp_len);
        if (jj || tmp_len <= 0) {
            PKCS7err(PKCS7_F_PKCS7_DATADECODE, ERR_R_EVP_LIB);
            goto cleanup;
        }
        jj = tmp_len;
    }

    evp_ctx=NULL;
    BIO_get_cipher_ctx(etmp,&evp_ctx);
    if (EVP_CipherInit_ex(evp_ctx,evp_cipher,NULL,NULL,NULL,0) <= 0)
        goto cleanup;
    if (EVP_CIPHER_asn1_to_param(evp_ctx,enc_alg->parameter) < 0)
        goto cleanup;

    if (jj != (unsigned) EVP_CIPHER_CTX_key_length(evp_ctx)) {
        /* Some S/MIME clients don't use the same key
         * and effective key length. The key length is
         * determined by the size of the decrypted RSA key.
         */
        if(!EVP_CIPHER_CTX_set_key_length(evp_ctx, (int)jj)) {
            PKCS7err(PKCS7_F_PKCS7_DATADECODE,
                     PKCS7_R_DECRYPTED_KEY_IS_WRONG_LENGTH);
            goto cleanup;
        }
    }
    if (EVP_CipherInit_ex(evp_ctx,NULL,NULL,tmp,NULL,0) <= 0)
        goto cleanup;

    OPENSSL_cleanse(tmp,jj);

    if (out == NULL)
        out=etmp;
    else
        BIO_push(out,etmp);
    etmp=NULL;

    if (data_body->length > 0)
        bio = BIO_new_mem_buf(data_body->data, data_body->length);
    else {
        bio=BIO_new(BIO_s_mem());
        BIO_set_mem_eof_return(bio,0);
    }
    BIO_push(out,bio);
    bio=NULL;

    if (0) {
    cleanup:
        if (out != NULL) BIO_free_all(out);
        if (etmp != NULL) BIO_free_all(etmp);
        if (bio != NULL) BIO_free_all(bio);
        out=NULL;
    }

    if (tmp != NULL)
        free(tmp);

    return(out);
}

static krb5_error_code
der_decode_data(unsigned char *data, long data_len,
                unsigned char **out, long *out_len)
{
    krb5_error_code retval = KRB5KDC_ERR_PREAUTH_FAILED;
    ASN1_OCTET_STRING *s = NULL;
    const unsigned char *p = data;

    if ((s = d2i_ASN1_BIT_STRING(NULL, &p, data_len)) == NULL)
        goto cleanup;
    *out_len = s->length;
    if ((*out = malloc((size_t) *out_len + 1)) == NULL) {
        retval = ENOMEM;
        goto cleanup;
    }
    memcpy(*out, s->data, (size_t) s->length);
    (*out)[s->length] = '\0';

    retval = 0;
cleanup:
    if (s != NULL)
        ASN1_OCTET_STRING_free(s);

    return retval;
}


#ifdef DEBUG_DH
static void
print_dh(DH * dh, char *msg)
{
    BIO *bio_err = NULL;

    bio_err = BIO_new(BIO_s_file());
    BIO_set_fp(bio_err, stderr, BIO_NOCLOSE | BIO_FP_TEXT);

    if (msg)
        BIO_puts(bio_err, (const char *)msg);
    if (dh)
        DHparams_print(bio_err, dh);

    BIO_puts(bio_err, "private key: ");
    BN_print(bio_err, dh->priv_key);
    BIO_puts(bio_err, (const char *)"\n");
    BIO_free(bio_err);

}

static void
print_pubkey(BIGNUM * key, char *msg)
{
    BIO *bio_err = NULL;

    bio_err = BIO_new(BIO_s_file());
    BIO_set_fp(bio_err, stderr, BIO_NOCLOSE | BIO_FP_TEXT);

    if (msg)
        BIO_puts(bio_err, (const char *)msg);
    if (key)
        BN_print(bio_err, key);
    BIO_puts(bio_err, "\n");

    BIO_free(bio_err);

}
#endif

static char *
pkinit_pkcs11_code_to_text(int err)
{
    int i;
    static char uc[32];

    for (i = 0; pkcs11_errstrings[i].text != NULL; i++)
        if (pkcs11_errstrings[i].code == err)
            break;
    if (pkcs11_errstrings[i].text != NULL)
        return (pkcs11_errstrings[i].text);
    snprintf(uc, sizeof(uc), _("unknown code 0x%x"), err);
    return (uc);
}
