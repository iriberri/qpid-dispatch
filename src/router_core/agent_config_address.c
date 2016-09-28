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

#include <qpid/dispatch/ctools.h>
#include "agent_config_address.h"
#include <inttypes.h>
#include <stdio.h>
#include <qpid/dispatch/agent.h>
#include "schema_enum.h"

#define QDR_CONFIG_ADDRESS_NAME          0
#define QDR_CONFIG_ADDRESS_IDENTITY      1
#define QDR_CONFIG_ADDRESS_TYPE          2
#define QDR_CONFIG_ADDRESS_PREFIX        3
#define QDR_CONFIG_ADDRESS_DISTRIBUTION  4
#define QDR_CONFIG_ADDRESS_WAYPOINT      5
#define QDR_CONFIG_ADDRESS_IN_PHASE      6
#define QDR_CONFIG_ADDRESS_OUT_PHASE     7

const char *qdr_config_address_columns[] =
    {"name",
     "identity",
     "type",
     "prefix",
     "distribution",
     "waypoint",
     "ingressPhase",
     "egressPhase",
     0};

const char *CONFIG_ADDRESS_TYPE = "org.apache.qpid.dispatch.router.config.address";

static void qdr_config_address_insert_column_CT(qdr_address_config_t *addr, int attr_id, qd_agent_request_t *request)
{
    const char *text = 0;
    const char *key;

    //if (as_map)
    //    qd_compose_insert_string(body, qdr_config_address_columns[col]);

    switch(attr_id) {
    case QD_SCHEMA_ADDRESS_ATTRIBUTES_NAME:
        qd_agent_request_set_string(request, addr->name);
        break;

    case QD_SCHEMA_ADDRESS_ATTRIBUTES_IDENTITY: {
        char id_str[100];
        snprintf(id_str, 100, "%"PRId64, addr->identity);
        qd_agent_request_set_string(request, id_str);
        break;
    }

    case QD_SCHEMA_ADDRESS_ATTRIBUTES_TYPE:
        qd_agent_request_set_string(request, (char *)CONFIG_ADDRESS_TYPE);
        break;

    case QD_SCHEMA_ADDRESS_ATTRIBUTES_PREFIX:
        key = (const char*) qd_hash_key_by_handle(addr->hash_handle);
        if (key && key[0] == 'Z')
            qd_agent_request_set_string(request, (char *)&key[1]);
        else
            qd_agent_request_set_null(request);
        break;

    case QD_SCHEMA_ADDRESS_ATTRIBUTES_DISTRIBUTION:
        switch (addr->treatment) {
        case QD_TREATMENT_MULTICAST_FLOOD:
        case QD_TREATMENT_MULTICAST_ONCE:   text = "multicast"; break;
        case QD_TREATMENT_ANYCAST_CLOSEST:  text = "closest";   break;
        case QD_TREATMENT_ANYCAST_BALANCED: text = "balanced";  break;
        default:
            text = 0;
        }

        qd_agent_request_set_string(request, (char *)text);
        break;

    case QD_SCHEMA_ADDRESS_ATTRIBUTES_WAYPOINT:
        qd_agent_request_set_bool(request, addr->in_phase == 0 && addr->out_phase == 1);
        break;

    case QD_SCHEMA_ADDRESS_ATTRIBUTES_INGRESSPHASE:
        qd_agent_request_set_int(request, addr->in_phase);
        break;

    case QD_SCHEMA_ADDRESS_ATTRIBUTES_EGRESSPHASE:
        qd_agent_request_set_int(request, addr->out_phase);
        break;
    }
}


static void qdr_manage_advance_config_address_CT(qdr_query_t *query, qdr_address_config_t *addr)
{
    query->next_offset++;
    addr = DEQ_NEXT(addr);
    query->more = !!addr;
}



void qdra_config_address_get_next_CT(qdr_core_t *core, qdr_query_t *query)
{
    qdr_address_config_t *addr = 0;

    //
    // Queries that get this far will always succeed.
    //
    query->status = QD_AMQP_OK;

    int offset = qd_agent_get_request_offset(query->request);

    //
    // If we have already populated the next_offset, use it.
    //

    if (query->next_offset > 0) {
        if (query->next_offset < DEQ_SIZE(core->addr_config)) {
            addr = DEQ_HEAD(core->addr_config);
            for (int i = 0; i < query->next_offset && addr; i++)
                addr = DEQ_NEXT(addr);
        }
    }
    else {
        //
        // If the offset goes beyond the set of objects, end the query now.
        //
        if (offset >= DEQ_SIZE(core->addr_config)) {
            query->more = false;
        }
        else {
            //
            // Run to the object at the offset.
            //
            addr = DEQ_HEAD(core->addr_config);

            for (int i = 0; i < offset && addr; i++)
                addr = DEQ_NEXT(addr);

            //
            // Advance to the next address
            //
            query->next_offset = offset;
        }
    }

    if (addr) {
        //
        // Write the columns of the addr entity into the response body.
        //
        qd_agent_request_write_object(query->request, addr);

        //
        // Advance to the next object
        //
        qdr_manage_advance_config_address_CT(query, addr);
    } else {
        query->more = false;
    }

    //
    // Enqueue the response.
    //
    qdr_agent_enqueue_response_CT(core, query);
}


static qd_address_treatment_t qdra_address_treatment_CT(qd_field_iterator_t *iter)
{
    if (iter) {
        if (qd_field_iterator_equal(iter, (unsigned char*) "multicast"))    return QD_TREATMENT_MULTICAST_ONCE;
        if (qd_field_iterator_equal(iter, (unsigned char*) "closest"))      return QD_TREATMENT_ANYCAST_CLOSEST;
        if (qd_field_iterator_equal(iter, (unsigned char*) "balanced"))     return QD_TREATMENT_ANYCAST_BALANCED;
    }
    return QD_TREATMENT_ANYCAST_BALANCED;
}


static qdr_address_config_t *qdr_address_config_find_by_identity_CT(qdr_core_t *core, qd_field_iterator_t *identity)
{
    if (!identity)
        return 0;

    qdr_address_config_t *rc = DEQ_HEAD(core->addr_config);
    while (rc) {
        // Convert the passed in identity to a char*
        char id[100];
        snprintf(id, 100, "%"PRId64, rc->identity);
        if (qd_field_iterator_equal(identity, (const unsigned char*) id))
            break;
        rc = DEQ_NEXT(rc);
    }

    return rc;

}


static qdr_address_config_t *qdr_address_config_find_by_name_CT(qdr_core_t *core, qd_field_iterator_t *name)
{
    if (!name)
        return 0;

    qdr_address_config_t *rc = DEQ_HEAD(core->addr_config);
    while (rc) { // Sometimes the name can be null
        if (rc->name && qd_field_iterator_equal(name, (const unsigned char*) rc->name))
            break;
        rc = DEQ_NEXT(rc);
    }

    return rc;
}


void qdra_config_address_delete_CT(qdr_core_t          *core,
                                   qdr_query_t         *query)
{
    qd_agent_request_t *request = query->request;

    qd_field_iterator_t *name = qd_agent_get_request_name(request);
    qd_field_iterator_t *identity = qd_agent_get_request_identity(request);


    qdr_address_config_t *addr = 0;

    if (!name && !identity) {
        query->status = QD_AMQP_BAD_REQUEST;
        query->status.description = "No name or identity provided";
        qd_log(core->agent_log, QD_LOG_ERROR, "Error performing DELETE of %s: %s", CONFIG_ADDRESS_TYPE, query->status.description);
    }
    else {
        if (identity)
            addr = qdr_address_config_find_by_identity_CT(core, identity);
        else if (name)
            addr = qdr_address_config_find_by_name_CT(core, name);

        if (addr) {
            //
            // Remove the address from the list and the hash index.
            //
            qd_hash_remove_by_handle(core->addr_hash, addr->hash_handle);
            DEQ_REMOVE(core->addr_config, addr);

            //
            // Free resources associated with this address.
            //
            if (addr->name)
                free(addr->name);
            free_qdr_address_config_t(addr);

            query->status = QD_AMQP_NO_CONTENT;
        } else
            query->status = QD_AMQP_NOT_FOUND;
    }

    //
    // Enqueue the response.
    //
    qdr_agent_enqueue_response_CT(core, query);
}


void qdra_get_config_address_attr(void* obj, int attr_id, qd_agent_request_t *request)
{
    qdr_address_config_t *addr = (qdr_address_config_t *)obj;
    qdr_config_address_insert_column_CT(addr, attr_id, request);
}

void qdra_config_address_create_CT(qdr_core_t  *core,
                                   qdr_query_t *query)
{

    qd_agent_request_t *request = query->request;

    qd_field_iterator_t *name = qd_agent_get_request_name(request);
    while (true) {
        //
        // Ensure there isn't a duplicate name and that the body is a map
        //
        qdr_address_config_t *addr = DEQ_HEAD(core->addr_config);
        while (addr) {
            if (name && addr->name && qd_field_iterator_equal(name, (const unsigned char*) addr->name))
                break;
            addr = DEQ_NEXT(addr);
        }

        if (!!addr) {
            query->status = QD_AMQP_BAD_REQUEST;
            query->status.description = "Name conflicts with an existing entity";
            qd_log(core->agent_log, QD_LOG_ERROR, "Error performing CREATE of %s: %s", CONFIG_ADDRESS_TYPE, query->status.description);
            break;
        }

        char *prefix = qd_agent_request_get_string(request, QD_SCHEMA_ADDRESS_ATTRIBUTES_PREFIX);

        //
        // Prefix field is mandatory.  Fail if it is not here.
        //
        if (!prefix) {
            query->status = QD_AMQP_BAD_REQUEST;
            query->status.description = "prefix field is mandatory";
            qd_log(core->agent_log, QD_LOG_ERROR, "Error performing CREATE of %s: %s", CONFIG_ADDRESS_TYPE, query->status.description);
            break;
        }

        //
        // Ensure that there isn't another configured address with the same prefix
        //



        qd_field_iterator_t *iter = qd_address_iterator_string(prefix, ITER_VIEW_ADDRESS_HASH);
        //qd_address_iterator_reset_view(iter, ITER_VIEW_ADDRESS_HASH);
        qd_address_iterator_override_prefix(iter, 'Z');
        addr = 0;
        qd_hash_retrieve(core->addr_hash, iter, (void**) &addr);
        if (!!addr) {
            query->status = QD_AMQP_BAD_REQUEST;
            query->status.description = "Address prefix conflicts with an existing entity";
            qd_log(core->agent_log, QD_LOG_ERROR, "Error performing CREATE of %s: %s", CONFIG_ADDRESS_TYPE, query->status.description);
            break;
        }

        bool waypoint  = qd_agent_request_get_bool(request, QD_SCHEMA_ADDRESS_ATTRIBUTES_WAYPOINT);
        int  in_phase  = qd_agent_request_get_long(request, QD_SCHEMA_ADDRESS_ATTRIBUTES_INGRESSPHASE);
        int  out_phase = qd_agent_request_get_long(request, QD_SCHEMA_ADDRESS_ATTRIBUTES_EGRESSPHASE);
        char *distribution = qd_agent_request_get_string(request, QD_SCHEMA_ADDRESS_ATTRIBUTES_DISTRIBUTION);

        //
        // Handle the address-phasing logic.  If the phases are provided, use them.  Otherwise
        // use the waypoint flag to set the most common defaults.
        //
        if (in_phase == -1 && out_phase == -1) {
            in_phase  = 0;
            out_phase = waypoint ? 1 : 0;
        }

        //
        // Validate the phase values
        //
        if (in_phase < 0 || in_phase > 9 || out_phase < 0 || out_phase > 9) {
            query->status = QD_AMQP_BAD_REQUEST;
            query->status.description = "Phase values must be between 0 and 9";
            qd_log(core->agent_log, QD_LOG_ERROR, "Error performing CREATE of %s: %s", CONFIG_ADDRESS_TYPE, query->status.description);
            break;
        }

        //
        // The request is good.  Create the entity and insert it into the hash index and list.
        //
        addr = new_qdr_address_config_t();
        DEQ_ITEM_INIT(addr);
        addr->name      = name ? (char*) qd_field_iterator_copy(name) : 0;
        addr->identity  = qdr_identifier(core);
        addr->treatment = qdra_address_treatment_CT(qd_address_iterator_string(distribution, ITER_VIEW_ALL));
        addr->in_phase  = in_phase;
        addr->out_phase = out_phase;

        qd_hash_insert(core->addr_hash, iter, addr, &addr->hash_handle);
        DEQ_INSERT_TAIL(core->addr_config, addr);

        query->status = QD_AMQP_CREATED;
        qd_agent_request_write_object(request, addr);
        break;
    }

    qdr_agent_enqueue_response_CT(core, query);
}


void qdra_config_address_get_CT(qdr_core_t          *core,
                                qdr_query_t         *query)
{
    qdr_address_config_t *addr = 0;

    qd_agent_request_t *request = query->request;

    qd_field_iterator_t *name = qd_agent_get_request_name(request);
    qd_field_iterator_t *identity = qd_agent_get_request_identity(request);

    if (!name && !identity) {
        query->status = QD_AMQP_BAD_REQUEST;
        query->status.description = "No name or identity provided";
        qd_log(core->agent_log, QD_LOG_ERROR, "Error performing READ of %s: %s", CONFIG_ADDRESS_TYPE, query->status.description);
    }
    else {
        if (identity) //If there is identity, ignore the name
            addr = qdr_address_config_find_by_identity_CT(core, identity);
        else if (name)
            addr = qdr_address_config_find_by_name_CT(core, name);

        if (addr == 0) {
            // Send back a 404
            query->status = QD_AMQP_NOT_FOUND;
        }
        else {
            //
            // Write the columns of the address entity into the response body.
            //
            //qdr_manage_write_config_address_map_CT(core, addr, query->body, qdr_config_address_columns);
            qd_agent_request_write_object(request, addr);
            query->status = QD_AMQP_OK;
        }
    }

    //
    // Enqueue the response.
    //
    qdr_agent_enqueue_response_CT(core, query);

}
