/*
 * clients/klist/klist.c
 *
 * Copyright 1990 by the Massachusetts Institute of Technology.
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
 * List out the contents of your credential cache or keytab.
 */

#include "krb5.h"
#include "com_err.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

extern int optind;
extern char *optarg;
int show_flags = 0, show_time = 0, status_only = 0, show_keys = 0;
int show_etype = 0;
char *defname;
char *progname;
krb5_int32 now;

krb5_context kcontext;

void show_credential KRB5_PROTOTYPE((char *,
				krb5_context,
				krb5_creds *));
	
void do_ccache KRB5_PROTOTYPE((char *));
void do_keytab KRB5_PROTOTYPE((char *));
void printtime KRB5_PROTOTYPE((time_t));
	
#define DEFAULT 0
#define CCACHE 1
#define KEYTAB 2

void usage()
{
     fprintf(stderr, "Usage: %s [[-c] [-f] [-s]] [-k [-t] [-K]] [name]\n",
	     progname); 
     fprintf(stderr, "\t-c specifies credentials cache, -k specifies keytab");
     fprintf(stderr, ", -c is default\n");
     fprintf(stderr, "\toptions for credential caches:\n");
     fprintf(stderr, "\t\t-f shows credentials flags\n");
     fprintf(stderr, "\t\t-s sets exit status based on valid tgt existence\n");
     fprintf(stderr, "\toptions for keytabs:\n");
     fprintf(stderr, "\t\t-t shows keytab entry timestamps\n");
     fprintf(stderr, "\t\t-K shows keytab entry DES keys\n");
     exit(1);
}
 

void
main(argc, argv)
    int argc;
    char **argv;
{
    int code;
    char *name;
    int mode;

    krb5_init_context(&kcontext);
    krb5_init_ets(kcontext);

    progname = (strrchr(*argv, '/') ? strrchr(*argv, '/')+1 : argv[0]);

    argv++;
    name = NULL;
    mode = DEFAULT;
    while (*argv) {
	if ((*argv)[0] != '-') {
	    if (name) usage();
	    name = *argv;
	} else switch ((*argv)[1]) {
	case 'e':
	    show_etype = 1;
	    break;
	case 'f':
	    show_flags = 1;
	    break;
	case 't':
	    show_time = 1;
	    break;
	case 'K':
	    show_keys = 1;
	    break;
	case 's':
	    status_only = 1;
	    break;
	case 'c':
	    if (mode != DEFAULT) usage();
	    mode = CCACHE;
	    break;
	case 'k':
	    if (mode != DEFAULT) usage();
	    mode = KEYTAB;
	    break;
	default:
	    usage();
	    break;
	}
	argv++;
    }

    if (mode == DEFAULT || mode == CCACHE) {
	 if (show_time || show_keys)
	      usage();
    } else {
	 if (show_flags || status_only)
	      usage();
    }

    if ((code = krb5_timeofday(kcontext, &now))) {
	 if (!status_only)
	      com_err(progname, code, "while getting time of day.");
	 exit(1);
    }

    if (mode == DEFAULT || mode == CCACHE)
	 do_ccache(name);
    else
	 do_keytab(name);
}    

void do_keytab(name)
   char *name;
{
     krb5_keytab kt;
     krb5_keytab_entry entry;
     krb5_kt_cursor cursor;
     char buf[BUFSIZ]; /* hopefully large enough for any type */
     char *pname;
     int code;
     
     if (name == NULL) {
	  if ((code = krb5_kt_default(kcontext, &kt))) {
	       com_err(progname, code, "while getting default keytab");
	       exit(1);
	  }
     } else {
	  if ((code = krb5_kt_resolve(kcontext, name, &kt))) {
	       com_err(progname, code, "while resolving keytab %s",
		       name);
	       exit(1);
	  }
     }

     if ((code = krb5_kt_get_name(kcontext, kt, buf, BUFSIZ))) {
	  com_err(progname, code, "while getting keytab name");
	  exit(1);
     }

     printf("Keytab name: %s\n", buf);
     
     if ((code = krb5_kt_start_seq_get(kcontext, kt, &cursor))) {
	  com_err(progname, code, "while starting keytab scan");
	  exit(1);
     }

     if (show_time) {
	  printf("KVNO Timestamp          Principal\n");
	  printf("---- ------------------ -------------------------------------------------------\n");
     } else {
	  printf("KVNO Principal\n");
	  printf("---- --------------------------------------------------------------------------\n");
     }
     
     while ((code = krb5_kt_next_entry(kcontext, kt, &entry, &cursor)) == 0) {
	  if ((code = krb5_unparse_name(kcontext, entry.principal, &pname))) {
	       com_err(progname, code, "while unparsing principal name");
	       exit(1);
	  }
	  printf("%4d ", entry.vno);
	  if (show_time) {
	       printtime(entry.timestamp);
	       printf(" ");
	  }
	  printf("%s", pname);
	  if (show_keys) {
	       printf(" (0x");
	       {
		    int i;
		    for (i = 0; i < entry.key.length; i++)
			 printf("%02x", entry.key.contents[i]);
	       }
	       printf(")");
	  }
	  printf("\n");
	  free(pname);
     }
     if (code && code != KRB5_KT_END) {
	  com_err(progname, code, "while scanning keytab");
	  exit(1);
     }
     if ((code = krb5_kt_end_seq_get(kcontext, kt, &cursor))) {
	  com_err(progname, code, "while ending keytab scan");
	  exit(1);
     }
     exit(0);
}
void do_ccache(name)
   char *name;
{
    krb5_ccache cache = NULL;
    krb5_cc_cursor cur;
    krb5_creds creds;
    krb5_principal princ;
    krb5_flags flags;
    krb5_error_code code;
    int	exit_status = 0;
	    
    if (status_only)
	/* exit_status is set back to 0 if a valid tgt is found */
	exit_status = 1;

    if (name == NULL) {
	if ((code = krb5_cc_default(kcontext, &cache))) {
	    if (!status_only)
		com_err(progname, code, "while getting default ccache");
	    exit(1);
	    }
    } else {
	if ((code = krb5_cc_resolve(kcontext, name, &cache))) {
	    if (!status_only)
		com_err(progname, code, "while resolving ccache %s",
			name);
	    exit(1);
	}
    }
 
    flags = 0;				/* turns off OPENCLOSE mode */
    if ((code = krb5_cc_set_flags(kcontext, cache, flags))) {
	if (code == ENOENT) {
	    if (!status_only)
		com_err(progname, code, "(ticket cache %s)",
			krb5_cc_get_name(kcontext, cache));
	} else {
	    if (!status_only)
		com_err(progname, code,
			"while setting cache flags (ticket cache %s)",
			krb5_cc_get_name(kcontext, cache));
	}
	exit(1);
    }
    if ((code = krb5_cc_get_principal(kcontext, cache, &princ))) {
	if (!status_only)
	    com_err(progname, code, "while retrieving principal name");
	exit(1);
    }
    if ((code = krb5_unparse_name(kcontext, princ, &defname))) {
	if (!status_only)
	    com_err(progname, code, "while unparsing principal name");
	exit(1);
    }
    if (!status_only) {
	printf("Ticket cache: %s\nDefault principal: %s\n\n",
	       krb5_cc_get_name(kcontext, cache), defname);
	fputs("  Valid starting       Expires          Service principal\n",
	      stdout);
    }
    if ((code = krb5_cc_start_seq_get(kcontext, cache, &cur))) {
	if (!status_only)
	    com_err(progname, code, "while starting to retrieve tickets");
	exit(1);
    }
    while (!(code = krb5_cc_next_cred(kcontext, cache, &cur, &creds))) {
	if (status_only) {
	    if (exit_status && creds.server->length == 2 &&
		strcmp(creds.server->realm.data, princ->realm.data) == 0 &&
		strcmp((char *)creds.server->data[0].data, "krbtgt") == 0 &&
		strcmp((char *)creds.server->data[1].data,
		       princ->realm.data) == 0 && 
		creds.times.endtime > now)
		exit_status = 0;
	} else {
	    show_credential(progname, kcontext, &creds);
	}
	krb5_free_cred_contents(kcontext, &creds);
    }
    if (code == KRB5_CC_END) {
	if ((code = krb5_cc_end_seq_get(kcontext, cache, &cur))) {
	    if (!status_only)
		com_err(progname, code, "while finishing ticket retrieval");
	    exit(1);
	}
	flags = KRB5_TC_OPENCLOSE;	/* turns on OPENCLOSE mode */
	if ((code = krb5_cc_set_flags(kcontext, cache, flags))) {
	    if (!status_only)
		com_err(progname, code, "while closing ccache");
	    exit(1);
	}
	exit(exit_status);
    } else {
	if (!status_only)
	    com_err(progname, code, "while retrieving a ticket");
	exit(1);
    }	
}

char *
flags_string(cred)
    register krb5_creds *cred;
{
    static char buf[32];
    int i = 0;
	
    if (cred->ticket_flags & TKT_FLG_FORWARDABLE)
        buf[i++] = 'F';
    if (cred->ticket_flags & TKT_FLG_FORWARDED)
        buf[i++] = 'f';
    if (cred->ticket_flags & TKT_FLG_PROXIABLE)
        buf[i++] = 'P';
    if (cred->ticket_flags & TKT_FLG_PROXY)
        buf[i++] = 'p';
    if (cred->ticket_flags & TKT_FLG_MAY_POSTDATE)
        buf[i++] = 'D';
    if (cred->ticket_flags & TKT_FLG_POSTDATED)
        buf[i++] = 'd';
    if (cred->ticket_flags & TKT_FLG_INVALID)
        buf[i++] = 'i';
    if (cred->ticket_flags & TKT_FLG_RENEWABLE)
        buf[i++] = 'R';
    if (cred->ticket_flags & TKT_FLG_INITIAL)
        buf[i++] = 'I';
    if (cred->ticket_flags & TKT_FLG_HW_AUTH)
        buf[i++] = 'H';
    if (cred->ticket_flags & TKT_FLG_PRE_AUTH)
        buf[i++] = 'A';
    buf[i] = '\0';
    return(buf);
}

static  char *Month_names[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
				"Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

void 
printtime(tv)
    time_t tv;
{
    struct tm *stime;

    stime = localtime((time_t *)&tv);
    printf("%2d-%s-%2d %02d:%02d:%02d",
           stime->tm_mday,
           Month_names[stime->tm_mon],
           stime->tm_year,
           stime->tm_hour,
           stime->tm_min,
           stime->tm_sec);
}

/* Make sure this list matches the ETYPE order in encryption.h */
#define ETYPE_MAX 6
char * etype_string[ETYPE_MAX] = {
    "ETYPE_NULL", 
    "ETYPE_DES_CBC_CRC", 
    "ETYPE_DES_CBC_MD4", 
    "ETYPE_DES_CBC_MD5", 
    "ETYPE_RAW_DES_CBC", 
    NULL };

void
show_credential(progname, kcontext, cred)
    char 		* progname;
    krb5_context  	  kcontext;
    register krb5_creds * cred;
{
    krb5_error_code retval;
    char *name, *sname, *flags;
    int	first = 1;

    retval = krb5_unparse_name(kcontext, cred->client, &name);
    if (retval) {
	com_err(progname, retval, "while unparsing client name");
	return;
    }
    retval = krb5_unparse_name(kcontext, cred->server, &sname);
    if (retval) {
	com_err(progname, retval, "while unparsing server name");
	free(name);
	return;
    }
    if (!cred->times.starttime)
	cred->times.starttime = cred->times.authtime;

    printtime(cred->times.starttime);
    putchar(' '); putchar(' ');
    printtime(cred->times.endtime);
    putchar(' '); putchar(' ');

    printf("%s\n", sname);

    if (strcmp(name, defname)) {
	    printf("\tfor client %s", name);
	    first = 0;
    }
    
    if (cred->times.renew_till) {
	if (first)
		fputs("\t",stdout);
	else
		fputs(", ",stdout);
	fputs("renew until ", stdout);
        printtime(cred->times.renew_till);
	first = 0;
    }

    if (show_etype) {
	krb5_enctype etype = cred->keyblock.etype;

	if (!first) 
	    putchar('\n');

	printf("\tEncryption type: ");
	if (etype != ETYPE_UNKNOWN) {
	    if ((etype < ETYPE_MAX) && etype_string[etype]) {
		printf("%s", etype_string[etype]);
	    } else {
		printf("UNRECOGNIZED");
	    }
	} else {
	    printf("ETYPE_UNKNOWN");
	}
    }

    if (show_flags) {
	flags = flags_string(cred);
	if (flags && *flags) {
	    if (first)
		fputs("\t",stdout);
	    else
		fputs(", ",stdout);
	    printf("Flags: %s", flags);
	    first = 0;
        }
    }

    /* if any additional info was printed, first is zero */
    if (!first)
	putchar('\n');
    free(name);
    free(sname);
}

