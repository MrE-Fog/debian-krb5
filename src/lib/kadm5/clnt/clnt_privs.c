/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 * Copyright 1993 OpenVision Technologies, Inc., All Rights Reserved.
 *
 * $Id: clnt_privs.c 25141 2011-09-04 23:52:11Z raeburn $
 * $Source$
 *
 */

#include    <gssrpc/rpc.h>
#include    <kadm5/admin.h>
#include    <kadm5/kadm_rpc.h>
#include    "client_internal.h"

kadm5_ret_t kadm5_get_privs(void *server_handle, long *privs)
{
    getprivs_ret *r;
    kadm5_server_handle_t handle = server_handle;

    r = get_privs_2(&handle->api_version, handle->clnt);
    if (r == NULL)
        return KADM5_RPC_ERROR;
    else if (r->code == KADM5_OK)
        *privs = r->privs;

    return r->code;
}
