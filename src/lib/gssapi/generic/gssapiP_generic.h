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

#ifndef _GSSAPIP_GENERIC_H_
#define _GSSAPIP_GENERIC_H_

/*
 * $Id$
 */

#include "gssapi.h"

#include "gssapi_generic_err.h"
#include <errno.h>
#include <sys/types.h>
#include <netinet/in.h>

/** helper macros **/

#define g_OID_equal(o1,o2) \
   (((o1)->length == (o2)->length) && \
    (memcmp((o1)->elements,(o2)->elements,(o1)->length) == 0))

/* this code knows that an int on the wire is 32 bits.  The type of
   num should be at least this big, or the extra shifts may do weird
   things */

#define TWRITE_INT(ptr, num, bigend) \
   (ptr)[0] = (bigend)?((num)>>24):((num)&0xff); \
   (ptr)[1] = (bigend)?(((num)>>16)&0xff):(((num)>>8)&0xff); \
   (ptr)[2] = (bigend)?(((num)>>8)&0xff):(((num)>>16)&0xff); \
   (ptr)[3] = (bigend)?((num)&0xff):((num)>>24); \
   (ptr) += 4;

#define TREAD_INT(ptr, num, bigend) \
   (num) = (((ptr)[0]<<((bigend)?24: 0)) | \
            ((ptr)[1]<<((bigend)?16: 8)) | \
            ((ptr)[2]<<((bigend)? 8:16)) | \
            ((ptr)[3]<<((bigend)? 0:24))); \
   (ptr) += 4;

#define TWRITE_STR(ptr, str, len) \
   memcpy((ptr), (char *) (str), (len)); \
   (ptr) += (len);

#define TREAD_STR(ptr, str, len) \
   (str) = (ptr); \
   (ptr) += (len);

#define TWRITE_BUF(ptr, buf, bigend) \
   TWRITE_INT((ptr), (buf).length, (bigend)); \
   TWRITE_STR((ptr), (buf).value, (buf).length);

/** malloc wrappers; these may actually do something later */

#define xmalloc(n) malloc(n)
#define xrealloc(p,n) realloc(p,n)
#ifdef xfree
#undef xfree
#endif
#define xfree(p) free(p)

/** helper functions **/

int g_save_name(void **vdb, gss_name_t *name);
int g_save_cred_id(void **vdb, gss_cred_id_t *cred);
int g_save_ctx_id(void **vdb, gss_ctx_id_t *ctx);

int g_validate_name(void **vdb, gss_name_t *name);
int g_validate_cred_id(void **vdb, gss_cred_id_t *cred);
int g_validate_ctx_id(void **vdb, gss_ctx_id_t *ctx);

int g_delete_name(void **vdb, gss_name_t *name);
int g_delete_cred_id(void **vdb, gss_cred_id_t *cred);
int g_delete_ctx_id(void **vdb, gss_ctx_id_t *ctx);

int g_make_string_buffer(const char *str, gss_buffer_t buffer);

int g_copy_OID_set(const gss_OID_set_desc * const in, gss_OID_set *out);

int g_token_size(const_gss_OID mech, unsigned int body_size);

void g_make_token_header(const_gss_OID mech, int body_size,
			  unsigned char **buf, int tok_type);

int g_verify_token_header(const_gss_OID mech, int *body_size,
			  unsigned char **buf, int tok_type, int toksize);

OM_uint32 g_display_major_status(OM_uint32 *minor_status,
				 OM_uint32 status_value,
				 int *message_context,
				 gss_buffer_t status_string);

OM_uint32 g_display_com_err_status(OM_uint32 *minor_status,
				   OM_uint32 status_value,
				   gss_buffer_t status_string);

char *g_canonicalize_host(char *hostname);

char *g_strdup(char *str);

/** declarations of internal name mechanism functions **/

OM_uint32 generic_gss_release_buffer
           (OM_uint32*,       /* minor_status */
            gss_buffer_t      /* buffer */
           );

OM_uint32 generic_gss_release_oid_set
           (OM_uint32*,       /* minor_status */
            gss_OID_set*      /* set */
           );

#endif /* _GSSAPIP_GENERIC_H_ */
