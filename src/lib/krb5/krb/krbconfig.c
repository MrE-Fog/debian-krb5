/*
 * $Source$
 * $Author$
 *
 * Copyright 1990,1991 by the Massachusetts Institute of Technology.
 * All Rights Reserved.
 *
 * Export of this software from the United States of America is assumed
 *   to require a specific license from the United States Government.
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
 * Configuration variables for libkrb.
 */

#if !defined(lint) && !defined(SABER)
static char rcsid_krbconfig_c [] =
"$Id$";
#endif	/* !lint & !SABER */

#include <krb5/krb5.h>

krb5_deltat krb5_clockskew = 5 * 60;	/* five minutes */
krb5_cksumtype krb5_kdc_req_sumtype = CKSUMTYPE_RSA_MD4;
krb5_flags krb5_kdc_default_options = KDC_OPT_RENEWABLE_OK;
