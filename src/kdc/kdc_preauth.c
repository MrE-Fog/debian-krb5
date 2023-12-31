/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/* kdc/kdc_preauth.c - Preauthentication routines for the KDC */
/*
 * Copyright 1995, 2003, 2007, 2009 by the Massachusetts Institute of
 * Technology.  All Rights Reserved.
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
 * permission.  Furthermore if you modify this software you must label
 * your software as modified software and not distribute it in such a
 * fashion that it might be confused with the original M.I.T. software.
 * M.I.T. makes no representations about the suitability of
 * this software for any purpose.  It is provided "as is" without express
 * or implied warranty.
 */
/*
 * Copyright (C) 1998 by the FundsXpress, INC.
 *
 * All rights reserved.
 *
 * Export of this software from the United States of America may require
 * a specific license from the United States Government.  It is the
 * responsibility of any person or organization contemplating export to
 * obtain such a license before exporting.
 *
 * WITHIN THAT CONSTRAINT, permission to use, copy, modify, and
 * distribute this software and its documentation for any purpose and
 * without fee is hereby granted, provided that the above copyright
 * notice appear in all copies and that both that copyright notice and
 * this permission notice appear in supporting documentation, and that
 * the name of FundsXpress. not be used in advertising or publicity pertaining
 * to distribution of the software without specific, written prior
 * permission.  FundsXpress makes no representations about the suitability of
 * this software for any purpose.  It is provided "as is" without express
 * or implied warranty.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */
/*
 * Copyright (c) 2006-2008, Novell, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *       this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *   * The copyright holder's name is not used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "k5-int.h"
#include "kdc_util.h"
#include "extern.h"
#include <stdio.h>
#include "adm_proto.h"
#if APPLE_PKINIT
#include "pkinit_server.h"
#include "pkinit_cert_store.h"
#endif /* APPLE_PKINIT */

#include <syslog.h>

#include <assert.h>
#include "../include/krb5/preauth_plugin.h"

typedef struct preauth_system_st {
    const char *name;
    int type;
    int flags;
    krb5_kdcpreauth_moddata moddata;
    krb5_kdcpreauth_init_fn init;
    krb5_kdcpreauth_fini_fn fini;
    krb5_kdcpreauth_edata_fn get_edata;
    krb5_kdcpreauth_verify_fn verify_padata;
    krb5_kdcpreauth_return_fn return_padata;
    krb5_kdcpreauth_free_modreq_fn free_modreq;
} preauth_system;

static void
get_etype_info(krb5_context context, krb5_kdc_req *request,
               krb5_kdcpreauth_callbacks cb, krb5_kdcpreauth_rock rock,
               krb5_kdcpreauth_moddata moddata, krb5_preauthtype pa_type,
               krb5_kdcpreauth_edata_respond_fn respond, void *arg);

static void
get_etype_info2(krb5_context context, krb5_kdc_req *request,
                krb5_kdcpreauth_callbacks cb, krb5_kdcpreauth_rock rock,
                krb5_kdcpreauth_moddata moddata, krb5_preauthtype pa_type,
                krb5_kdcpreauth_edata_respond_fn respond, void *arg);

static krb5_error_code
etype_info_as_rep_helper(krb5_context context, krb5_pa_data * padata,
                         krb5_db_entry *client,
                         krb5_kdc_req *request, krb5_kdc_rep *reply,
                         krb5_key_data *client_key,
                         krb5_keyblock *encrypting_key,
                         krb5_pa_data **send_pa,
                         int etype_info2);

static krb5_error_code
return_etype_info(krb5_context, krb5_pa_data *padata,
                  krb5_data *req_pkt, krb5_kdc_req *request,
                  krb5_kdc_rep *reply, krb5_keyblock *encrypting_key,
                  krb5_pa_data **send_pa, krb5_kdcpreauth_callbacks cb,
                  krb5_kdcpreauth_rock rock, krb5_kdcpreauth_moddata moddata,
                  krb5_kdcpreauth_modreq modreq);

static krb5_error_code
return_etype_info2(krb5_context, krb5_pa_data *padata,
                   krb5_data *req_pkt, krb5_kdc_req *request,
                   krb5_kdc_rep *reply, krb5_keyblock *encrypting_key,
                   krb5_pa_data **send_pa, krb5_kdcpreauth_callbacks cb,
                   krb5_kdcpreauth_rock rock, krb5_kdcpreauth_moddata moddata,
                   krb5_kdcpreauth_modreq modreq);

static krb5_error_code
return_pw_salt(krb5_context, krb5_pa_data *padata,
               krb5_data *req_pkt, krb5_kdc_req *request, krb5_kdc_rep *reply,
               krb5_keyblock *encrypting_key, krb5_pa_data **send_pa,
               krb5_kdcpreauth_callbacks cb, krb5_kdcpreauth_rock rock,
               krb5_kdcpreauth_moddata moddata, krb5_kdcpreauth_modreq modreq);


#if APPLE_PKINIT
/* PKINIT preauth support */
static krb5_error_code get_pkinit_edata(
    krb5_context context,
    krb5_kdc_req *request,
    krb5_db_entry *client,
    krb5_db_entry *server,
    preauth_get_entry_data_proc get_entry_data,
    void *pa_module_context,
    krb5_pa_data *pa_data);
static krb5_error_code verify_pkinit_request(
    krb5_context context,
    krb5_db_entry *client,
    krb5_data *req_pkt,
    krb5_kdc_req *request,
    krb5_enc_tkt_part *enc_tkt_reply,
    krb5_pa_data *data,
    preauth_get_entry_data_proc get_entry_data,
    void *pa_module_context,
    void **pa_request_context,
    krb5_data **e_data,
    krb5_authdata ***authz_data);
static krb5_error_code return_pkinit_response(
    krb5_context context,
    krb5_pa_data * padata,
    krb5_db_entry *client,
    krb5_data *req_pkt,
    krb5_kdc_req *request,
    krb5_kdc_rep *reply,
    krb5_key_data *client_key,
    krb5_keyblock *encrypting_key,
    krb5_pa_data **send_pa,
    preauth_get_entry_data_proc get_entry_data,
    void *pa_module_context,
    void **pa_request_context);
#endif /* APPLE_PKINIT */

static preauth_system static_preauth_systems[] = {
#if APPLE_PKINIT
    {
        "pkinit",
        KRB5_PADATA_PK_AS_REQ,
        PA_SUFFICIENT,
        NULL,                   /* pa_sys_context */
        NULL,                   /* init */
        NULL,                   /* fini */
        get_pkinit_edata,
        verify_pkinit_request,
        return_pkinit_response,
        NULL                    /* free_modreq */
    },
#endif /* APPLE_PKINIT */
    {
        "FAST",
        KRB5_PADATA_FX_FAST,
        PA_HARDWARE,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        0
    },
    {
        "etype-info",
        KRB5_PADATA_ETYPE_INFO,
        0,
        NULL,
        NULL,
        NULL,
        get_etype_info,
        0,
        return_etype_info
    },
    {
        "etype-info2",
        KRB5_PADATA_ETYPE_INFO2,
        0,
        NULL,
        NULL,
        NULL,
        get_etype_info2,
        0,
        return_etype_info2
    },
    {
        "pw-salt",
        KRB5_PADATA_PW_SALT,
        PA_PSEUDO,              /* Don't include this in the error list */
        NULL,
        NULL,
        NULL,
        0,
        0,
        return_pw_salt
    },
    {
        "pac-request",
        KRB5_PADATA_PAC_REQUEST,
        PA_PSEUDO,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL
    },
#if 0
    {
        "server-referral",
        KRB5_PADATA_SERVER_REFERRAL,
        PA_PSEUDO,
        0,
        0,
        return_server_referral
    },
#endif
};

#define NUM_STATIC_PREAUTH_SYSTEMS (sizeof(static_preauth_systems) /    \
                                    sizeof(*static_preauth_systems))

static preauth_system *preauth_systems;
static size_t n_preauth_systems;

/* Get all available kdcpreauth vtables and a count of preauth types they
 * support.  Return an empty list on failure. */
static void
get_plugin_vtables(krb5_context context,
                   struct krb5_kdcpreauth_vtable_st **vtables_out,
                   size_t *n_tables_out, size_t *n_systems_out)
{
    krb5_plugin_initvt_fn *plugins = NULL, *pl;
    struct krb5_kdcpreauth_vtable_st *vtables;
    size_t count, n_tables, n_systems, i;

    *vtables_out = NULL;
    *n_tables_out = *n_systems_out = 0;

    /* Auto-register encrypted challenge and (if possible) pkinit. */
    k5_plugin_register_dyn(context, PLUGIN_INTERFACE_KDCPREAUTH, "pkinit",
                           "preauth");
    k5_plugin_register(context, PLUGIN_INTERFACE_KDCPREAUTH,
                       "encrypted_challenge",
                       kdcpreauth_encrypted_challenge_initvt);
    k5_plugin_register(context, PLUGIN_INTERFACE_KDCPREAUTH,
                       "encrypted_timestamp",
                       kdcpreauth_encrypted_timestamp_initvt);

    if (k5_plugin_load_all(context, PLUGIN_INTERFACE_KDCPREAUTH, &plugins))
        return;
    for (count = 0; plugins[count]; count++);
    vtables = calloc(count + 1, sizeof(*vtables));
    if (vtables == NULL)
        goto cleanup;
    for (pl = plugins, n_tables = 0; *pl != NULL; pl++) {
        if ((*pl)(context, 1, 1, (krb5_plugin_vtable)&vtables[n_tables]) == 0)
            n_tables++;
    }
    for (i = 0, n_systems = 0; i < n_tables; i++) {
        for (count = 0; vtables[i].pa_type_list[count] > 0; count++);
        n_systems += count;
    }
    *vtables_out = vtables;
    *n_tables_out = n_tables;
    *n_systems_out = n_systems;

cleanup:
    k5_plugin_free_modules(context, plugins);
}

/* Make a list of realm names.  The caller should free the list container but
 * not the list elements (which are aliases into kdc_realmlist). */
static krb5_error_code
get_realm_names(const char ***list_out)
{
    const char **list;
    int i;

    list = calloc(kdc_numrealms + 1, sizeof(*list));
    if (list == NULL)
        return ENOMEM;
    for (i = 0; i < kdc_numrealms; i++)
        list[i] = kdc_realmlist[i]->realm_name;
    list[i] = NULL;
    *list_out = list;
    return 0;
}

void
load_preauth_plugins(krb5_context context)
{
    krb5_error_code ret;
    struct krb5_kdcpreauth_vtable_st *vtables = NULL, *vt;
    size_t n_systems, n_tables, i, j;
    krb5_kdcpreauth_moddata moddata;
    const char **realm_names = NULL, *emsg;
    preauth_system *sys;

    /* Get all available kdcpreauth vtables. */
    get_plugin_vtables(context, &vtables, &n_tables, &n_systems);

    /* Allocate the list of static and plugin preauth systems. */
    n_systems += NUM_STATIC_PREAUTH_SYSTEMS;
    preauth_systems = calloc(n_systems + 1, sizeof(preauth_system));
    if (preauth_systems == NULL)
        goto cleanup;

    if (get_realm_names(&realm_names))
        goto cleanup;

    /* Add the static system to the list first.  No static systems require
     * initialization, so just make a direct copy. */
    memcpy(preauth_systems, static_preauth_systems,
           sizeof(static_preauth_systems));

    /* Add the dynamically-loaded mechanisms to the list. */
    n_systems = NUM_STATIC_PREAUTH_SYSTEMS;
    for (i = 0; i < n_tables; i++) {
        /* Try to initialize this module. */
        vt = &vtables[i];
        moddata = NULL;
        if (vt->init) {
            ret = vt->init(context, &moddata, realm_names);
            if (ret) {
                emsg = krb5_get_error_message(context, ret);
                krb5_klog_syslog(LOG_ERR, _("preauth %s failed to "
                                            "initialize: %s"), vt->name, emsg);
                krb5_free_error_message(context, emsg);
                continue;
            }
        }
        /* Add this module to the systems list once for each pa type. */
        for (j = 0; vt->pa_type_list[j] > 0; j++) {
            sys = &preauth_systems[n_systems];
            sys->name = vt->name;
            sys->type = vt->pa_type_list[j];
            sys->flags = (vt->flags) ? vt->flags(context, sys->type) : 0;
            sys->moddata = moddata;
            sys->init = vt->init;
            /* Only call fini once for each plugin. */
            sys->fini = (j == 0) ? vt->fini : NULL;
            sys->get_edata = vt->edata;
            sys->verify_padata = vt->verify;
            sys->return_padata = vt->return_padata;
            sys->free_modreq = vt->free_modreq;
            n_systems++;
        }
    }
    n_preauth_systems = n_systems;
    /* Add the end-of-list marker. */
    preauth_systems[n_systems].name = "[end]";
    preauth_systems[n_systems].type = -1;

cleanup:
    free(vtables);
    free(realm_names);
}

void
unload_preauth_plugins(krb5_context context)
{
    size_t i;

    for (i = 0; i < n_preauth_systems; i++) {
        if (preauth_systems[i].fini)
            preauth_systems[i].fini(context, preauth_systems[i].moddata);
    }
    free(preauth_systems);
    preauth_systems = NULL;
    n_preauth_systems = 0;
}

/*
 * The make_padata_context() function creates a space for storing any
 * request-specific module data which will be needed by return_padata() later.
 * Each preauth type gets a storage location of its own.
 */
struct request_pa_context {
    int n_contexts;
    struct {
        preauth_system *pa_system;
        krb5_kdcpreauth_modreq modreq;
    } *contexts;
};

static krb5_error_code
make_padata_context(krb5_context context, void **padata_context)
{
    int i;
    struct request_pa_context *ret;

    ret = malloc(sizeof(*ret));
    if (ret == NULL) {
        return ENOMEM;
    }

    ret->n_contexts = n_preauth_systems;
    ret->contexts = malloc(sizeof(ret->contexts[0]) * ret->n_contexts);
    if (ret->contexts == NULL) {
        free(ret);
        return ENOMEM;
    }

    memset(ret->contexts, 0, sizeof(ret->contexts[0]) * ret->n_contexts);

    for (i = 0; i < ret->n_contexts; i++) {
        ret->contexts[i].pa_system = &preauth_systems[i];
        ret->contexts[i].modreq = NULL;
    }

    *padata_context = ret;

    return 0;
}

/*
 * The free_padata_context function frees any context information pointers
 * which the check_padata() function created but which weren't already cleaned
 * up by return_padata().
 */
void
free_padata_context(krb5_context kcontext, void *padata_context)
{
    struct request_pa_context *context = padata_context;
    preauth_system *sys;
    int i;

    if (context == NULL)
        return;
    for (i = 0; i < context->n_contexts; i++) {
        sys = context->contexts[i].pa_system;
        if (!sys->free_modreq || !context->contexts[i].modreq)
            continue;
        sys->free_modreq(kcontext, sys->moddata, context->contexts[i].modreq);
        context->contexts[i].modreq = NULL;
    }

    free(context->contexts);
    free(context);
}

static krb5_deltat
max_time_skew(krb5_context context, krb5_kdcpreauth_rock rock)
{
    return context->clockskew;
}

static krb5_error_code
client_keys(krb5_context context, krb5_kdcpreauth_rock rock,
            krb5_keyblock **keys_out)
{
    krb5_kdc_req *request = rock->request;
    krb5_db_entry *client = rock->client;
    krb5_keyblock *keys, key;
    krb5_key_data *entry_key;
    int i, k;

    keys = malloc(sizeof(krb5_keyblock) * (request->nktypes + 1));
    if (keys == NULL)
        return ENOMEM;

    memset(keys, 0, sizeof(krb5_keyblock) * (request->nktypes + 1));
    k = 0;
    for (i = 0; i < request->nktypes; i++) {
        entry_key = NULL;
        if (krb5_dbe_find_enctype(context, client, request->ktype[i],
                                  -1, 0, &entry_key) != 0)
            continue;
        if (krb5_dbe_decrypt_key_data(context, NULL, entry_key,
                                      &key, NULL) != 0)
            continue;
        keys[k++] = key;
    }
    if (k == 0) {
        free(keys);
        return ENOENT;
    }
    *keys_out = keys;
    return 0;
}

static void free_keys(krb5_context context, krb5_kdcpreauth_rock rock,
                      krb5_keyblock *keys)
{
    krb5_keyblock *k;

    if (keys == NULL)
        return;
    for (k = keys; k->enctype != 0; k++)
        krb5_free_keyblock_contents(context, k);
    free(keys);
}

static krb5_data *
request_body(krb5_context context, krb5_kdcpreauth_rock rock)
{
    return rock->inner_body;
}

static krb5_keyblock *
fast_armor(krb5_context context, krb5_kdcpreauth_rock rock)
{
    return rock->rstate->armor_key;
}

static krb5_error_code
get_string(krb5_context context, krb5_kdcpreauth_rock rock, const char *key,
           char **value_out)
{
    return krb5_dbe_get_string(context, rock->client, key, value_out);
}

static void
free_string(krb5_context context, krb5_kdcpreauth_rock rock, char *string)
{
    krb5_dbe_free_string(context, string);
}

static void *
client_entry(krb5_context context, krb5_kdcpreauth_rock rock)
{
    return rock->client;
}

static verto_ctx *
event_context(krb5_context context, krb5_kdcpreauth_rock rock)
{
    return rock->vctx;
}

static struct krb5_kdcpreauth_callbacks_st callbacks = {
    1,
    max_time_skew,
    client_keys,
    free_keys,
    request_body,
    fast_armor,
    get_string,
    free_string,
    client_entry,
    event_context
};

static krb5_error_code
find_pa_system(int type, preauth_system **preauth)
{
    preauth_system *ap;

    ap = preauth_systems ? preauth_systems : static_preauth_systems;
    while ((ap->type != -1) && (ap->type != type))
        ap++;
    if (ap->type == -1)
        return(KRB5_PREAUTH_BAD_TYPE);
    *preauth = ap;
    return 0;
}

/* Find a pointer to the request-specific module data for pa_sys. */
static krb5_error_code
find_modreq(preauth_system *pa_sys, struct request_pa_context *context,
            krb5_kdcpreauth_modreq **modreq_out)
{
    int i;

    *modreq_out = NULL;
    if (context == NULL)
        return KRB5KRB_ERR_GENERIC;

    for (i = 0; i < context->n_contexts; i++) {
        if (context->contexts[i].pa_system == pa_sys) {
            *modreq_out = &context->contexts[i].modreq;
            return 0;
        }
    }

    return KRB5KRB_ERR_GENERIC;
}

/*
 * Create a list of indices into the preauth_systems array, sorted by order of
 * preference.
 */
static krb5_boolean
pa_list_includes(krb5_pa_data **pa_data, krb5_preauthtype pa_type)
{
    while (*pa_data != NULL) {
        if ((*pa_data)->pa_type == pa_type)
            return TRUE;
        pa_data++;
    }
    return FALSE;
}
static void
sort_pa_order(krb5_context context, krb5_kdc_req *request, int *pa_order)
{
    size_t i, j, k, n_repliers, n_key_replacers;

    /* First, set up the default order. */
    i = 0;
    for (j = 0; j < n_preauth_systems; j++) {
        if (preauth_systems[j].return_padata != NULL)
            pa_order[i++] = j;
    }
    n_repliers = i;
    pa_order[n_repliers] = -1;

    /* Reorder so that PA_REPLACES_KEY modules are listed first. */
    for (i = 0; i < n_repliers; i++) {
        /* If this module replaces the key, then it's okay to leave it where it
         * is in the order. */
        if (preauth_systems[pa_order[i]].flags & PA_REPLACES_KEY)
            continue;
        /* If not, search for a module which does, and swap in the first one we
         * find. */
        for (j = i + 1; j < n_repliers; j++) {
            if (preauth_systems[pa_order[j]].flags & PA_REPLACES_KEY) {
                k = pa_order[j];
                pa_order[j] = pa_order[i];
                pa_order[i] = k;
                break;
            }
        }
    }

    if (request->padata != NULL) {
        /* Now reorder the subset of modules which replace the key,
         * bubbling those which handle pa_data types provided by the
         * client ahead of the others.
         */
        for (i = 0; preauth_systems[pa_order[i]].flags & PA_REPLACES_KEY; i++) {
            continue;
        }
        n_key_replacers = i;
        for (i = 0; i < n_key_replacers; i++) {
            if (pa_list_includes(request->padata,
                                 preauth_systems[pa_order[i]].type))
                continue;
            for (j = i + 1; j < n_key_replacers; j++) {
                if (pa_list_includes(request->padata,
                                     preauth_systems[pa_order[j]].type)) {
                    k = pa_order[j];
                    pa_order[j] = pa_order[i];
                    pa_order[i] = k;
                    break;
                }
            }
        }
    }
#ifdef DEBUG
    krb5_klog_syslog(LOG_DEBUG, "original preauth mechanism list:");
    for (i = 0; i < n_preauth_systems; i++) {
        if (preauth_systems[i].return_padata != NULL)
            krb5_klog_syslog(LOG_DEBUG, "... %s(%d)", preauth_systems[i].name,
                             preauth_systems[i].type);
    }
    krb5_klog_syslog(LOG_DEBUG, "sorted preauth mechanism list:");
    for (i = 0; pa_order[i] != -1; i++) {
        krb5_klog_syslog(LOG_DEBUG, "... %s(%d)",
                         preauth_systems[pa_order[i]].name,
                         preauth_systems[pa_order[i]].type);
    }
#endif
}

const char *missing_required_preauth(krb5_db_entry *client,
                                     krb5_db_entry *server,
                                     krb5_enc_tkt_part *enc_tkt_reply)
{
#if 0
    /*
     * If this is the pwchange service, and the pre-auth bit is set,
     * allow it even if the HW preauth would normally be required.
     *
     * Sandia national labs wanted this for some strange reason... we
     * leave it disabled normally.
     */
    if (isflagset(server->attributes, KRB5_KDB_PWCHANGE_SERVICE) &&
        isflagset(enc_tkt_reply->flags, TKT_FLG_PRE_AUTH))
        return 0;
#endif

#ifdef DEBUG
    krb5_klog_syslog (
        LOG_DEBUG,
        "client needs %spreauth, %shw preauth; request has %spreauth, %shw preauth",
        isflagset (client->attributes, KRB5_KDB_REQUIRES_PRE_AUTH) ? "" : "no ",
        isflagset (client->attributes, KRB5_KDB_REQUIRES_HW_AUTH) ? "" : "no ",
        isflagset (enc_tkt_reply->flags, TKT_FLG_PRE_AUTH) ? "" : "no ",
        isflagset (enc_tkt_reply->flags, TKT_FLG_HW_AUTH) ? "" : "no ");
#endif

    if (isflagset(client->attributes, KRB5_KDB_REQUIRES_PRE_AUTH) &&
        !isflagset(enc_tkt_reply->flags, TKT_FLG_PRE_AUTH))
        return "NEEDED_PREAUTH";

    if (isflagset(client->attributes, KRB5_KDB_REQUIRES_HW_AUTH) &&
        !isflagset(enc_tkt_reply->flags, TKT_FLG_HW_AUTH))
        return "NEEDED_HW_PREAUTH";

    return 0;
}

struct hint_state {
    kdc_hint_respond_fn respond;
    void *arg;
    kdc_realm_t *realm;

    krb5_kdcpreauth_rock rock;
    krb5_kdc_req *request;
    krb5_pa_data ***e_data_out;

    int hw_only;
    preauth_system *ap;
    krb5_pa_data **pa_data, **pa_cur;
    krb5_preauthtype pa_type;
};

static void
hint_list_finish(struct hint_state *state, krb5_error_code code)
{
    kdc_hint_respond_fn oldrespond = state->respond;
    void *oldarg = state->arg;

    if (!code) {
        if (state->pa_data[0] == 0) {
            krb5_klog_syslog(LOG_INFO,
                             _("%spreauth required but hint list is empty"),
                             state->hw_only ? "hw" : "");
        }
        /* If we fail to get the cookie it is probably still reasonable to
         * continue with the response. */
        kdc_preauth_get_cookie(state->rock->rstate, state->pa_cur);

        *state->e_data_out = state->pa_data;
        state->pa_data = NULL;
    }

    krb5_free_pa_data(kdc_context, state->pa_data);
    free(state);
    (*oldrespond)(oldarg);
}

static void
hint_list_next(struct hint_state *arg);

static void
finish_get_edata(void *arg, krb5_error_code code, krb5_pa_data *pa)
{
    struct hint_state *state = arg;

    kdc_active_realm = state->realm;
    if (code == 0) {
        if (pa == NULL) {
            /* Include an empty value of the current type. */
            pa = calloc(1, sizeof(*pa));
            pa->magic = KV5M_PA_DATA;
            pa->pa_type = state->pa_type;
        }
        *state->pa_cur++ = pa;
    }

    state->ap++;
    hint_list_next(state);
}

static void
hint_list_next(struct hint_state *state)
{
    preauth_system *ap = state->ap;

    if (ap->type == -1) {
        hint_list_finish(state, 0);
        return;
    }

    if (state->hw_only && !(ap->flags & PA_HARDWARE))
        goto next;
    if (ap->flags & PA_PSEUDO)
        goto next;

    state->pa_type = ap->type;
    if (ap->get_edata) {
        ap->get_edata(kdc_context, state->request, &callbacks, state->rock,
                      ap->moddata, ap->type, finish_get_edata, state);
    } else
        finish_get_edata(state, 0, NULL);
    return;

next:
    state->ap++;
    hint_list_next(state);
}

void
get_preauth_hint_list(krb5_kdc_req *request, krb5_kdcpreauth_rock rock,
                      krb5_pa_data ***e_data_out, kdc_hint_respond_fn respond,
                      void *arg)
{
    struct hint_state *state;

    *e_data_out = NULL;

    /* Allocate our state. */
    state = malloc(sizeof(*state));
    if (!state) {
        (*respond)(arg);
        return;
    }
    state->hw_only = isflagset(rock->client->attributes,
                               KRB5_KDB_REQUIRES_HW_AUTH);
    state->respond = respond;
    state->arg = arg;
    state->request = request;
    state->rock = rock;
    state->realm = kdc_active_realm;
    state->e_data_out = e_data_out;

    /* Allocate two extra entries for the cookie and the terminator. */
    state->pa_data = calloc(n_preauth_systems + 2, sizeof(krb5_pa_data *));
    if (!state->pa_data) {
        free(state);
        (*respond)(arg);
        return;
    }

    state->pa_cur = state->pa_data;
    state->ap = preauth_systems;
    hint_list_next(state);
}

/*
 * Add authorization data returned from preauth modules to the ticket
 * It is assumed that ad is a "null-terminated" array of krb5_authdata ptrs
 */
static krb5_error_code
add_authorization_data(krb5_enc_tkt_part *enc_tkt_part, krb5_authdata **ad)
{
    krb5_authdata **newad;
    int oldones, newones;
    int i;

    if (enc_tkt_part == NULL || ad == NULL)
        return EINVAL;

    for (newones = 0; ad[newones] != NULL; newones++);
    if (newones == 0)
        return 0;   /* nothing to add */

    if (enc_tkt_part->authorization_data == NULL)
        oldones = 0;
    else
        for (oldones = 0;
             enc_tkt_part->authorization_data[oldones] != NULL; oldones++);

    newad = malloc((oldones + newones + 1) * sizeof(krb5_authdata *));
    if (newad == NULL)
        return ENOMEM;

    /* Copy any existing pointers */
    for (i = 0; i < oldones; i++)
        newad[i] = enc_tkt_part->authorization_data[i];

    /* Add the new ones */
    for (i = 0; i < newones; i++)
        newad[oldones+i] = ad[i];

    /* Terminate the new list */
    newad[oldones+i] = NULL;

    /* Free any existing list */
    if (enc_tkt_part->authorization_data != NULL)
        free(enc_tkt_part->authorization_data);

    /* Install our new list */
    enc_tkt_part->authorization_data = newad;

    return 0;
}

struct padata_state {
    kdc_preauth_respond_fn respond;
    void *arg;
    kdc_realm_t *realm;

    krb5_kdcpreauth_modreq *modreq_ptr;
    krb5_pa_data **padata;
    int pa_found;
    krb5_context context;
    krb5_kdcpreauth_rock rock;
    krb5_data *req_pkt;
    krb5_kdc_req *request;
    krb5_enc_tkt_part *enc_tkt_reply;
    void **padata_context;

    preauth_system *pa_sys;
    krb5_pa_data **pa_e_data;
    krb5_boolean typed_e_data_flag;
    int pa_ok;
    krb5_error_code saved_code;

    krb5_pa_data ***e_data_out;
    krb5_boolean *typed_e_data_out;
};

static void
finish_check_padata(struct padata_state *state, krb5_error_code code)
{
    kdc_preauth_respond_fn oldrespond;
    void *oldarg;

    assert(state);
    oldrespond = state->respond;
    oldarg = state->arg;

    if (!state->pa_ok) {
        /* Return any saved preauth e-data. */
        *state->e_data_out = state->pa_e_data;
        *state->typed_e_data_out = state->typed_e_data_flag;
    } else
        krb5_free_pa_data(state->context, state->pa_e_data);

    if (state->pa_ok) {
        free(state);
        (*oldrespond)(oldarg, 0);
        return;
    }

    /* pa system was not found; we may return PREAUTH_REQUIRED later,
       but we did not actually fail to verify the pre-auth. */
    if (!state->pa_found) {
        free(state);
        (*oldrespond)(oldarg, 0);
        return;
    }
    free(state);

    /* The following switch statement allows us
     * to return some preauth system errors back to the client.
     */
    switch(code) {
    case 0: /* in case of PA-PAC-REQUEST with no PA-ENC-TIMESTAMP */
    case KRB5KRB_AP_ERR_BAD_INTEGRITY:
    case KRB5KRB_AP_ERR_SKEW:
    case KRB5KDC_ERR_PREAUTH_REQUIRED:
    case KRB5KDC_ERR_ETYPE_NOSUPP:
        /* rfc 4556 */
    case KRB5KDC_ERR_CLIENT_NOT_TRUSTED:
    case KRB5KDC_ERR_INVALID_SIG:
    case KRB5KDC_ERR_DH_KEY_PARAMETERS_NOT_ACCEPTED:
    case KRB5KDC_ERR_CANT_VERIFY_CERTIFICATE:
    case KRB5KDC_ERR_INVALID_CERTIFICATE:
    case KRB5KDC_ERR_REVOKED_CERTIFICATE:
    case KRB5KDC_ERR_REVOCATION_STATUS_UNKNOWN:
    case KRB5KDC_ERR_CLIENT_NAME_MISMATCH:
    case KRB5KDC_ERR_INCONSISTENT_KEY_PURPOSE:
    case KRB5KDC_ERR_DIGEST_IN_CERT_NOT_ACCEPTED:
    case KRB5KDC_ERR_PA_CHECKSUM_MUST_BE_INCLUDED:
    case KRB5KDC_ERR_DIGEST_IN_SIGNED_DATA_NOT_ACCEPTED:
    case KRB5KDC_ERR_PUBLIC_KEY_ENCRYPTION_NOT_SUPPORTED:
        /* earlier drafts of what became rfc 4556 */
    case KRB5KDC_ERR_CERTIFICATE_MISMATCH:
    case KRB5KDC_ERR_KDC_NOT_TRUSTED:
    case KRB5KDC_ERR_REVOCATION_STATUS_UNAVAILABLE:
        /* This value is shared with
         *     KRB5KDC_ERR_DH_KEY_PARAMETERS_NOT_ACCEPTED. */
        /* case KRB5KDC_ERR_KEY_TOO_WEAK: */
    case KRB5KDC_ERR_DISCARD:
        /* pkinit alg-agility */
    case KRB5KDC_ERR_NO_ACCEPTABLE_KDF:
        (*oldrespond)(oldarg, code);
        return;
    default:
        (*oldrespond)(oldarg, KRB5KDC_ERR_PREAUTH_FAILED);
        return;
    }
}

static void
next_padata(struct padata_state *state);

static void
finish_verify_padata(void *arg, krb5_error_code code,
                     krb5_kdcpreauth_modreq modreq, krb5_pa_data **e_data,
                     krb5_authdata **authz_data)
{
    struct padata_state *state = arg;
    const char *emsg;
    krb5_boolean typed_e_data_flag;

    assert(state);
    kdc_active_realm = state->realm; /* Restore the realm. */
    *state->modreq_ptr = modreq;

    if (code) {
        emsg = krb5_get_error_message(state->context, code);
        krb5_klog_syslog(LOG_INFO, "preauth (%s) verify failure: %s",
                         state->pa_sys->name, emsg);
        krb5_free_error_message(state->context, emsg);

        /* Ignore authorization data returned from modules that fail */
        if (authz_data != NULL) {
            krb5_free_authdata(state->context, authz_data);
            authz_data = NULL;
        }

        typed_e_data_flag = ((state->pa_sys->flags & PA_TYPED_E_DATA) != 0);

        /*
         * We'll return edata from either the first PA_REQUIRED module
         * that fails, or the first non-PA_REQUIRED module that fails.
         * Hang on to edata from the first non-PA_REQUIRED module.
         * If we've already got one saved, simply discard this one.
         */
        if (state->pa_sys->flags & PA_REQUIRED) {
            /* free up any previous edata we might have been saving */
            if (state->pa_e_data != NULL)
                krb5_free_pa_data(state->context, state->pa_e_data);
            state->pa_e_data = e_data;
            state->typed_e_data_flag = typed_e_data_flag;

            /* Make sure we use the current retval */
            state->pa_ok = 0;
            finish_check_padata(state, code);
            return;
        } else if (state->pa_e_data == NULL) {
            /* save the first error code and e-data */
            state->pa_e_data = e_data;
            state->typed_e_data_flag = typed_e_data_flag;
            state->saved_code = code;
        } else if (e_data != NULL) {
            /* discard this extra e-data from non-PA_REQUIRED module */
            krb5_free_pa_data(state->context, e_data);
        }
    } else {
#ifdef DEBUG
        krb5_klog_syslog (LOG_DEBUG, ".. .. ok");
#endif

        /* Ignore any edata returned on success */
        if (e_data != NULL)
            krb5_free_pa_data(state->context, e_data);

        /* Add any authorization data to the ticket */
        if (authz_data != NULL) {
            add_authorization_data(state->enc_tkt_reply, authz_data);
            free(authz_data);
        }

        state->pa_ok = 1;
        if (state->pa_sys->flags & PA_SUFFICIENT) {
            finish_check_padata(state, state->saved_code);
            return;
        }
    }

    next_padata(state);
}

static void
next_padata(struct padata_state *state)
{
    assert(state);
    if (!state->padata)
        state->padata = state->request->padata;
    else
        state->padata++;

    if (!*state->padata) {
        finish_check_padata(state, state->saved_code);
        return;
    }

#ifdef DEBUG
    krb5_klog_syslog (LOG_DEBUG, ".. pa_type 0x%x", (*state->padata)->pa_type);
#endif
    if (find_pa_system((*state->padata)->pa_type, &state->pa_sys))
        goto next;
    if (find_modreq(state->pa_sys, *state->padata_context, &state->modreq_ptr))
        goto next;
#ifdef DEBUG
    krb5_klog_syslog (LOG_DEBUG, ".. pa_type %s", state->pa_sys->name);
#endif
    if (state->pa_sys->verify_padata == 0)
        goto next;

    state->pa_found++;
    state->pa_sys->verify_padata(state->context, state->req_pkt,
                                 state->request, state->enc_tkt_reply,
                                 *state->padata, &callbacks, state->rock,
                                 state->pa_sys->moddata, finish_verify_padata,
                                 state);
    return;

next:
    next_padata(state);
}

/*
 * This routine is called to verify the preauthentication information
 * for a V5 request.
 *
 * Returns 0 if the pre-authentication is valid, non-zero to indicate
 * an error code of some sort.
 */

void
check_padata(krb5_context context, krb5_kdcpreauth_rock rock,
             krb5_data *req_pkt, krb5_kdc_req *request,
             krb5_enc_tkt_part *enc_tkt_reply, void **padata_context,
             krb5_pa_data ***e_data, krb5_boolean *typed_e_data,
             kdc_preauth_respond_fn respond, void *arg)
{
    struct padata_state *state;

    if (request->padata == 0) {
        (*respond)(arg, 0);
        return;
    }

    if (make_padata_context(context, padata_context) != 0) {
        (*respond)(arg, KRB5KRB_ERR_GENERIC);
        return;
    }

    state = malloc(sizeof(*state));
    if (!state) {
        (*respond)(arg, ENOMEM);
        return;
    }
    memset(state, 0, sizeof(*state));
    state->respond = respond;
    state->arg = arg;
    state->context = context;
    state->rock = rock;
    state->req_pkt = req_pkt;
    state->request = request;
    state->enc_tkt_reply = enc_tkt_reply;
    state->padata_context = padata_context;
    state->e_data_out = e_data;
    state->typed_e_data_out = typed_e_data;
    state->realm = kdc_active_realm;

#ifdef DEBUG
    krb5_klog_syslog (LOG_DEBUG, "checking padata");
#endif

    next_padata(state);
}

/*
 * return_padata creates any necessary preauthentication
 * structures which should be returned by the KDC to the client
 */
krb5_error_code
return_padata(krb5_context context, krb5_kdcpreauth_rock rock,
              krb5_data *req_pkt, krb5_kdc_req *request, krb5_kdc_rep *reply,
              krb5_keyblock *encrypting_key, void **padata_context)
{
    krb5_error_code             retval;
    krb5_pa_data **             padata;
    krb5_pa_data **             send_pa_list;
    krb5_pa_data **             send_pa;
    krb5_pa_data *              pa = 0;
    krb5_pa_data null_item;
    preauth_system *            ap;
    int *                       pa_order;
    int *                       pa_type;
    int                         size = 0;
    krb5_kdcpreauth_modreq      *modreq_ptr;
    krb5_boolean                key_modified;
    krb5_keyblock               original_key;
    if ((!*padata_context) &&
        (make_padata_context(context, padata_context) != 0)) {
        return KRB5KRB_ERR_GENERIC;
    }

    for (ap = preauth_systems; ap->type != -1; ap++) {
        if (ap->return_padata)
            size++;
    }

    if ((send_pa_list = malloc((size+1) * sizeof(krb5_pa_data *))) == NULL)
        return ENOMEM;
    if ((pa_order = malloc((size+1) * sizeof(int))) == NULL) {
        free(send_pa_list);
        return ENOMEM;
    }
    sort_pa_order(context, request, pa_order);

    retval = krb5_copy_keyblock_contents(context, encrypting_key,
                                         &original_key);
    if (retval) {
        free(send_pa_list);
        free(pa_order);
        return retval;
    }
    key_modified = FALSE;
    null_item.contents = NULL;
    null_item.length = 0;
    send_pa = send_pa_list;
    *send_pa = 0;

    for (pa_type = pa_order; *pa_type != -1; pa_type++) {
        ap = &preauth_systems[*pa_type];
        if (!key_modified)
            if (original_key.enctype != encrypting_key->enctype)
                key_modified = TRUE;
        if (!key_modified)
            if (original_key.length != encrypting_key->length)
                key_modified = TRUE;
        if (!key_modified)
            if (memcmp(original_key.contents, encrypting_key->contents,
                       original_key.length) != 0)
                key_modified = TRUE;
        if (key_modified && (ap->flags & PA_REPLACES_KEY))
            continue;
        if (ap->return_padata == 0)
            continue;
        if (find_modreq(ap, *padata_context, &modreq_ptr))
            continue;
        pa = &null_item;
        null_item.pa_type = ap->type;
        if (request->padata) {
            for (padata = request->padata; *padata; padata++) {
                if ((*padata)->pa_type == ap->type) {
                    pa = *padata;
                    break;
                }
            }
        }
        retval = ap->return_padata(context, pa, req_pkt, request, reply,
                                   encrypting_key, send_pa, &callbacks, rock,
                                   ap->moddata, *modreq_ptr);
        if (retval)
            goto cleanup;

        if (*send_pa)
            send_pa++;
        *send_pa = 0;
    }

    retval = 0;

    if (send_pa_list[0]) {
        reply->padata = send_pa_list;
        send_pa_list = 0;
    }

cleanup:
    krb5_free_keyblock_contents(context, &original_key);
    free(pa_order);
    if (send_pa_list)
        krb5_free_pa_data(context, send_pa_list);

    return (retval);
}

static krb5_boolean
request_contains_enctype(krb5_context context,  const krb5_kdc_req *request,
                         krb5_enctype enctype)
{
    int i;
    for (i =0; i < request->nktypes; i++)
        if (request->ktype[i] == enctype)
            return 1;
    return 0;
}

static krb5_error_code
_make_etype_info_entry(krb5_context context,
                       krb5_principal client_princ, krb5_key_data *client_key,
                       krb5_enctype etype, krb5_etype_info_entry **entry,
                       int etype_info2)
{
    krb5_data                   salt;
    krb5_etype_info_entry *     tmp_entry;
    krb5_error_code             retval;

    if ((tmp_entry = malloc(sizeof(krb5_etype_info_entry))) == NULL)
        return ENOMEM;

    salt.data = 0;

    tmp_entry->magic = KV5M_ETYPE_INFO_ENTRY;
    tmp_entry->etype = etype;
    tmp_entry->length = KRB5_ETYPE_NO_SALT;
    tmp_entry->salt = 0;
    tmp_entry->s2kparams.data = NULL;
    tmp_entry->s2kparams.length = 0;
    retval = get_salt_from_key(context, client_princ, client_key, &salt);
    if (retval)
        goto fail;
    if (etype_info2 && client_key->key_data_ver > 1 &&
        client_key->key_data_type[1] == KRB5_KDB_SALTTYPE_AFS3) {
        switch (etype) {
        case ENCTYPE_DES_CBC_CRC:
        case ENCTYPE_DES_CBC_MD4:
        case ENCTYPE_DES_CBC_MD5:
            tmp_entry->s2kparams.data = malloc(1);
            if (tmp_entry->s2kparams.data == NULL) {
                retval = ENOMEM;
                goto fail;
            }
            tmp_entry->s2kparams.length = 1;
            tmp_entry->s2kparams.data[0] = 1;
            break;
        default:
            break;
        }
    }

    if (salt.length >= 0) {
        tmp_entry->length = salt.length;
        tmp_entry->salt = (unsigned char *) salt.data;
        salt.data = 0;
    }
    *entry = tmp_entry;
    return 0;

fail:
    if (tmp_entry) {
        if (tmp_entry->s2kparams.data)
            free(tmp_entry->s2kparams.data);
        free(tmp_entry);
    }
    if (salt.data)
        free(salt.data);
    return retval;
}

/* Create etype information for a client for the preauth-required hint list,
 * for either etype-info or etype-info2. */
static void
etype_info_helper(krb5_context context, krb5_kdc_req *request,
                  krb5_db_entry *client, krb5_preauthtype pa_type,
                  krb5_kdcpreauth_edata_respond_fn respond, void *arg)
{
    krb5_error_code retval;
    krb5_pa_data *pa = NULL;
    krb5_etype_info_entry **entry = NULL;
    krb5_data *scratch = NULL;
    krb5_key_data *client_key;
    krb5_enctype db_etype;
    int i = 0, start = 0, seen_des = 0;
    int etype_info2 = (pa_type == KRB5_PADATA_ETYPE_INFO2);

    entry = k5alloc((client->n_key_data * 2 + 1) * sizeof(*entry), &retval);
    if (entry == NULL)
        goto cleanup;
    entry[0] = NULL;

    while (1) {
        retval = krb5_dbe_search_enctype(context, client, &start, -1,
                                         -1, 0, &client_key);
        if (retval == KRB5_KDB_NO_MATCHING_KEY)
            break;
        if (retval)
            goto cleanup;
        db_etype = client_key->key_data_type[0];
        if (db_etype == ENCTYPE_DES_CBC_MD4)
            db_etype = ENCTYPE_DES_CBC_MD5;

        if (request_contains_enctype(context, request, db_etype)) {
            assert(etype_info2 ||
                   !enctype_requires_etype_info_2(db_etype));
            retval = _make_etype_info_entry(context, client->princ, client_key,
                                            db_etype, &entry[i], etype_info2);
            if (retval != 0)
                goto cleanup;
            i++;
        }

        /*
         * If there is a des key in the kdb, try the "similar" enctypes,
         * avoid duplicate entries.
         */
        if (!seen_des) {
            switch (db_etype) {
            case ENCTYPE_DES_CBC_MD5:
                db_etype = ENCTYPE_DES_CBC_CRC;
                break;
            case ENCTYPE_DES_CBC_CRC:
                db_etype = ENCTYPE_DES_CBC_MD5;
                break;
            default:
                continue;

            }
            if (krb5_is_permitted_enctype(context, db_etype) &&
                request_contains_enctype(context, request, db_etype)) {
                retval = _make_etype_info_entry(context, client->princ,
                                                client_key, db_etype,
                                                &entry[i], etype_info2);
                if (retval != 0)
                    goto cleanup;
                entry[i+1] = 0;
                i++;
            }
            seen_des++;
        }
    }
    if (etype_info2)
        retval = encode_krb5_etype_info2(entry, &scratch);
    else
        retval = encode_krb5_etype_info(entry, &scratch);
    if (retval)
        goto cleanup;
    pa = k5alloc(sizeof(*pa), &retval);
    if (pa == NULL)
        goto cleanup;
    pa->magic = KV5M_PA_DATA;
    pa->pa_type = pa_type;
    pa->contents = (unsigned char *)scratch->data;
    pa->length = scratch->length;
    scratch->data = NULL;

cleanup:
    krb5_free_etype_info(context, entry);
    krb5_free_data(context, scratch);
    (*respond)(arg, retval, pa);
}

static void
get_etype_info(krb5_context context, krb5_kdc_req *request,
               krb5_kdcpreauth_callbacks cb, krb5_kdcpreauth_rock rock,
               krb5_kdcpreauth_moddata moddata, krb5_preauthtype pa_type,
               krb5_kdcpreauth_edata_respond_fn respond, void *arg)
{
    int i;

    for (i=0;  i < request->nktypes; i++) {
        if (enctype_requires_etype_info_2(request->ktype[i])) {
            /* Requestor understands etype-info2, so don't send etype-info. */
            (*respond)(arg, KRB5KDC_ERR_PADATA_TYPE_NOSUPP, NULL);
            return;
        }
    }

    etype_info_helper(context, request, rock->client, pa_type, respond, arg);
}

static void
get_etype_info2(krb5_context context, krb5_kdc_req *request,
                krb5_kdcpreauth_callbacks cb, krb5_kdcpreauth_rock rock,
                krb5_kdcpreauth_moddata moddata, krb5_preauthtype pa_type,
                krb5_kdcpreauth_edata_respond_fn respond, void *arg)
{
    etype_info_helper(context, request, rock->client, pa_type, respond, arg);
}

static krb5_error_code
etype_info_as_rep_helper(krb5_context context, krb5_pa_data * padata,
                         krb5_db_entry *client,
                         krb5_kdc_req *request, krb5_kdc_rep *reply,
                         krb5_key_data *client_key,
                         krb5_keyblock *encrypting_key,
                         krb5_pa_data **send_pa,
                         int etype_info2)
{
    int i;
    krb5_error_code retval;
    krb5_pa_data *tmp_padata;
    krb5_etype_info_entry **entry = NULL;
    krb5_data *scratch = NULL;

    /*
     * Skip PA-ETYPE-INFO completely if AS-REQ lists any "newer"
     * enctypes.
     */
    if (!etype_info2) {
        for (i = 0; i < request->nktypes; i++) {
            if (enctype_requires_etype_info_2(request->ktype[i])) {
                *send_pa = NULL;
                return 0;
            }
        }
    }

    tmp_padata = malloc( sizeof(krb5_pa_data));
    if (tmp_padata == NULL)
        return ENOMEM;
    if (etype_info2)
        tmp_padata->pa_type = KRB5_PADATA_ETYPE_INFO2;
    else
        tmp_padata->pa_type = KRB5_PADATA_ETYPE_INFO;

    entry = malloc(2 * sizeof(krb5_etype_info_entry *));
    if (entry == NULL) {
        retval = ENOMEM;
        goto cleanup;
    }
    entry[0] = NULL;
    entry[1] = NULL;
    retval = _make_etype_info_entry(context, client->princ, client_key,
                                    encrypting_key->enctype, entry,
                                    etype_info2);
    if (retval)
        goto cleanup;

    if (etype_info2)
        retval = encode_krb5_etype_info2(entry, &scratch);
    else
        retval = encode_krb5_etype_info(entry, &scratch);

    if (retval)
        goto cleanup;
    tmp_padata->contents = (krb5_octet *)scratch->data;
    tmp_padata->length = scratch->length;
    *send_pa = tmp_padata;

    /* For cleanup - we no longer own the contents of the krb5_data
     * only to pointer to the krb5_data
     */
    scratch->data = 0;

cleanup:
    if (entry)
        krb5_free_etype_info(context, entry);
    if (retval) {
        if (tmp_padata)
            free(tmp_padata);
    }
    if (scratch)
        krb5_free_data(context, scratch);
    return retval;
}

static krb5_error_code
return_etype_info2(krb5_context context, krb5_pa_data * padata,
                   krb5_data *req_pkt, krb5_kdc_req *request,
                   krb5_kdc_rep *reply, krb5_keyblock *encrypting_key,
                   krb5_pa_data **send_pa, krb5_kdcpreauth_callbacks cb,
                   krb5_kdcpreauth_rock rock, krb5_kdcpreauth_moddata moddata,
                   krb5_kdcpreauth_modreq modreq)
{
    return etype_info_as_rep_helper(context, padata, rock->client, request,
                                    reply, rock->client_key, encrypting_key,
                                    send_pa, 1);
}


static krb5_error_code
return_etype_info(krb5_context context, krb5_pa_data *padata,
                  krb5_data *req_pkt, krb5_kdc_req *request,
                  krb5_kdc_rep *reply, krb5_keyblock *encrypting_key,
                  krb5_pa_data **send_pa, krb5_kdcpreauth_callbacks cb,
                  krb5_kdcpreauth_rock rock, krb5_kdcpreauth_moddata moddata,
                  krb5_kdcpreauth_modreq modreq)
{
    return etype_info_as_rep_helper(context, padata, rock->client, request,
                                    reply, rock->client_key, encrypting_key,
                                    send_pa, 0);
}

static krb5_error_code
return_pw_salt(krb5_context context, krb5_pa_data *in_padata,
               krb5_data *req_pkt, krb5_kdc_req *request, krb5_kdc_rep *reply,
               krb5_keyblock *encrypting_key, krb5_pa_data **send_pa,
               krb5_kdcpreauth_callbacks cb, krb5_kdcpreauth_rock rock,
               krb5_kdcpreauth_moddata moddata, krb5_kdcpreauth_modreq modreq)
{
    krb5_error_code     retval;
    krb5_pa_data *      padata;
    krb5_data *         scratch;
    krb5_data           salt_data;
    krb5_key_data *     client_key = rock->client_key;
    int i;

    for (i = 0; i < request->nktypes; i++) {
        if (enctype_requires_etype_info_2(request->ktype[i]))
            return 0;
    }
    if (client_key->key_data_ver == 1 ||
        client_key->key_data_type[1] == KRB5_KDB_SALTTYPE_NORMAL)
        return 0;

    if ((padata = malloc(sizeof(krb5_pa_data))) == NULL)
        return ENOMEM;
    padata->magic = KV5M_PA_DATA;
    padata->pa_type = KRB5_PADATA_PW_SALT;

    switch (client_key->key_data_type[1]) {
    case KRB5_KDB_SALTTYPE_V4:
        /* send an empty (V4) salt */
        padata->contents = 0;
        padata->length = 0;
        break;
    case KRB5_KDB_SALTTYPE_NOREALM:
        if ((retval = krb5_principal2salt_norealm(kdc_context,
                                                  request->client,
                                                  &salt_data)))
            goto cleanup;
        padata->contents = (krb5_octet *)salt_data.data;
        padata->length = salt_data.length;
        break;
    case KRB5_KDB_SALTTYPE_AFS3:
        /* send an AFS style realm-based salt */
        /* for now, just pass the realm back and let the client
           do the work. In the future, add a kdc configuration
           variable that specifies the old cell name. */
        padata->pa_type = KRB5_PADATA_AFS3_SALT;
        /* it would be just like ONLYREALM, but we need to pass the 0 */
        scratch = krb5_princ_realm(kdc_context, request->client);
        if ((padata->contents = malloc(scratch->length+1)) == NULL) {
            retval = ENOMEM;
            goto cleanup;
        }
        memcpy(padata->contents, scratch->data, scratch->length);
        padata->length = scratch->length+1;
        padata->contents[scratch->length] = 0;
        break;
    case KRB5_KDB_SALTTYPE_ONLYREALM:
        scratch = krb5_princ_realm(kdc_context, request->client);
        if ((padata->contents = malloc(scratch->length)) == NULL) {
            retval = ENOMEM;
            goto cleanup;
        }
        memcpy(padata->contents, scratch->data, scratch->length);
        padata->length = scratch->length;
        break;
    case KRB5_KDB_SALTTYPE_SPECIAL:
        if ((padata->contents = malloc(client_key->key_data_length[1]))
            == NULL) {
            retval = ENOMEM;
            goto cleanup;
        }
        memcpy(padata->contents, client_key->key_data_contents[1],
               client_key->key_data_length[1]);
        padata->length = client_key->key_data_length[1];
        break;
    default:
        free(padata);
        return 0;
    }

    *send_pa = padata;
    return 0;

cleanup:
    free(padata);
    return retval;
}



#if APPLE_PKINIT
/* PKINIT preauth support */
#define  PKINIT_DEBUG    0
#if     PKINIT_DEBUG
#define kdcPkinitDebug(args...)       printf(args)
#else
#define kdcPkinitDebug(args...)
#endif

/*
 * get_edata() - our only job is to determine whether this KDC is capable of
 * performing PKINIT. We infer that from the presence or absence of any
 * KDC signing cert.
 */
static krb5_error_code get_pkinit_edata(
    krb5_context context,
    krb5_kdc_req *request,
    krb5_db_entry *client,
    krb5_db_entry *server,
    preauth_get_entry_data_proc pkinit_get_entry_data,
    void *pa_module_context,
    krb5_pa_data *pa_data)
{
    krb5_pkinit_signing_cert_t cert = NULL;
    krb5_error_code err = krb5_pkinit_get_kdc_cert(0, NULL, NULL, &cert);

    kdcPkinitDebug("get_pkinit_edata: kdc cert %s\n", err ? "NOT FOUND" : "FOUND");
    if(cert) {
        krb5_pkinit_release_cert(cert);
    }
    return err;
}

/*
 * This is 0 only for testing until the KDC DB contains
 * the hash of the client cert
 */
#define REQUIRE_CLIENT_CERT_MATCH   1

static krb5_error_code verify_pkinit_request(
    krb5_context context,
    krb5_db_entry *client,
    krb5_data *req_pkt,
    krb5_kdc_req *request,
    krb5_enc_tkt_part *enc_tkt_reply,
    krb5_pa_data *data,
    preauth_get_entry_data_proc pkinit_get_entry_data,
    void *pa_module_context,
    void **pa_request_context,
    krb5_data **e_data,
    krb5_authdata ***authz_data)
{
    krb5_error_code         krtn;
    krb5_data               pa_data;
    krb5_data               *der_req = NULL;
    krb5_boolean            valid_cksum;
    char                    *cert_hash = NULL;
    unsigned                cert_hash_len;
    unsigned                key_dex;
    unsigned                cert_match = 0;
    krb5_keyblock           decrypted_key, *mkey_ptr;

    /* the data we get from the AS-REQ */
    krb5_timestamp          client_ctime = 0;
    krb5_ui_4               client_cusec = 0;
    krb5_timestamp          kdc_ctime = 0;
    krb5_int32              kdc_cusec = 0;
    krb5_ui_4               nonce = 0;
    krb5_checksum           pa_cksum;
    krb5int_cert_sig_status cert_sig_status;
    krb5_data               client_cert = {0, 0, NULL};

    krb5_kdc_req *tmp_as_req = NULL;

    kdcPkinitDebug("verify_pkinit_request\n");

    decrypted_key.contents = NULL;
    pa_data.data = (char *)data->contents;
    pa_data.length = data->length;
    krtn = krb5int_pkinit_as_req_parse(context, &pa_data,
                                       &client_ctime, &client_cusec,
                                       &nonce, &pa_cksum,
                                       &cert_sig_status,
                                       NULL, NULL,/* num_cms_types, cms_types */
                                       &client_cert,       /* signer_cert */
                                       /* remaining fields unused (for now) */
                                       NULL, NULL,/* num_all_certs, all_certs */
                                       NULL, NULL,/* num_trusted_CAs, trusted_CAs */
                                       NULL);     /* kdc_cert */
    if(krtn) {
        kdcPkinitDebug("pa_pk_as_req_parse returned %d; PKINIT aborting.\n",
                       (int)krtn);
        return krtn;
    }
#if     PKINIT_DEBUG
    if(cert_sig_status != pki_cs_good) {
        kdcPkinitDebug("verify_pkinit_request: cert_sig_status %d\n",
                       (int)cert_sig_status);
    }
#endif  /* PKINIT_DEBUG */

    /*
     * Verify signature and cert.
     * FIXME: The spec calls for an e-data with error-specific type to be
     * returned on error here. TD_TRUSTED_CERTIFIERS
     * to be returned to the client here. There is no way for a preauth
     * module to pass back e-data to process_as_req at this time. We
     * might want to add such capability via an out param to check_padata
     * and to its callees.
     */
    switch(cert_sig_status) {
    case pki_cs_good:
        break;
    case pki_cs_sig_verify_fail:
        /* no e-data */
        krtn = KDC_ERR_INVALID_SIG;
        goto cleanup;
    case pki_cs_no_root:
    case pki_cs_unknown_root:
    case pki_cs_untrusted:
        /*
         * Can't verify to known root.
         * e-data TD_TRUSTED_CERTIFIERS
         */
        kdcPkinitDebug("verify_pkinit_request: KDC_ERR_CANT_VERIFY_CERTIFICATE\n");
        krtn = KDC_ERR_CANT_VERIFY_CERTIFICATE;
        goto cleanup;
    case pki_cs_bad_leaf:
    case pki_cs_expired:
    case pki_cs_not_valid_yet:
        /*
         * Problems with client cert itself.
         * e-data type TD_INVALID_CERTIFICATES
         */
        krtn = KDC_ERR_INVALID_CERTIFICATE;
        goto cleanup;
    case pki_cs_revoked:
        /* e-data type TD-INVALID-CERTIFICATES */
        krtn = KDC_ERR_REVOKED_CERTIFICATE;
        goto cleanup;
    case pki_bad_key_use:
        krtn = KDC_ERR_INCONSISTENT_KEY_PURPOSE;
        /* no e-data */
        goto cleanup;
    case pki_bad_digest:
        /* undefined (explicitly!) e-data */
        krtn = KDC_ERR_DIGEST_IN_SIGNED_DATA_NOT_ACCEPTED;
        goto cleanup;
    case pki_bad_cms:
    case pki_cs_other_err:
    default:
        krtn = KRB5KDC_ERR_PREAUTH_FAILED;
        goto cleanup;
    }

    krtn = krb5_us_timeofday(context, &kdc_ctime, &kdc_cusec);
    if(krtn) {
        goto cleanup;
    }
    if (labs(kdc_ctime - client_ctime) > context->clockskew) {
        kdcPkinitDebug("verify_pkinit_request: clock skew violation client %d svr %d\n",
                       (int)client_ctime, (int)kdc_ctime);
        krtn = KRB5KRB_AP_ERR_SKEW;
        goto cleanup;
    }

    /*
     * The KDC may have modified the request after decoding it.
     * We need to compute the checksum on the data that
     * came from the client.  Therefore, we use the original
     * packet contents.
     */
    krtn = decode_krb5_as_req(req_pkt, &tmp_as_req);
    if(krtn) {
        kdcPkinitDebug("decode_krb5_as_req returned %d\n", (int)krtn);
        goto cleanup;
    }

    /* calculate and compare checksum */
    krtn = encode_krb5_kdc_req_body(tmp_as_req, &der_req);
    if(krtn) {
        kdcPkinitDebug("encode_krb5_kdc_req_body returned %d\n", (int)krtn);
        goto cleanup;
    }
    krtn = krb5_c_verify_checksum(context, NULL, 0, der_req,
                                  &pa_cksum, &valid_cksum);
    if(krtn) {
        kdcPkinitDebug("krb5_c_verify_checksum returned %d\n", (int)krtn);
        goto cleanup;
    }
    if(!valid_cksum) {
        kdcPkinitDebug("verify_pkinit_request: checksum error\n");
        krtn = KRB5KRB_AP_ERR_BAD_INTEGRITY;
        goto cleanup;
    }

#if REQUIRE_CLIENT_CERT_MATCH
    /* look up in the KDB to ensure correct client/cert binding */
    cert_hash = krb5_pkinit_cert_hash_str(&client_cert);
    if(cert_hash == NULL) {
        krtn = ENOMEM;
        goto cleanup;
    }
    cert_hash_len = strlen(cert_hash);
    for(key_dex=0; key_dex<client->n_key_data; key_dex++) {
        krb5_key_data *key_data = &client->key_data[key_dex];
        kdcPkinitDebug("--- key %u type[0] %u length[0] %u type[1] %u length[1] %u\n",
                       key_dex,
                       key_data->key_data_type[0], key_data->key_data_length[0],
                       key_data->key_data_type[1], key_data->key_data_length[1]);
        if(key_data->key_data_type[1] != KRB5_KDB_SALTTYPE_CERTHASH) {
            continue;
        }

        /*
         * Unfortunately this key is stored encrypted even though it's
         * not sensitive...
         */
        krtn = krb5_dbe_decrypt_key_data(context, NULL, key_data,
                                         &decrypted_key, NULL);
        if(krtn) {
            kdcPkinitDebug("verify_pkinit_request: error decrypting cert hash block\n");
            break;
        }
        if((decrypted_key.contents != NULL) &&
           (cert_hash_len == decrypted_key.length) &&
           !memcmp(decrypted_key.contents, cert_hash, cert_hash_len)) {
            cert_match = 1;
            break;
        }
    }
    if(decrypted_key.contents) {
        krb5_free_keyblock_contents(context, &decrypted_key);
    }
    if(!cert_match) {
        kdcPkinitDebug("verify_pkinit_request: client cert does not match\n");
        krtn = KDC_ERR_CLIENT_NOT_TRUSTED;
        goto cleanup;
    }
#endif   /* REQUIRE_CLIENT_CERT_MATCH */
    krtn = 0;
    setflag(enc_tkt_reply->flags, TKT_FLG_PRE_AUTH);

cleanup:
    if(pa_cksum.contents) {
        free(pa_cksum.contents);
    }
    if (tmp_as_req) {
        krb5_free_kdc_req(context, tmp_as_req);
    }
    if (der_req) {
        krb5_free_data(context, der_req);
    }
    if(cert_hash) {
        free(cert_hash);
    }
    if(client_cert.data) {
        free(client_cert.data);
    }
    kdcPkinitDebug("verify_pkinit_request: returning %d\n", (int)krtn);
    return krtn;
}

static krb5_error_code return_pkinit_response(
    krb5_context context,
    krb5_pa_data * padata,
    krb5_db_entry *client,
    krb5_data *req_pkt,
    krb5_kdc_req *request,
    krb5_kdc_rep *reply,
    krb5_key_data *client_key,
    krb5_keyblock *encrypting_key,
    krb5_pa_data **send_pa,
    preauth_get_entry_data_proc pkinit_get_entry_data,
    void *pa_module_context,
    void **pa_request_context)
{
    krb5_error_code             krtn;
    krb5_data                   pa_data;
    krb5_pkinit_signing_cert_t  signing_cert = NULL;
    krb5_checksum               as_req_checksum = {0};
    krb5_data                   *encoded_as_req = NULL;
    krb5int_algorithm_id        *cms_types = NULL;
    krb5_ui_4                   num_cms_types = 0;

    /* the data we get from the AS-REQ */
    krb5_ui_4                   nonce = 0;
    krb5_data                   client_cert = {0};

    /*
     * Trusted CA list and specific KC cert optionally obtained via
     * krb5int_pkinit_as_req_parse(). All are DER-encoded
     * issuerAndSerialNumbers.
     */
    krb5_data                   *trusted_CAs = NULL;
    krb5_ui_4                   num_trusted_CAs;
    krb5_data                   kdc_cert = {0};

    if (padata == NULL) {
        /* Client has to send us something */
        return 0;
    }

    kdcPkinitDebug("return_pkinit_response\n");
    pa_data.data = (char *)padata->contents;
    pa_data.length = padata->length;

    /*
     * We've already verified; just obtain the fields we need to create a response
     */
    krtn = krb5int_pkinit_as_req_parse(context,
                                       &pa_data,
                                       NULL, NULL, &nonce,     /* ctime, cusec, nonce */
                                       NULL, NULL,             /* pa_cksum, cert_status */
                                       &num_cms_types, &cms_types,
                                       &client_cert,   /* signer_cert: we encrypt for this */
                                       /* remaining fields unused (for now) */
                                       NULL, NULL,     /* num_all_certs, all_certs */
                                       &num_trusted_CAs, &trusted_CAs,
                                       &kdc_cert);
    if(krtn) {
        kdcPkinitDebug("pa_pk_as_req_parse returned %d; PKINIT aborting.\n",
                       (int)krtn);
        goto cleanup;
    }
    if(client_cert.data == NULL) {
        kdcPkinitDebug("pa_pk_as_req_parse failed to give a client_cert; aborting.\n");
        krtn = KRB5KDC_ERR_PREAUTH_FAILED;
        goto cleanup;
    }

    if(krb5_pkinit_get_kdc_cert(num_trusted_CAs, trusted_CAs,
                                (kdc_cert.data ? &kdc_cert : NULL),
                                &signing_cert)) {
        /*
         * Since get_pkinit_edata was able to obtain *some* KDC cert,
         * this means that we can't satisfy the client's requirement.
         * FIXME - particular error status for this?
         */
        kdcPkinitDebug("return_pkinit_response: NO appropriate signing cert!\n");
        krtn = KRB5KDC_ERR_PREAUTH_FAILED;
        goto cleanup;
    }

    /*
     * Cook up keyblock for caller and for outgoing AS-REP.
     * FIXME how much is known to be valid about encrypting_key?
     * Will encrypting_key->enctype always be valid here? Seems that
     * if we allow for clients without a shared secret (i.e. preauth
     * by PKINIT only) there won't be a valid encrypting_key set up
     * here for us.
     */
    krb5_free_keyblock_contents(context, encrypting_key);
    krb5_c_make_random_key(context, encrypting_key->enctype, encrypting_key);

    /* calculate checksum of incoming AS-REQ */
    krtn = encode_krb5_as_req(request, &encoded_as_req);
    if(krtn) {
        kdcPkinitDebug("encode_krb5_as_req returned %d; PKINIT aborting.\n", (int)krtn);
        goto cleanup;
    }
    krtn = krb5_c_make_checksum(context, context->kdc_req_sumtype,
                                encrypting_key, KRB5_KEYUSAGE_TGS_REQ_AUTH_CKSUM,
                                encoded_as_req, &as_req_checksum);
    if(krtn) {
        goto cleanup;
    }

    /*
     * FIXME: here we assume that the client has one cert - the one that
     * signed the AuthPack in the request (and that we therefore obtained from
     * krb5int_pkinit_as_req_parse()), and the one we're using to encrypt the
     * ReplyKeyPack with here. This may need rethinking.
     */
    krtn = krb5int_pkinit_as_rep_create(context,
                                        encrypting_key, &as_req_checksum,
                                        signing_cert, TRUE,
                                        &client_cert,
                                        num_cms_types, cms_types,
                                        num_trusted_CAs, trusted_CAs,
                                        (kdc_cert.data ? &kdc_cert : NULL),
                                        &pa_data);
    if(krtn) {
        kdcPkinitDebug("pa_pk_as_rep_create returned %d; PKINIT aborting.\n",
                       (int)krtn);
        goto cleanup;
    }

    *send_pa = (krb5_pa_data *)malloc(sizeof(krb5_pa_data));
    if(*send_pa == NULL) {
        krtn = ENOMEM;
        free(pa_data.data);
        goto cleanup;
    }
    (*send_pa)->magic = KV5M_PA_DATA;
    (*send_pa)->pa_type = KRB5_PADATA_PK_AS_REP;
    (*send_pa)->length = pa_data.length;
    (*send_pa)->contents = (krb5_octet *)pa_data.data;
    krtn = 0;

#if PKINIT_DEBUG
    fprintf(stderr, "return_pkinit_response: SUCCESS\n");
    fprintf(stderr, "nonce 0x%x enctype %d keydata %02x %02x %02x %02x...\n",
            (int)nonce, (int)encrypting_key->enctype,
            encrypting_key->contents[0], encrypting_key->contents[1],
            encrypting_key->contents[2], encrypting_key->contents[3]);
#endif

cleanup:
    /* all of this was allocd by krb5int_pkinit_as_req_parse() */
    if(signing_cert) {
        krb5_pkinit_release_cert(signing_cert);
    }
    if(cms_types) {
        unsigned dex;
        krb5int_algorithm_id *alg_id;

        for(dex=0; dex<num_cms_types; dex++) {
            alg_id = &cms_types[dex];
            if(alg_id->algorithm.data) {
                free(alg_id->algorithm.data);
            }
            if(alg_id->parameters.data) {
                free(alg_id->parameters.data);
            }
        }
        free(cms_types);
    }
    if(trusted_CAs) {
        unsigned dex;
        for(dex=0; dex<num_trusted_CAs; dex++) {
            free(trusted_CAs[dex].data);
        }
        free(trusted_CAs);
    }
    if(kdc_cert.data) {
        free(kdc_cert.data);
    }
    if(client_cert.data) {
        free(client_cert.data);
    }
    if(encoded_as_req) {
        krb5_free_data(context, encoded_as_req);
    }
    return krtn;
}

#endif /* APPLE_PKINIT */

/*
 * Returns TRUE if the PAC should be included
 */
krb5_boolean
include_pac_p(krb5_context context, krb5_kdc_req *request)
{
    krb5_error_code             code;
    krb5_pa_data                **padata;
    krb5_boolean                retval = TRUE; /* default is to return PAC */
    krb5_data                   data;
    krb5_pa_pac_req             *req = NULL;

    if (request->padata == NULL) {
        return retval;
    }

    for (padata = request->padata; *padata != NULL; padata++) {
        if ((*padata)->pa_type == KRB5_PADATA_PAC_REQUEST) {
            data.data = (char *)(*padata)->contents;
            data.length = (*padata)->length;

            code = decode_krb5_pa_pac_req(&data, &req);
            if (code == 0) {
                retval = req->include_pac;
                krb5_free_pa_pac_req(context, req);
                req = NULL;
            }
            break;
        }
    }

    return retval;
}

static krb5_error_code
return_referral_enc_padata( krb5_context context,
                            krb5_enc_kdc_rep_part *reply,
                            krb5_db_entry *server)
{
    krb5_error_code             code;
    krb5_tl_data                tl_data;
    krb5_pa_data                pa_data;

    tl_data.tl_data_type = KRB5_TL_SVR_REFERRAL_DATA;
    code = krb5_dbe_lookup_tl_data(context, server, &tl_data);
    if (code || tl_data.tl_data_length == 0)
        return 0;

    pa_data.magic = KV5M_PA_DATA;
    pa_data.pa_type = KRB5_PADATA_SVR_REFERRAL_INFO;
    pa_data.length = tl_data.tl_data_length;
    pa_data.contents = tl_data.tl_data_contents;
    return add_pa_data_element(context, &pa_data, &reply->enc_padata, TRUE);
}

krb5_error_code
return_enc_padata(krb5_context context, krb5_data *req_pkt,
                  krb5_kdc_req *request, krb5_keyblock *reply_key,
                  krb5_db_entry *server, krb5_enc_kdc_rep_part *reply_encpart,
                  krb5_boolean is_referral)
{
    krb5_error_code code = 0;
    /* This should be initialized and only used for Win2K compat and other
     * specific standardized uses such as FAST negotiation. */
    assert(reply_encpart->enc_padata == NULL);
    if (is_referral) {
        code = return_referral_enc_padata(context, reply_encpart, server);
        if (code)
            return code;
    }
    code = kdc_handle_protected_negotiation(req_pkt, request, reply_key,
                                            &reply_encpart->enc_padata);
    if (code)
        goto cleanup;
    /*Add potentially other enc_padata providers*/
cleanup:
    return code;
}


#if 0
static krb5_error_code return_server_referral(krb5_context context,
                                              krb5_pa_data * padata,
                                              krb5_db_entry *client,
                                              krb5_db_entry *server,
                                              krb5_kdc_req *request,
                                              krb5_kdc_rep *reply,
                                              krb5_key_data *client_key,
                                              krb5_keyblock *encrypting_key,
                                              krb5_pa_data **send_pa)
{
    krb5_error_code             code;
    krb5_tl_data                tl_data;
    krb5_pa_data                *pa_data;
    krb5_enc_data               enc_data;
    krb5_data                   plain;
    krb5_data                   *enc_pa_data;

    *send_pa = NULL;

    tl_data.tl_data_type = KRB5_TL_SERVER_REFERRAL;

    code = krb5_dbe_lookup_tl_data(context, server, &tl_data);
    if (code || tl_data.tl_data_length == 0)
        return 0; /* no server referrals to return */

    plain.length = tl_data.tl_data_length;
    plain.data = tl_data.tl_data_contents;

    /* Encrypt ServerReferralData */
    code = krb5_encrypt_helper(context, encrypting_key,
                               KRB5_KEYUSAGE_PA_SERVER_REFERRAL_DATA,
                               &plain, &enc_data);
    if (code)
        return code;

    /* Encode ServerReferralData into PA-SERVER-REFERRAL-DATA */
    code = encode_krb5_enc_data(&enc_data, &enc_pa_data);
    if (code) {
        krb5_free_data_contents(context, &enc_data.ciphertext);
        return code;
    }

    krb5_free_data_contents(context, &enc_data.ciphertext);

    /* Return PA-SERVER-REFERRAL-DATA */
    pa_data = (krb5_pa_data *)malloc(sizeof(*pa_data));
    if (pa_data == NULL) {
        krb5_free_data(context, enc_pa_data);
        return ENOMEM;
    }

    pa_data->magic = KV5M_PA_DATA;
    pa_data->pa_type = KRB5_PADATA_SVR_REFERRAL_INFO;
    pa_data->length = enc_pa_data->length;
    pa_data->contents = enc_pa_data->data;

    free(enc_pa_data); /* don't free contents */

    *send_pa = pa_data;

    return 0;
}
#endif
