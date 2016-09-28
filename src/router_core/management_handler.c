/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include <qpid/dispatch/amqp.h>
#include "agent_config_address.h"
#include "agent_config_link_route.h"
#include "agent_config_auto_link.h"
#include "agent_address.h"
#include "agent_link.h"
#include "router_core_private.h"
#include <stdio.h>
#include <qpid/dispatch/agent.h>
#include "schema_enum.h"

static void qdr_manage_read_CT(qdr_core_t *core, qdr_action_t *action, bool discard);
static void qdr_manage_create_CT(qdr_core_t *core, qdr_action_t *action, bool discard);
static void qdr_manage_delete_CT(qdr_core_t *core, qdr_action_t *action, bool discard);
static void qdr_manage_update_CT(qdr_core_t *core, qdr_action_t *action, bool discard);

ALLOC_DECLARE(qdr_query_t);
ALLOC_DEFINE(qdr_query_t);

const char * MANAGEMENT_INTERNAL = "_local/$_management_internal";

typedef struct qd_management_context_t {
    qdr_query_t *query;
    int          current_count;
} qd_management_context_t ;

ALLOC_DECLARE(qd_management_context_t);
ALLOC_DEFINE(qd_management_context_t);

/**
 * Convenience function to create and initialize context (qd_management_context_t)
 */
static qd_management_context_t* qd_management_context()
{
    qd_management_context_t *ctx = new_qd_management_context_t();
    ctx->current_count = 0;

    return ctx;
}

//==================================================================================
// Internal Functions
//==================================================================================

static void qd_manage_response_handler(void *context, const qd_amqp_error_t *status, qd_agent_request_t *request, bool more)
{
    qd_management_context_t *ctx = (qd_management_context_t*) context;

    if (qd_agent_get_request_operation(request) == QD_SCHEMA_ENTITY_OPERATION_QUERY) {

        if (status->status / 100 == 2) { // There is no error, proceed to conditionally call get_next

            if (more) {
               ctx->current_count++; // Increment how many you have at hand
               if (qd_agent_get_request_count(request) != ctx->current_count) {
                       qdr_query_get_next(ctx->query);
                   return;
               } else {
                   //
                   // This is the one case where the core agent won't free the query itself.
                   //
                   qdr_query_free(ctx->query);
               }
            }
        }
    }

    qd_agent_t *agent = qd_agent_get_request_agent(request);

    qdr_core_t *core = qd_agent_get_router_core(agent);

    qd_agent_request_complete(core, status, request);

    // Free the request and its components

    free_qd_management_context_t(ctx);
}


static void qd_core_agent_query_handler(void *ctx, qd_agent_request_t *request)
{
    qd_management_context_t *context = qd_management_context();

    qdr_core_t *core = (qdr_core_t *)ctx;

    // Set the callback function.
    qdr_manage_handler(core, qd_manage_response_handler);

    context->query = qdr_manage_query(context, core, request);

    qdr_query_get_next(context->query);
}


static void qd_core_agent_read_handler(void *ctx, qd_agent_request_t *request)
{
    qd_management_context_t *context = qd_management_context();

    qdr_core_t *core = (qdr_core_t *)ctx;

    // Set the callback function.
    qdr_manage_handler(core, qd_manage_response_handler);

    qdr_manage_read(context, core, request);
}


static void qd_core_agent_create_handler(void *ctx, qd_agent_request_t *request)
{
    qd_management_context_t *context = qd_management_context();

    qdr_core_t *core = (qdr_core_t *)ctx;

    // Set the callback function.
    qdr_manage_handler(core, qd_manage_response_handler);

    qdr_manage_create(context, core, request);
}

static void qd_core_agent_update_handler(void *ctx, qd_agent_request_t *request)
{
    qd_management_context_t *context = qd_management_context();

    qdr_core_t *core = (qdr_core_t *)ctx;

    // Set the callback function.
    qdr_manage_handler(core, qd_manage_response_handler);

    qdr_manage_update(context, core, request);
}


static void qd_core_agent_delete_handler(void *ctx, qd_agent_request_t *request)
{
    qd_management_context_t *context = qd_management_context();

    qdr_core_t *core = (qdr_core_t *)ctx;

    // Set the callback function.
    qdr_manage_handler(core, qd_manage_response_handler);

    qdr_manage_delete(context, core, request);
}


void register_handlers_1(qdr_core_t *core, qd_agent_t *agent) {
    qd_agent_register_handlers(core,
                               agent,
                               (int)QD_SCHEMA_ENTITY_TYPE_ROUTER_CONFIG_ADDRESS,
                               qd_core_agent_create_handler,
                               qd_core_agent_read_handler,
                               qd_core_agent_update_handler,
                               qd_core_agent_delete_handler,
                               qd_core_agent_query_handler,
                               qdra_get_config_address_attr);

}


/**
 *
 * Handler for the management agent.
 *
 */
void qdr_management_agent_on_message(void *context, qd_message_t *msg, int unused_link_id, int unused_cost)
{
    qdr_core_t *core = (qdr_core_t*) context;

    qdr_send_to2(core, msg, MANAGEMENT_INTERNAL, false, false);


}


static void qdr_agent_response_handler(void *context)
{
    qdr_core_t  *core = (qdr_core_t*) context;
    qdr_query_t *query;
    bool         done = false;

    while (!done) {
        sys_mutex_lock(core->query_lock);
        query = DEQ_HEAD(core->outgoing_query_list);
        if (query)
            DEQ_REMOVE_HEAD(core->outgoing_query_list);
        done = DEQ_SIZE(core->outgoing_query_list) == 0;
        sys_mutex_unlock(core->query_lock);

        if (query) {
            bool more = query->more;
            core->agent_response_handler(query->context, &query->status, query->request, more);
            //core->agent_response_handler(query->context, &query->status, more);
            if (!more)
                qdr_query_free(query);
        }
    }
}


void qdr_agent_enqueue_response_CT(qdr_core_t *core, qdr_query_t *query)
{
    sys_mutex_lock(core->query_lock);
    DEQ_INSERT_TAIL(core->outgoing_query_list, query);
    bool notify = DEQ_SIZE(core->outgoing_query_list) == 1;
    sys_mutex_unlock(core->query_lock);

    if (notify)
        qd_timer_schedule(core->agent_timer, 0);
}


static void qdrh_query_get_next_CT(qdr_core_t *core, qdr_action_t *action, bool discard);

//==================================================================================
// Interface Functions
//==================================================================================

void qdr_manage_create(void               *context,
                        qdr_core_t        *core,
                       qd_agent_request_t *request)
{
    qdr_action_t *action = qdr_action(qdr_manage_create_CT, "manage_create");

    // Create a query object here
    action->args.agent.query = qdr_query(core, context, request);
    qdr_action_enqueue(core, action);
}


void qdr_manage_delete(void               *context,
                       qdr_core_t         *core,
                       qd_agent_request_t *request)
{
    qdr_action_t *action = qdr_action(qdr_manage_delete_CT, "manage_delete");
    action->args.agent.query = qdr_query(core, context, request);
    qdr_action_enqueue(core, action);
}


void qdr_manage_read(void               *context,
                     qdr_core_t         *core,
                     qd_agent_request_t *request)
{
    qdr_action_t *action = qdr_action(qdr_manage_read_CT, "manage_read");
    action->args.agent.query = qdr_query(core, context, request);
    qdr_action_enqueue(core, action);
}


void qdr_manage_update(void               *context,
                       qdr_core_t         *core,
                       qd_agent_request_t *request)
{
    qdr_action_t *action = qdr_action(qdr_manage_update_CT, "manage_update");
    action->args.agent.query = qdr_query(core, context, request);
    qdr_action_enqueue(core, action);
}



qdr_query_t *qdr_query(qdr_core_t          *core,
                       void                *context,
                       qd_agent_request_t  *request)
{
    qdr_query_t *query = new_qdr_query_t();

    DEQ_ITEM_INIT(query);
    ZERO(query);
    query->core        = core;
    query->context     = context;
    query->more        = false;
    query->request     = request;
    query->next_offset = 0;

    return query;
}


void qdr_query_get_next(qdr_query_t *query)
{
    qdr_action_t *action = qdr_action(qdrh_query_get_next_CT, "qdr_manage_query");
    action->args.agent.query = query;
    qdr_action_enqueue(query->core, action);
}

qdr_query_t *qdr_manage_query(void               *context,
                              qdr_core_t         *core,
                              qd_agent_request_t *request)
{
    return qdr_query(core, context, request);
}


void qdr_query_free(qdr_query_t *query)
{
    if (!query)
        return;

    if (query->next_key)
        qdr_field_free(query->next_key);

    free_qdr_query_t(query);
}



void qdr_manage_handler(qdr_core_t *core, qdr_manage_response_t response_handler)
{
    core->agent_response_handler = response_handler;
}


//==================================================================================
// In-Thread Functions
//==================================================================================

void qdr_agent_setup_CT(qdr_core_t *core)
{
    DEQ_INIT(core->outgoing_query_list);
    core->query_lock  = sys_mutex();
    core->agent_timer = qd_timer(core->qd, qdr_agent_response_handler, core);
}


static void qdr_agent_forbidden(qdr_core_t *core, qdr_query_t *query, bool op_query)
{
    query->status = QD_AMQP_FORBIDDEN;
    if (query->body && !op_query)
        qd_compose_insert_null(query->body);
    qdr_agent_enqueue_response_CT(core, query);
}


static void qdr_manage_read_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    qdr_query_t             *query      = action->args.agent.query;

    switch (query->entity_type) {
    case QD_ROUTER_CONFIG_ADDRESS:    qdra_config_address_get_CT(core, query); break;
    case QD_ROUTER_CONFIG_LINK_ROUTE: qdra_config_link_route_get_CT(core, query); break;
    case QD_ROUTER_CONFIG_AUTO_LINK:  qdra_config_auto_link_get_CT(core, query); break;
    case QD_ROUTER_CONNECTION:        break;
    case QD_ROUTER_LINK:              break;
    case QD_ROUTER_ADDRESS:           qdra_address_get_CT(core, query); break;
    case QD_ROUTER_FORBIDDEN:         qdr_agent_forbidden(core, query, false); break;
    case QD_ROUTER_EXCHANGE:          break;
    case QD_ROUTER_BINDING:           break;
   }
}


static void qdr_manage_create_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    qdr_query_t             *query      = action->args.agent.query;

    switch (query->entity_type) {
        case QD_ROUTER_CONFIG_ADDRESS:    qdra_config_address_create_CT(core, query); break;
        case QD_ROUTER_CONFIG_LINK_ROUTE: qdra_config_link_route_create_CT(core, query); break;
        case QD_ROUTER_CONFIG_AUTO_LINK:  qdra_config_auto_link_create_CT(core, query); break;
        case QD_ROUTER_CONNECTION:        break;
        case QD_ROUTER_LINK:              break;
        case QD_ROUTER_ADDRESS:           break;
        case QD_ROUTER_FORBIDDEN:         qdr_agent_forbidden(core, query, false); break;
        case QD_ROUTER_EXCHANGE:          break;
        case QD_ROUTER_BINDING:           break;
   }
}


static void qdr_manage_delete_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    qdr_query_t             *query      = action->args.agent.query;

    switch (query->entity_type) {
        case QD_ROUTER_CONFIG_ADDRESS:    qdra_config_address_delete_CT(core, query); break;
        case QD_ROUTER_CONFIG_LINK_ROUTE: qdra_config_link_route_delete_CT(core, query); break;
        case QD_ROUTER_CONFIG_AUTO_LINK:  qdra_config_auto_link_delete_CT(core, query); break;
        case QD_ROUTER_CONNECTION:        break;
        case QD_ROUTER_LINK:              break;
        case QD_ROUTER_ADDRESS:           break;
        case QD_ROUTER_FORBIDDEN:         qdr_agent_forbidden(core, query, false); break;
        case QD_ROUTER_EXCHANGE:          break;
        case QD_ROUTER_BINDING:           break;
   }
}

static void qdr_manage_update_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    qdr_query_t             *query      = action->args.agent.query;

    switch (query->entity_type) {
        case QD_ROUTER_CONFIG_ADDRESS:    break;
        case QD_ROUTER_CONFIG_LINK_ROUTE: break;
        case QD_ROUTER_CONFIG_AUTO_LINK:  break;
        case QD_ROUTER_CONNECTION:        break;
        case QD_ROUTER_LINK:              qdra_link_update_CT(core, query); break;
        case QD_ROUTER_ADDRESS:           break;
        case QD_ROUTER_FORBIDDEN:         qdr_agent_forbidden(core, query, false); break;
        case QD_ROUTER_EXCHANGE:          break;
        case QD_ROUTER_BINDING:           break;
   }

}


static void qdrh_query_get_next_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    qdr_query_t *query  = action->args.agent.query;

    if (!discard) {
        switch (query->entity_type) {
            case QD_ROUTER_CONFIG_ADDRESS:
                qdra_config_address_get_next_CT(core, query);
                break;
            case QD_ROUTER_CONFIG_LINK_ROUTE: qdra_config_link_route_get_next_CT(core, query); break;
            case QD_ROUTER_CONFIG_AUTO_LINK:  qdra_config_auto_link_get_next_CT(core, query); break;
            case QD_ROUTER_CONNECTION:        break;
            case QD_ROUTER_LINK:              qdra_link_get_next_CT(core, query); break;
            case QD_ROUTER_ADDRESS:           qdra_address_get_next_CT(core, query); break;
            case QD_ROUTER_FORBIDDEN:         break;
            case QD_ROUTER_EXCHANGE:          break;
            case QD_ROUTER_BINDING:           break;
        }
    }
}

