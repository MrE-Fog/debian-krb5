/*
 * lib/kdb/t_kdb.c
 *
 * Copyright 1995 by the Massachusetts Institute of Technology.
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
 */

/*
 * t_kdb.c	- Test [and optionally obtain timing information about] the
 *		  Kerberos database functions.
 */

#include "k5-int.h"
#include <sys/time.h>

#if	HAVE_SRAND48
#define	RAND()		lrand48()
#define	SRAND(a)	srand48(a)
#define	RAND_TYPE	long
#elif	HAVE_SRAND
#define	RAND()		rand()
#define	SRAND(a)	srand(a)
#define	RAND_TYPE	int
#elif	HAVE_SRANDOM
#define	RAND()		random()
#define	SRAND(a)	srandom(a)
#define	RAND_TYPE	long
#else	/* no random */
need a random number generator
#endif	/* no random */

#define	T_KDB_N_PASSES	100
#define	T_KDB_DEF_DB	"test_db"
#define	MAX_PNAME_LEN	1024
#define	MAX_PRINC_COMPS	8
#define	MAX_COMP_SIZE	32

#define	RANDOM(a,b)	(a + (RAND() % (b-a)))

char			*programname = (char *) NULL;
krb5_data		mprinc_data_entries[] = {
    { 0, sizeof("master")-1, "master"},
    { 0, sizeof("key")-1, "key"}
};

krb5_principal_data	master_princ_data = {
    0,						/* Magic number		*/
    { 0, sizeof("test.realm")-1, "test.realm"},	/* Realm		*/
    mprinc_data_entries,			/* Name/instance	*/
    sizeof(mprinc_data_entries)/
	sizeof(mprinc_data_entries[0]),		/* Number		*/
    KRB5_NT_SRV_INST				/* Type			*/
};

struct timeval	tstart_time, tend_time;
struct timezone	dontcare;
krb5_principal	*recorded_principals = (krb5_principal *) NULL;
char		**recorded_names = (char **) NULL;

/*
 * Timer macros.
 */
#define	swatch_on()	((void) gettimeofday(&tstart_time, &dontcare))
#define	swatch_eltime()	((gettimeofday(&tend_time, &dontcare)) ? -1.0 :	\
			 (((float) (tend_time.tv_sec -			\
				    tstart_time.tv_sec)) +		\
			  (((float) (tend_time.tv_usec -		\
				     tstart_time.tv_usec))/1000000.0)))

/*
 * Free all principals and names in the recorded names list.
 */
void
free_principals(kcontext, nentries)
    krb5_context	kcontext;
    int 		nentries;
{
    int i;
    if (recorded_principals) {
	for (i=0; i<nentries; i++) {
	    if (recorded_principals[i])
		krb5_free_principal(kcontext, recorded_principals[i]);
	}
	free(recorded_principals);
    }
    recorded_principals = (krb5_principal *) NULL;

    if (recorded_names) {
	for (i=0; i<nentries; i++) {
	    if (recorded_names[i])
		free(recorded_names[i]);
	}
	free(recorded_names);
    }
    recorded_names = (char **) NULL;
}

/*
 * Initialize the recorded names list.
 */
void
init_princ_recording(kcontext, nentries)
    krb5_context	kcontext;
    int 		nentries;
{
    if (recorded_principals = (krb5_principal *)
	malloc(nentries * sizeof(krb5_principal)))
	memset((char *) recorded_principals, 0,
	       nentries * sizeof(krb5_principal));
    if (recorded_names = (char **) malloc(nentries * sizeof(char *)))
	memset((char *) recorded_names, 0, nentries * sizeof(char *));
}

/*
 * Record a principal and name.
 */
void
record_principal(slotno, princ, pname)
    int			slotno;
    krb5_principal	princ;
    char		*pname;
{
    recorded_principals[slotno] = princ;
    recorded_names[slotno] = (char *) malloc(strlen(pname)+1);
    if (recorded_names[slotno])
	strcpy(recorded_names[slotno], pname);
}

#define	playback_principal(slotno)	(recorded_principals[slotno])
#define	playback_name(slotno)		(recorded_names[slotno])

/*
 * See if a principal already exists.
 */
krb5_boolean
principal_found(nvalid, pname)
    int		nvalid;
    char	*pname;
{
    krb5_boolean	found;
    int			i;

    found = 0;
    for (i=0; i<nvalid; i++) {
	if (!strcmp(recorded_names[i], pname)) {
	    found = 1;
	    break;
	}
    }
    return(found);
}

/*
 * Add a principal to the database.
 */
krb5_error_code
add_principal(kcontext, principal, eblock, key, rseed)
    krb5_context	kcontext;
    krb5_principal	principal;
    krb5_encrypt_block	*eblock;
    krb5_keyblock	*key;
    krb5_pointer	rseed;
{
    krb5_error_code		kret;
    krb5_db_entry		dbent;
    krb5_keyblock		*rkey;
    krb5_keyblock		*kp;
    int				nentries = 1;

    memset((char *) &dbent, 0, sizeof(dbent));
    rkey = (krb5_keyblock *) NULL;
    dbent.principal = principal;
    dbent.kvno = 1;
    dbent.max_life = KRB5_KDB_MAX_LIFE;
    dbent.max_renewable_life = KRB5_KDB_MAX_RLIFE;
    dbent.mkvno = 1;
    dbent.expiration = KRB5_KDB_EXPIRATION;
    dbent.mod_name = principal;
    dbent.attributes = KRB5_KDB_DEF_FLAGS;
    if (kret = krb5_timeofday(kcontext, &dbent.mod_date))
	return(kret);

    if (!key) {
	if (kret = krb5_random_key(kcontext, eblock, rseed, &rkey))
	    return(kret);
	kp = rkey;
    }
    else
	kp = key;

    if (kret = krb5_kdb_encrypt_key(kcontext, eblock, kp, &dbent.key))
	goto out;

    if (rkey)
	krb5_free_keyblock(kcontext, rkey);

    dbent.salt_type = KRB5_KDB_SALTTYPE_NORMAL;
    dbent.salt_length = 0;
    dbent.salt = (krb5_octet *) NULL;
    kret = krb5_db_put_principal(kcontext, &dbent, &nentries);
    if (!kret && (nentries != 1))
	kret = KRB5KRB_ERR_GENERIC;
 out:
    if (dbent.key.contents)
	free(dbent.key.contents);
    return(kret);
}

/*
 * Generate a principal name.
 */
krb5_error_code
gen_principal(kcontext, realm, do_rand, n, princp, namep)
    krb5_context	kcontext;
    char		*realm;
    int			do_rand;
    int			n;
    krb5_principal	*princp;
    char		**namep;
{
    static char pnamebuf[MAX_PNAME_LEN];
    static char *instnames[] = {
	"instance1", "xxx2", "whereami3", "ABCDEFG4" };
    static char *princnames[] = {
	"princ1", "user2", "service3", "RANDOM4" };

    krb5_error_code	kret;
    char		*instname;
    char		*princbase;
    int			ncomps;
    int			i, complen, j;
    char		*cp;

    if (do_rand) {
	ncomps = RANDOM(1,MAX_PRINC_COMPS);
	cp = pnamebuf;
	for (i=0; i<ncomps; i++) {
	    complen = RANDOM(1,MAX_COMP_SIZE);
	    for (j=0; j<complen; j++) {
		*cp = (char) RANDOM(0,256);
		while (!isalnum(*cp))
		    *cp = (char) RANDOM(0,256);
		cp++;
	    }
	    *cp = '/';
	    cp++;
	}
	cp[-1] = '@';
	strcpy(cp, realm);
    }
    else {
	instname = instnames[n % (sizeof(instnames)/sizeof(instnames[0]))];
	princbase = princnames[n % (sizeof(princnames)/sizeof(princnames[0]))];
	sprintf(pnamebuf, "%s%d/%s@%s", princbase, n, instname, realm);
    }
    kret = krb5_parse_name(kcontext, pnamebuf, princp);
    *namep = (!kret) ? pnamebuf : (char *) NULL;
    return(kret);
}

/*
 * Find a principal in the database.
 */
krb5_error_code
find_principal(kcontext, principal, docompare)
    krb5_context	kcontext;
    krb5_principal	principal;
    krb5_boolean	docompare;
{
    krb5_error_code	kret;
    krb5_db_entry	dbent;
    int			how_many;
    krb5_boolean	more;

    how_many = 1;
    more = 0;
    if (kret = krb5_db_get_principal(kcontext,
				     principal,
				     &dbent,
				     &how_many,
				     &more))
	return(kret);
    if (docompare) {
	if ((dbent.kvno != 1) ||
	    (dbent.max_life != KRB5_KDB_MAX_LIFE) ||
	    (dbent.max_renewable_life != KRB5_KDB_MAX_RLIFE) ||
	    (dbent.mkvno != 1) ||
	    (dbent.expiration != KRB5_KDB_EXPIRATION) ||
	    (dbent.attributes != KRB5_KDB_DEF_FLAGS) ||
	    !krb5_principal_compare(kcontext, principal, dbent.principal) ||
	    !krb5_principal_compare(kcontext, principal, dbent.mod_name))
	    kret = KRB5_PRINC_NOMATCH;
	krb5_db_free_principal(kcontext, &dbent, how_many);
    }
    if (!kret && how_many)
	krb5_db_free_principal(kcontext, &dbent, how_many);
    return(((how_many == 1) && (more == 0)) ? 0 : KRB5KRB_ERR_GENERIC);
}

/*
 * Delete a principal.
 */
krb5_error_code
delete_principal(kcontext, principal)
    krb5_context	kcontext;
    krb5_principal	principal;
{
    krb5_error_code	kret;
    int			num2delete;

    num2delete = 1;
    if (kret = krb5_db_delete_principal(kcontext,
					principal,
					&num2delete))
	return(kret);
    return((num2delete == 1) ? 0 : KRB5KRB_ERR_GENERIC);
}

int
do_testing(db, passes, verbose, timing, rcases, check, save_db)
    char	*db;
    int		passes;
    int		verbose;
    int		timing;
    int		rcases;
    int		check;
    int		save_db;
{
    krb5_error_code	kret;
    krb5_context	kcontext;
    char		*op, *linkage, *oparg;
    krb5_principal	master_princ;
    char		*mkey_name;
    char		*realm;
    char		*mkey_fullname;
    char		*master_passwd;
    krb5_data		salt_data;
    krb5_encrypt_block	master_encblock;
    krb5_keyblock	master_keyblock;
    krb5_data		passwd;
    krb5_pointer	rseed;
    krb5_boolean	db_open, db_created;
    int			passno;
    krb5_principal	principal;
    char		*pname;
    float		elapsed;
    krb5_keyblock	stat_kb;

    mkey_name = "master/key";
    realm = master_princ_data.realm.data;
    mkey_fullname = (char *) NULL;
    master_princ = (krb5_principal) NULL;
    master_passwd = "master_password";
    db_open = 0;
    db_created = 0;
    linkage = "";
    oparg = "";

    /* Set up some initial context */
    krb5_init_context(&kcontext);
    krb5_init_ets(kcontext);

    /* 
     * The database had better not exist.
     */
    op = "making sure database doesn't exist";
    if (!(kret = krb5_db_set_name(kcontext, db))) {
	kret = EEXIST;
	goto goodbye;
    }

    /* Set up the master key name */
    op = "setting up master key name";
    if (kret = krb5_db_setup_mkey_name(kcontext,
				       mkey_name,
				       realm,
				       &mkey_fullname,
				       &master_princ))
	goto goodbye;

    if (verbose)
	fprintf(stdout, "%s: Initializing '%s', master key is '%s'\n",
		programname, db, mkey_fullname);

    op = "salting master key";
    if (kret = krb5_principal2salt(kcontext, master_princ, &salt_data))
	goto goodbye;

    op = "converting master key";
    krb5_use_cstype(kcontext, &master_encblock, DEFAULT_KDC_ETYPE);
    master_keyblock.keytype = DEFAULT_KDC_KEYTYPE;
    passwd.length = strlen(master_passwd);
    passwd.data = master_passwd;
    if (kret = krb5_string_to_key(kcontext,
				  &master_encblock,
				  master_keyblock.keytype,
				  &master_keyblock,
				  &passwd,
				  &salt_data))
	goto goodbye;
    /* Clean up */
    free(salt_data.data);

    /* Process master key */
    op = "processing master key";
    if (kret = krb5_process_key(kcontext, &master_encblock, &master_keyblock))
	goto goodbye;

    /* Initialize random key generator */
    op = "initializing random key generator";
    if (kret = krb5_init_random_key(kcontext,
				    &master_encblock,
				    &master_keyblock,
				    &rseed))
	goto goodbye;

    /* Create database */
    op = "creating database";
    if (kret = krb5_db_create(kcontext, db))
	goto goodbye;

    db_created = 1;

    /* Set this database as active. */
    op = "setting active database";
    if (kret = krb5_db_set_name(kcontext, db))
	goto goodbye;

    /* Initialize database */
    op = "initializing database";
    if (kret = krb5_db_init(kcontext))
	goto goodbye;

    db_open = 1;
    op = "adding master principal";
    if (kret = add_principal(kcontext,
			     master_princ,
			     &master_encblock,
			     &master_keyblock,
			     rseed))
	goto goodbye;


    stat_kb.keytype = DEFAULT_KDC_KEYTYPE;
    stat_kb.etype = DEFAULT_KDC_ETYPE;
    stat_kb.length = 8;
    stat_kb.contents = (krb5_octet *) "helpmeee";

    /* We are now ready to proceed to test. */
    if (verbose)
	fprintf(stdout, "%s: Beginning %stest\n",
		programname, (rcases) ? "random " : "");
    init_princ_recording(kcontext, passes);
    if (rcases) {
	struct tacc {
	    float	t_time;
	    int		t_number;
	} accumulated[3];
	int 		i, nvalid, discrim, highwater, coinflip;
	krb5_keyblock	*kbp;

	/* Generate random cases */
	for (i=0; i<3; i++) {
	    accumulated[i].t_time = 0.0;
	    accumulated[i].t_number = 0;
	}

	/*
	 * Generate principal names.
	 */
	if (verbose > 1)
	    fprintf(stdout, "%s: generating %d names\n",
		    programname, passes);
	for (passno=0; passno<passes; passno++) {
	    op = "generating principal name";
	    do {
		if (kret = gen_principal(kcontext,
					 realm,
					 rcases,
					 passno,
					 &principal,
					 &pname))
		    goto goodbye;
	    } while (principal_found(passno-1, pname));
	    record_principal(passno, principal, pname);
	}

	/* Prime the database with some number of entries */
	nvalid = passes/4;
	if (nvalid < 10)
	    nvalid = 10;
	if (nvalid > passes)
	    nvalid = passes;

	if (verbose > 1)
	    fprintf(stdout, "%s: priming database with %d principals\n",
		    programname, nvalid);
	highwater = 0;
	for (passno=0; passno<nvalid; passno++) {
	    op = "adding principal";
	    coinflip = RANDOM(0,2);
	    kbp = (coinflip) ? &stat_kb : (krb5_keyblock *) NULL;
	    if (timing) {
		swatch_on();
	    }
	    if (kret = add_principal(kcontext,
				     playback_principal(passno),
				     &master_encblock,
				     kbp,
				     rseed)) {
		linkage = "initially ";
		oparg = playback_name(passno);
		goto cya;
	    }
	    if (timing) {
		elapsed = swatch_eltime();
		accumulated[0].t_time += elapsed;
		accumulated[0].t_number++;
	    }
	    if (verbose > 4)
		fprintf(stderr, "*A(%s)\n", playback_name(passno));
	    highwater++;
	}

	if (verbose > 1)
	    fprintf(stderr, "%s: beginning random loop\n", programname);
	/* Loop through some number of times and pick random operations */
	for (i=0; i<3*passes; i++) {
	    discrim = RANDOM(0,100);

	    /* Add a principal 25% of the time, if possible */
	    if ((discrim < 25) && (nvalid < passes)) {
		op = "adding principal";
		coinflip = RANDOM(0,2);
		kbp = (coinflip) ? &stat_kb : (krb5_keyblock *) NULL;
		if (timing) {
		    swatch_on();
		}
		if (kret = add_principal(kcontext,
					 playback_principal(nvalid),
					 &master_encblock,
					 kbp,
					 rseed)) {
		    oparg = playback_name(nvalid);
		    goto cya;
		}
		if (timing) {
		    elapsed = swatch_eltime();
		    accumulated[0].t_time += elapsed;
		    accumulated[0].t_number++;
		}
		if (verbose > 4)
		    fprintf(stderr, "*A(%s)\n", playback_name(nvalid));
		nvalid++;
		if (nvalid > highwater)
		    highwater = nvalid;
	    }
	    /* Delete a principal 15% of the time, if possible */
	    else if ((discrim > 85) && (nvalid > 10)) {
		op = "deleting principal";
		if (timing) {
		    swatch_on();
		}
		if (kret = delete_principal(kcontext,
					    playback_principal(nvalid-1))) {
		    oparg = playback_name(nvalid-1);
		    goto cya;
		}
		if (timing) {
		    elapsed = swatch_eltime();
		    accumulated[2].t_time += elapsed;
		    accumulated[2].t_number++;
		}
		if (verbose > 4)
		    fprintf(stderr, "XD(%s)\n", playback_name(nvalid-1));
		nvalid--;
	    }
	    /* Otherwise, find a principal */
	    else {
		op = "looking up principal";
		passno = RANDOM(0, nvalid);
		if (timing) {
		    swatch_on();
		}
		if (kret = find_principal(kcontext,
					  playback_principal(passno),
					  check)) {
		    oparg = playback_name(passno);
		    goto cya;
		}
		if (timing) {
		    elapsed = swatch_eltime();
		    accumulated[1].t_time += elapsed;
		    accumulated[1].t_number++;
		}
		if (verbose > 4)
		    fprintf(stderr, "-S(%s)\n", playback_name(passno));
	    }
	}

	/* Clean up the remaining principals */
	if (verbose > 1)
	    fprintf(stdout, "%s: deleting remaining %d principals\n",
		    programname, nvalid);
	for (passno=0; passno<nvalid; passno++) {
	    op = "deleting principal";
	    if (timing) {
		swatch_on();
	    }
	    if (kret = delete_principal(kcontext,
					playback_principal(passno))) {
		linkage = "finally ";
		oparg = playback_name(passno);
		goto cya;
	    }
	    if (timing) {
		elapsed = swatch_eltime();
		accumulated[2].t_time += elapsed;
		accumulated[2].t_number++;
	    }
	    if (verbose > 4)
		fprintf(stderr, "XD(%s)\n", playback_name(passno));
	}
    cya:
	if (verbose)
	    fprintf(stdout,
		    "%s: highwater mark was %d principals\n",
		    programname, highwater);
	if (accumulated[0].t_number && timing)
	    fprintf(stdout,
		    "%s: performed %8d additions in %9.4f seconds (%9.4f/add)\n",
		    programname, accumulated[0].t_number,
		    accumulated[0].t_time, 
		    accumulated[0].t_time / (float) accumulated[0].t_number);
	if (accumulated[1].t_number && timing)
	    fprintf(stdout,
		    "%s: performed %8d lookups   in %9.4f seconds (%9.4f/search)\n",
		    programname, accumulated[1].t_number,
		    accumulated[1].t_time, 
		    accumulated[1].t_time / (float) accumulated[1].t_number);
	if (accumulated[2].t_number && timing)
	    fprintf(stdout,
		    "%s: performed %8d deletions in %9.4f seconds (%9.4f/delete)\n",
		    programname, accumulated[2].t_number,
		    accumulated[2].t_time, 
		    accumulated[2].t_time / (float) accumulated[2].t_number);
	if (kret)
	    goto goodbye;
    }
    else {
	/*
	 * Generate principal names.
	 */
	for (passno=0; passno<passes; passno++) {
	    op = "generating principal name";
	    if (kret = gen_principal(kcontext,
				     realm,
				     rcases,
				     passno,
				     &principal,
				     &pname))
		goto goodbye;
	    record_principal(passno, principal, pname);
	}
	/*
	 * Add principals.
	 */
	if (timing) {
	    swatch_on();
	}
	for (passno=0; passno<passes; passno++) {
	    op = "adding principal";
	    if (kret = add_principal(kcontext,
				     playback_principal(passno),
				     &master_encblock,
				     &stat_kb,
				     rseed))
		goto goodbye;
	    if (verbose > 4)
		fprintf(stderr, "*A(%s)\n", playback_name(passno));
	}
	if (timing) {
	    elapsed = swatch_eltime();
	    fprintf(stdout,
		    "%s:   added %d principals in %9.4f seconds (%9.4f/add)\n",
		    programname, passes, elapsed, elapsed/((float) passes));
	}

	/*
	 * Lookup principals.
	 */
	if (timing) {
	    swatch_on();
	}
	for (passno=0; passno<passes; passno++) {
	    op = "looking up principal";
	    if (kret = find_principal(kcontext,
				      playback_principal(passno),
				      check))
		goto goodbye;
	    if (verbose > 4)
		fprintf(stderr, "-S(%s)\n", playback_name(passno));
	}
	if (timing) {
	    elapsed = swatch_eltime();
	    fprintf(stdout,
		    "%s:   found %d principals in %9.4f seconds (%9.4f/search)\n",
		    programname, passes, elapsed, elapsed/((float) passes));
	}

	/*
	 * Delete principals.
	 */
	if (timing) {
	    swatch_on();
	}
	for (passno=passes-1; passno>=0; passno--) {
	    op = "deleting principal";
	    if (kret = delete_principal(kcontext,
					playback_principal(passno)))
		goto goodbye;
	    if (verbose > 4)
		fprintf(stderr, "XD(%s)\n", playback_name(passno));
	}
	if (timing) {
	    elapsed = swatch_eltime();
	    fprintf(stdout,
		    "%s: deleted %d principals in %9.4f seconds (%9.4f/delete)\n",
		    programname, passes, elapsed, elapsed/((float) passes));
	}
    }

 goodbye:
    if (kret)
	fprintf(stderr, "%s: error while %s %s%s(%s)\n",
		programname, op, linkage, oparg, error_message(kret));
    free_principals(kcontext, passes);
    if (db_open)
	(void) krb5_db_fini(kcontext);
    if (db_created) {
	char *fnbuf;

	if (!kret && !save_db) {
	    fnbuf = (char *) malloc(MAXPATHLEN);
	    if (fnbuf) {
		sprintf(fnbuf, "%s.ok", db);
		unlink(fnbuf);
		sprintf(fnbuf, "%s.dir", db);
		unlink(fnbuf);
		sprintf(fnbuf, "%s.pag", db);
		unlink(fnbuf);
		sprintf(fnbuf, "%s.db", db);
		unlink(fnbuf);
		free(fnbuf);
	    }
	}
	else {
	    if (kret && verbose)
		fprintf(stderr, "%s: database not deleted because of error\n",
			programname);
	}
    }
    return((kret) ? 1 : 0);
}

/*
 * usage:
 *	t_kdb	[-t]		- Get timing information.
 *		[-r]		- Generate random cases.
 *		[-n <num>]	- Use <num> as the number of passes.
 *		[-c]		- Check contents.
 *		[-v]		- Verbose output.
 *		[-d <dbname>]	- Database name.
 *		[-s]		- Save database even on successful completion.
 */
int
main(argc, argv)
    int		argc;
    char	*argv[];
{
    char	option;
    extern char	*optarg;

    int		do_time, do_random, num_passes, check_cont, verbose, error;
    int		save_db;
    char	*db_name;

    programname = argv[0];
    if (strrchr(programname, (int) '/'))
	programname = strrchr(programname, (int) '/') + 1;
    SRAND((RAND_TYPE)time((void *) NULL));

    /* Default values. */
    do_time = 0;
    do_random = 0;
    num_passes = T_KDB_N_PASSES;
    check_cont = 0;
    verbose = 0;
    db_name = T_KDB_DEF_DB;
    save_db = 0;
    error = 0;

    /* Parse argument list */
    while ((option = getopt(argc, argv, "cd:n:rstv")) != EOF) {
	switch (option) {
	case 'c':
	    check_cont = 1;
	    break;
	case 'd':
	    db_name = optarg;
	    break;
	case 'n':
	    if (sscanf(optarg, "%d", &num_passes) != 1) {
		fprintf(stderr, "%s: %s is not a valid number for %c option\n",
			programname, optarg, option);
		error++;
	    }
	    break;
	case 'r':
	    do_random = 1;
	    break;
	case 's':
	    save_db = 1;
	    break;
	case 't':
	    do_time = 1;
	    break;
	case 'v':
	    verbose++;
	    break;
	default:
	    error++;
	    break;
	}
    }
    if (error)
	fprintf(stderr, "%s: usage is %s [-crstv] [-d <dbname>] [-n <num>]\n",
		programname, programname);
    else
	error = do_testing(db_name,
			   num_passes,
			   verbose,
			   do_time,
			   do_random,
			   check_cont,
			   save_db);
    return(error);
}
