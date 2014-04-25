/* Copyright (C) 2014 Timo Dritschler <timo.dritschler@kit.edu>
   (Karlsruhe Institute of Technology)

   This library is free software; you can redistribute it and/or modify it
   under the terms of the GNU Lesser General Public License as published by the
   Free Software Foundation; either version 2.1 of the License, or (at your
   option) any later version.

   This library is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
   FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
   details.

   You should have received a copy of the GNU Lesser General Public License along
   with this library; if not, write to the Free Software Foundation, Inc., 51
   Franklin St, Fifth Floor, Boston, MA 02110, USA
*/

/**
 * SECTION: kiro-client
 * @Short_description: KIRO RDMA Client / Consumer
 * @Title: KiroClient
 *
 * KiroClient implements the client / active / consumer side of the the RDMA
 * Communication Channel. It uses a KIRO-CLIENT to manage data read from the Server.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <rdma/rdma_verbs.h>
#include <glib.h>
#include "kiro-client.h"
#include "kiro-rdma.h"

#include <errno.h>


/*
 * Definition of 'private' structures and members and macro to access them
 */

#define KIRO_CLIENT_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), KIRO_TYPE_CLIENT, KiroClientPrivate))

struct _KiroClientPrivate {

    /* Properties */
    // PLACEHOLDER //

    /* 'Real' private structures */
    /* (Not accessible by properties) */
    struct rdma_event_channel   *ec;    // Main Event Channel
    struct rdma_cm_id           *conn;  // Connection to the Server
    

};


G_DEFINE_TYPE_WITH_PRIVATE (KiroClient, kiro_client, G_TYPE_OBJECT);


static void kiro_client_init (KiroClient *self)
{
    KiroClientPrivate *priv = KIRO_CLIENT_GET_PRIVATE(self);
    memset(priv, 0, sizeof(&priv));
}

static void
kiro_client_finalize (GObject *object)
{
    //PASS
}

static void
kiro_client_class_init (KiroClientClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    gobject_class->finalize = kiro_client_finalize;
}



int kiro_client_connect (KiroClient *self, char *address, char* port)
{
    KiroClientPrivate *priv = KIRO_CLIENT_GET_PRIVATE(self);

    if(priv->conn)
    {
        printf("Already connected to server.\n");
        return -1;
    }
    
    struct rdma_addrinfo hints, *res_addrinfo;
    memset(&hints, 0, sizeof(hints));
    hints.ai_port_space = RDMA_PS_IB;
    if(rdma_getaddrinfo(address, port, &hints, &res_addrinfo))
    {
        printf("Failed to contruct address information for %s:%s\n",address, port);
        return -1;
    }
    printf("Address information created.\n");
    
    struct ibv_qp_init_attr qp_attr;
    memset(&qp_attr, 0, sizeof(qp_attr));
    qp_attr.cap.max_send_wr = 10;
    qp_attr.cap.max_recv_wr = 10;
    qp_attr.cap.max_send_sge = 1;
    qp_attr.cap.max_recv_sge = 1;
    qp_attr.qp_context = priv->conn;
    qp_attr.sq_sig_all = 1;
    
    if(rdma_create_ep(&(priv->conn), res_addrinfo, NULL, &qp_attr))
    {
        printf("Endpoint creation failed with error: %i\n", errno);
        return -1;
    }
    printf("Route to server resolved.\n");
    
    struct kiro_connection_context *ctx = (struct kiro_connection_context *)calloc(1,sizeof(struct kiro_connection_context));
    if(!ctx)
    {
        printf("Failed to create connection context.\n");
        rdma_destroy_ep(priv->conn);
        return -1;
    }
    
    ctx->cf_mr_send = (struct kiro_rdma_mem *)calloc(1, sizeof(struct kiro_rdma_mem));
    ctx->cf_mr_recv = (struct kiro_rdma_mem *)calloc(1, sizeof(struct kiro_rdma_mem));
    if(!ctx->cf_mr_recv || !ctx->cf_mr_send)
    {
        printf("Failed to allocate Control Flow Memory Container.\n");
        kiro_destroy_connection_context(ctx);
        rdma_destroy_ep(priv->conn);
        return -1;
    }
    
    ctx->cf_mr_recv = kiro_create_rdma_memory(priv->conn->pd, sizeof(struct kiro_ctrl_msg), IBV_ACCESS_LOCAL_WRITE);
    ctx->cf_mr_send = kiro_create_rdma_memory(priv->conn->pd, sizeof(struct kiro_ctrl_msg), IBV_ACCESS_LOCAL_WRITE);
    if(!ctx->cf_mr_recv || !ctx->cf_mr_send)
    {
        printf("Failed to register control message memory.\n");
        kiro_destroy_connection_context(ctx);
        rdma_destroy_ep(priv->conn);
        return -1;
    }
    ctx->cf_mr_recv->size = ctx->cf_mr_send->size = sizeof(struct kiro_ctrl_msg);
    priv->conn->context = ctx;
    
    if(rdma_post_recv(priv->conn, priv->conn, ctx->cf_mr_recv->mem, ctx->cf_mr_recv->size, ctx->cf_mr_recv->mr))
    {
        printf("Posting preemtive receive for connection failed with error: %i\n", errno);
        kiro_destroy_connection_context(ctx);
        rdma_destroy_ep(priv->conn);
        return -1;
    }
    
    if(rdma_connect(priv->conn, NULL))
    {
        printf("Failed to establish connection to the server.\n");
        kiro_destroy_connection_context(ctx);
        rdma_destroy_ep(priv->conn);
        return -1;
    }
    
    priv->ec = rdma_create_event_channel();
    int oldflags = fcntl (priv->ec->fd, F_GETFL, 0);
    /* Only change the FD Mode if we were able to get its flags */
    if (oldflags >= 0) {
        oldflags |= O_NONBLOCK;
        /* Store modified flag word in the descriptor. */
        fcntl (priv->ec->fd, F_SETFL, oldflags);
    }
    if(rdma_migrate_id(priv->conn, priv->ec))
    {
        printf("Was unable to migrate connection to new Event Channel.\n");
        rdma_disconnect(priv->conn);
        kiro_destroy_connection_context(ctx);
        rdma_destroy_ep(priv->conn);
        return -1;
    }
    
    //ToDo:
    //Create TRB, request RDMA from Server, call kiro_client_sync, ???, Profit!
    
    
    printf("Connected to server.\n");
    return 0;
    
}



int kiro_client_sync (KiroClient *self)
{
    //PASS
    return 0;
}










