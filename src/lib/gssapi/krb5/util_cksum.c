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

#include "gssapiP_krb5.h"
#include <memory.h>

/*
 * $Id$
 */

krb5_error_code kg_checksum_channel_bindings(gss_channel_bindings_t cb,
					     krb5_checksum *cksum)
{
   int len;
   char *buf, *ptr;
   long tmp;
   krb5_error_code code;

   /* generate a buffer full of zeros if no cb specified */

   if (cb == GSS_C_NO_CHANNEL_BINDINGS) {
      /* allocate the cksum contents buffer */
      if ((cksum->contents = (krb5_octet *)
	   xmalloc(krb5_checksum_size(CKSUMTYPE_RSA_MD5))) == NULL)
	 return(ENOMEM);

      cksum->checksum_type = CKSUMTYPE_RSA_MD5;
      memset(cksum->contents, '\0',
	     (cksum->length = krb5_checksum_size(CKSUMTYPE_RSA_MD5)));
      return(0);
   }

   /* create the buffer to checksum into */

   len = (sizeof(tmp)*5+
	  cb->initiator_address.length+
	  cb->acceptor_address.length+
	  cb->application_data.length);

   if ((buf = (char *) xmalloc(len)) == NULL)
      return(ENOMEM);

   /* allocate the cksum contents buffer */
   if ((cksum->contents = (krb5_octet *)
	xmalloc(krb5_checksum_size(CKSUMTYPE_RSA_MD5))) == NULL) {
      free(buf);
      return(ENOMEM);
   }

   /* helper macros.  This code currently depends on a long being 32
      bits, and htonl dtrt. */

   ptr = buf;

   TWRITE_INT(ptr, tmp, cb->initiator_addrtype);
   TWRITE_BUF(ptr, tmp, cb->initiator_address);
   TWRITE_INT(ptr, tmp, cb->acceptor_addrtype);
   TWRITE_BUF(ptr, tmp, cb->acceptor_address);
   TWRITE_BUF(ptr, tmp, cb->application_data);

   /* checksum the data */

   if (code = krb5_calculate_checksum(CKSUMTYPE_RSA_MD5, buf, len,
				      NULL, 0, cksum)) {
      xfree(cksum->contents);
      xfree(buf);
      return(code);
   }

   /* success */

   xfree(buf);
   return(0);
}
