/*
 * $Source$
 * $Author$
 *
 * Copyright 1990,1991 by the Massachusetts Institute of Technology.
 * All Rights Reserved.
 *
 * For copying and distribution information, please see the file
 * <krb5/copyright.h>.
 *
 * krb_rd_priv for krb425
 */

#if !defined(lint) && !defined(SABER)
static char rcsid_rd_priv_c[] =
"$Id$";
#endif	/* !lint & !SABER */

#include "krb425.h"
#include <arpa/inet.h>

long
krb_rd_priv(in, in_length, sched, key, sender, receiver, msg)
u_char *in;
u_long in_length;
Key_schedule sched;	/* ignored */
des_cblock key;
struct sockaddr_in *sender;
struct sockaddr_in *receiver;
MSG_DAT *msg;
{
	krb5_data inbuf;
	krb5_data out;
	krb5_keyblock keyb;
	krb5_address saddr, *saddr2;
	krb5_address raddr;
	krb5_error_code r;
	char sa[4], ra[4];
	krb5_rcache rcache;
	char *cachename;

	keyb.keytype = KEYTYPE_DES;
	keyb.length = sizeof(des_cblock);
	keyb.contents = (krb5_octet *)key;

	saddr.addrtype = ADDRTYPE_INET;
	saddr.length = 4;
	saddr.contents = (krb5_octet *)sa;

	raddr.addrtype = ADDRTYPE_INET;
	raddr.length = 4;
	raddr.contents = (krb5_octet *)ra;

	memcpy(sa, (char *)&sender->sin_addr, 4);
	memcpy(ra, (char *)&receiver->sin_addr, 4);

	inbuf.data = (char *)in;
	inbuf.length = in_length;

	if (r = krb5_gen_portaddr(&saddr, (krb5_pointer)&sender->sin_port,
				  &saddr2)) {
#ifdef	EBUG
	    ERROR(r);
#endif
	    return(krb425error(r));
	}
	if (cachename = calloc(1, strlen(inet_ntoa(sender->sin_addr)+1+1+5)))
	    /* 1 for NUL, 1 for ., 5 for digits of port
		       (unsigned 16bit, no greater than 65535) */
	    sprintf(cachename, "%s.%u", inet_ntoa(sender->sin_addr),
		    ntohs(receiver->sin_port));
	else {
#ifdef	EBUG
	    ERROR(ENOMEM);
#endif
	    return(krb425error(ENOMEM));
	}
	    
	if (r = krb5_get_server_rcache(cachename,
				       &rcache)) {
	    krb5_free_address(saddr2);
#ifdef	EBUG
	    ERROR(r);
#endif
	    return(-1);
	}
	free(cachename);
	r = krb5_rd_priv(&inbuf, &keyb, saddr2, &raddr,
			 0, 0, 0, rcache, &out);
	krb5_rc_close(rcache);
	xfree(rcache);

	krb5_free_address(saddr2);

	if (r) {
#ifdef	EBUG
		ERROR(r);
#endif
		return(krb425error(r));
	}

	msg->app_data = (u_char *)out.data;
	msg->app_length = out.length;
	msg->hash = 0L;
	msg->swap = 0;
	msg->time_sec = 0;
	msg->time_5ms = 0;
	return(KSUCCESS);
}
