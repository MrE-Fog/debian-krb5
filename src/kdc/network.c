/*
 * kdc/network.c
 *
 * Copyright 1990 by the Massachusetts Institute of Technology.
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
 * Network code for Kerberos v5 KDC.
 */


#include <krb5/copyright.h>
#include <krb5/osconf.h>
#include <krb5/krb5.h>
#include <krb5/ext-proto.h>
#include <krb5/los-proto.h>
#include <krb5/kdb.h>
#include <com_err.h>
#include "kdc_util.h"
#include "extern.h"
#include "kdc5_err.h"

#ifdef KRB5_USE_INET
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#if defined(_AIX) || defined(AIXArchitecture)
#include <sys/select.h>
#endif
#include <arpa/inet.h>
#include <netdb.h>

extern char *krb5_kdc_udp_portname;
extern char *krb5_kdc_sec_udp_portname;
extern int errno;

static int udp_port_fd = -1;
static int sec_udp_port_fd = -1;
static fd_set select_fds;
static int select_nfsd;

krb5_error_code
setup_network(prog)
const char *prog;
{
    struct servent *sp;
    struct sockaddr_in sin;
    krb5_error_code retval;

    FD_ZERO(&select_fds);
    select_nfsd = 0;
    sp = getservbyname(krb5_kdc_udp_portname, "udp");
    if (!sp) {
	com_err(prog, 0, "%s/udp service unknown\n",
		krb5_kdc_udp_portname);
	return KDC5_NOPORT;
    }
    if ((udp_port_fd = socket(PF_INET, SOCK_DGRAM, 0)) == -1) {
	retval = errno;
	com_err(prog, 0, "Cannot create server socket");
	return retval;
    }
    memset((char *)&sin, 0, sizeof(sin));
    sin.sin_port = sp->s_port;
    if (bind(udp_port_fd, (struct sockaddr *)&sin, sizeof(sin)) == -1) {
	retval = errno;
	com_err(prog, 0, "Cannot bind server socket");
	return retval;
    }
    FD_SET(udp_port_fd, &select_fds);
    if (udp_port_fd+1 > select_nfsd)
	    select_nfsd = udp_port_fd+1;

    /*
     * Now we set up the secondary listening port, if it is enabled
     */
    if (!krb5_kdc_sec_udp_portname)
	    return 0;		/* No secondary listening port defined */

    sp = getservbyname(krb5_kdc_sec_udp_portname, "udp");
    if (!sp) {
	com_err(prog, 0, "%s/udp service unknown\n",
		krb5_kdc_sec_udp_portname);
	return 0;		/* Don't give an error if we can't */
				/* find it */
    }
    if ((sec_udp_port_fd = socket(PF_INET, SOCK_DGRAM, 0)) == -1) {
	com_err(prog, errno, "while trying to create secondary server socket");
	return 0;		/* Don't give an error we we can't do this */
    }
    memset((char *)&sin, 0, sizeof(sin));
    sin.sin_port = sp->s_port;
    if (bind(sec_udp_port_fd, (struct sockaddr *)&sin, sizeof(sin)) == -1) {
	com_err(prog, errno, "while trying to bind secondary server socket");
	return 0;		/* Don't give an error if we can't do this */
    }
    FD_SET(sec_udp_port_fd, &select_fds);
    if (sec_udp_port_fd+1 > select_nfsd)
	    select_nfsd = sec_udp_port_fd+1;
    
    return 0;
}

void process_packet(port_fd, prog, is_secondary)
    int	port_fd;
    char	*prog;
    int		is_secondary;
{
    int cc, saddr_len;
    krb5_fulladdr faddr;
    krb5_error_code retval;
    struct sockaddr_in saddr;
    krb5_address addr;
    krb5_data request;
    krb5_data *response;
    char pktbuf[MAX_DGRAM_SIZE];

    if (port_fd < 0)
	return;
    
    saddr_len = sizeof(saddr);
    cc = recvfrom(port_fd, pktbuf, sizeof(pktbuf), 0,
		  (struct sockaddr *)&saddr, &saddr_len);
    if (cc == -1) {
	if (errno != EINTR)
	    com_err(prog, errno, "while receiving from network");
	return;
    }
    if (!cc)
	return;		/* zero-length packet? */

    request.length = cc;
    request.data = pktbuf;
    faddr.port = ntohs(saddr.sin_port);
    faddr.address = &addr;
    addr.addrtype = ADDRTYPE_INET;
    addr.length = 4;
    /* this address is in net order */
    addr.contents = (krb5_octet *) &saddr.sin_addr;
    if (retval = dispatch(&request, &faddr, is_secondary, &response)) {
	com_err(prog, retval, "while dispatching");
	return;
    }
    cc = sendto(port_fd, response->data, response->length, 0,
		(struct sockaddr *)&saddr, saddr_len);
    if (cc == -1) {
        krb5_free_data(kdc_context, response);
	com_err(prog, errno, "while sending reply to %s/%d",
		inet_ntoa(saddr.sin_addr), ntohs(saddr.sin_port));
	return;
    }
    if (cc != response->length) {
	krb5_free_data(kdc_context, response);
	com_err(prog, 0, "short reply write %d vs %d\n",
		response->length, cc);
	return;
    }
    krb5_free_data(kdc_context, response);
    return;
}

krb5_error_code
listen_and_process(prog)
const char *prog;
{
    int			nfound;
    fd_set		readfds;

    if (udp_port_fd == -1)
	return KDC5_NONET;
    
    while (!signal_requests_exit) {
	readfds = select_fds;
	nfound = select(select_nfsd, &readfds, 0, 0, 0);
	if (nfound == -1) {
	    if (errno == EINTR)
		continue;
	    com_err(prog, errno, "while selecting for network input");
	    continue;
	}
	if (FD_ISSET(udp_port_fd, &readfds))
	    process_packet(udp_port_fd, prog, 0);

	if ((sec_udp_port_fd > 0) && FD_ISSET(sec_udp_port_fd, &readfds))
	    process_packet(sec_udp_port_fd, prog, 1);
    }
    return 0;
}

krb5_error_code
closedown_network(prog)
const char *prog;
{
    if (udp_port_fd == -1)
	return KDC5_NONET;

    (void) close(udp_port_fd);
    udp_port_fd = -1;

    if (sec_udp_port_fd != -1)
	(void) close(sec_udp_port_fd);
    sec_udp_port_fd = -1;
    return 0;
}

#endif /* INET */
