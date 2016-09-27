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

#include <Python.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <qpid/dispatch/router_core.h>
#include <qpid/dispatch/python_embedded.h>
#include <qpid/dispatch/agent.h>
#include <qpid/dispatch/timer.h>
#include <qpid/dispatch/log.h>

#include "schema_enum.h"
#include "compose_private.h"

#define MANAGEMENT_INTERNAL_MODULE "qpid_dispatch_internal.test_post"
#define MANAGEMENT_MODULE "qpid_dispatch.management"

#define QD_AGENT_MAX_COLUMNS 64
#define QDR_AGENT_COLUMN_NULL (QD_AGENT_MAX_COLUMNS + 1)

//const char **test[2] = {qd_schema_sslProfile_attributes_names, qd_schema_listener_attributes_names};

typedef enum {
    QD_REQUEST_CORRELATION_ID,
    QD_REQUEST_NAME,
    QD_REQUEST_IDENTITY,
    QD_REQUEST_REPLY_TO,
    QD_REQUEST_BODY,
} qd_request_attributes_t;

typedef struct qd_management_work_item_t {
    DEQ_LINKS(struct qd_management_work_item_t);
    qd_agent_request_t *request;
} qd_management_work_item_t;

DEQ_DECLARE(qd_management_work_item_t, qd_management_work_list_t);

typedef struct qd_entity_type_handler_t {
    qd_schema_entity_type_t      entity_type;
    void                         *ctx;
    qd_agent_handler_t           create_handler;
    qd_agent_handler_t           read_handler;
    qd_agent_handler_t           update_handler;
    qd_agent_handler_t           delete_handler;
    qd_agent_handler_t           query_handler;
    qd_agent_attribute_handler_t attribute_handler;
} qd_entity_type_handler_t;

typedef struct {
    PyObject_HEAD
    qd_agent_t *agent;
} AgentAdapter;

struct qd_agent_t {
    qd_dispatch_t             *qd;
    qd_timer_t                *timer;
    AgentAdapter              *adapter;
    char                      *address;
    const char                *config_file;
    qd_management_work_list_t  work_queue;
    sys_mutex_t               *lock;
    qd_log_source_t           *log_source;
    qd_timer_t                *work_timer;
    qdr_core_t                *router_core;
    qd_entity_type_handler_t  *handlers[QD_SCHEMA_ENTITY_TYPE_ENUM_COUNT];
};

struct qd_agent_request_t {
    qd_entity_type_handler_t     *entity_handler;
    qd_parsed_field_t            *params;
    qd_parsed_field_t            *in_body; // The  parsed field that holds the parsed contents of the management request body
    qd_buffer_list_t             *buffers; // A buffer chain holding all the relevant information for the CRUDQ operations.
    void                         *ctx;
    int                           count; // count and offset are for queries
    int                           offset;
    qd_schema_entity_type_t       entity_type;
    qd_schema_entity_operation_t  operation;
    qd_composed_field_t          *out_body; // Body of the outgoing response.
    int                           attribute_count;
    bool                          respond; // Should the agent respond to this request?
    bool                          map_initialized;
    qd_agent_t                   *agent;
    int                           columns[QD_AGENT_MAX_COLUMNS];
};

#define QD_AGENT_MAX_COLUMNS 64
#define QDR_AGENT_COLUMN_NULL (QD_AGENT_MAX_COLUMNS + 1)

static qd_parsed_field_t *qd_get_parsed_field_by_index(qd_agent_request_t *request, qd_request_attributes_t field);
static qd_parsed_field_t *qd_get_parsed_field(qd_agent_request_t *request);

const char * const STATUS_DESCRIPTION = "statusDescription";
const char * const STATUS_CODE = "statusCode";

// Should this function be in agent.c
static int qd_agent_request_get_attribute_count(qd_agent_request_t *request)
{
    int attribute_count = -1;
    switch(request->entity_type) {
        case QD_SCHEMA_ENTITY_TYPE_SSLPROFILE:
            attribute_count = QD_SCHEMA_SSLPROFILE_ATTRIBUTES_ENUM_COUNT;
            break;
        case QD_SCHEMA_ENTITY_TYPE_ROUTER_CONFIG_ADDRESS:
            attribute_count = QD_SCHEMA_ADDRESS_ATTRIBUTES_ENUM_COUNT;
            break;
        case QD_SCHEMA_ENTITY_TYPE_LISTENER:
            attribute_count = QD_SCHEMA_LISTENER_ATTRIBUTES_ENUM_COUNT;
            break;
        default:
            break;

    }
    assert(attribute_count >= 0);

    return attribute_count;
}


static PyObject *qd_post_management_request(PyObject *self,
                                            PyObject *args,
                                            PyObject *keywds)

{
    int operation;    //Is this a CREATE, READ, UPDATE, DELETE or QUERY
    int entity_type;  // Is this a listener or connector or address.... etc.
    int count = 0;        // used for queries only
    int offset = 0;       //used for queries only
    PyObject *cid      = 0;
    PyObject *name     = 0;
    PyObject *identity = 0;
    PyObject *body     = 0;
    PyObject *reply_to = 0;

    static char *kwlist[] = {"operation", "entity_type", "cid", "name", "identity", "body", "reply_to", "count", "offset", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, keywds, "iiOOOOOii", kwlist, &operation, &entity_type, &cid, &name, &identity, &body, &reply_to, &count, &offset))
        return 0;

    qd_composed_field_t *field = qd_compose_subfield(0);


    //
    // Start a list and add cid, name, identity, reply_to, body to the list in that order.
    //
    qd_compose_start_list(field);

    qd_py_to_composed(cid, field);
    qd_py_to_composed(name, field);
    qd_py_to_composed(identity, field);
    qd_py_to_composed(reply_to, field);
    qd_py_to_composed(body, field);

    qd_compose_end_list(field);


    AgentAdapter *adapter = ((AgentAdapter*) self);

    //
    // Create a request and add it to the work_queue
    //
    qd_agent_request_t *request = NEW(qd_agent_request_t);
    request->buffers = qd_compose_buffers(field);

    request->count = count;
    request->offset = offset;
    request->entity_type = entity_type;
    request->operation = operation;
    request->in_body = 0;
    request->params = 0;
    request->out_body = 0;//qd_compose(QD_PERFORMATIVE_BODY_AMQP_VALUE, 0);
    request->map_initialized = false;
    request->respond = !!reply_to;
    request->agent = adapter->agent;

    request->entity_handler = adapter->agent->handlers[entity_type];
    request->attribute_count = qd_agent_request_get_attribute_count(request);

    request->ctx = adapter->agent->handlers[entity_type]->ctx;
    qd_management_work_item_t *work_item = NEW(qd_management_work_item_t);
    DEQ_ITEM_INIT(work_item);
    work_item->request = request;

    //
    // Add work item to the work item list after locking the work item list
    //
    sys_mutex_lock(adapter->agent->lock);
    DEQ_INSERT_TAIL(adapter->agent->work_queue, work_item);
    sys_mutex_unlock(adapter->agent->lock);

    qd_timer_schedule(adapter->agent->work_timer, 0);

    return Py_None;
}

/**
 * Declare all the methods in the AgentAdapter.
 * post_management_request is the name of the method that the python side would call and qd_post_management_request is the C implementation
 * of the function.
 */
static PyMethodDef AgentAdapter_functions[] = {
    //{"post_management_request", (PyCFunction)qd_post_management_request, METH_VARARGS|METH_KEYWORDS, "Posts a management request to a work queue"},
    {"post_management_request", (PyCFunction)qd_post_management_request, METH_VARARGS|METH_KEYWORDS, "Posts a management request to a work queue"},
    {0, 0, 0, 0} // <-- Not sure why we need this
};

static PyTypeObject AgentAdapterType = {
    PyObject_HEAD_INIT(0)
    0,                              /* ob_size*/
    MANAGEMENT_INTERNAL_MODULE ".AgentAdapter",  /* tp_name*/
    sizeof(AgentAdapter),           /* tp_basicsize*/
    0,                              /* tp_itemsize*/
    0,                              /* tp_dealloc*/
    0,                              /* tp_print*/
    0,                              /* tp_getattr*/
    0,                              /* tp_setattr*/
    0,                              /* tp_compare*/
    0,                              /* tp_repr*/
    0,                              /* tp_as_number*/
    0,                              /* tp_as_sequence*/
    0,                              /* tp_as_mapping*/
    0,                              /* tp_hash */
    0,                              /* tp_call*/
    0,                              /* tp_str*/
    0,                              /* tp_getattro*/
    0,                              /* tp_setattro*/
    0,                              /* tp_as_buffer*/
    Py_TPFLAGS_DEFAULT,             /* tp_flags*/
    "Agent request Adapter",        /* tp_doc */
    0,                              /* tp_traverse */
    0,                              /* tp_clear */
    0,                              /* tp_richcompare */
    0,                              /* tp_weaklistoffset */
    0,                              /* tp_iter */
    0,                              /* tp_iternext */
    AgentAdapter_functions,         /* tp_methods */
    0,                              /* tp_members */
    0,                              /* tp_getset */
    0,                              /* tp_base */
    0,                              /* tp_dict */
    0,                              /* tp_descr_get */
    0,                              /* tp_descr_set */
    0,                              /* tp_dictoffset */
    0,                              /* tp_init */
    0,                              /* tp_alloc */
    0,                              /* tp_new */
    0,                              /* tp_free */
    0,                              /* tp_is_gc */
    0,                              /* tp_bases */
    0,                              /* tp_mro */
    0,                              /* tp_cache */
    0,                              /* tp_subclasses */
    0,                              /* tp_weaklist */
    0,                              /* tp_del */
    0                               /* tp_version_tag */
};


static void process_work_queue(void *context)
{
    qd_agent_t *agent = (qd_agent_t *)context;

    qd_management_work_item_t *work_item = DEQ_HEAD(agent->work_queue);

    while(work_item) {
        qd_agent_request_t *request = work_item->request;

        qd_entity_type_handler_t *handler = agent->handlers[request->entity_type];
        switch (request->operation) {
            case QD_SCHEMA_ENTITY_OPERATION_READ:
                handler->read_handler(request->ctx,request);
                break;
            case QD_SCHEMA_ENTITY_OPERATION_DELETE:
                handler->delete_handler(request->ctx, request);
                break;
            case QD_SCHEMA_ENTITY_OPERATION_CREATE:
                handler->create_handler(request->ctx, request);
                break;
            case QD_SCHEMA_ENTITY_OPERATION_UPDATE:
                handler->update_handler(request->ctx, request);
                break;
            case QD_SCHEMA_ENTITY_OPERATION_QUERY:
                handler->query_handler(request->ctx, request);
                break;
            default:
                break;
        }

        work_item = DEQ_NEXT(work_item);
    }
}


qd_agent_t* qd_agent(qd_dispatch_t *qd, char *address, const char *config_path)
{
    //
    // Create a new instance of AgentAdapterType
    //
    AgentAdapterType.tp_new = PyType_GenericNew;
    PyType_Ready(&AgentAdapterType);

    // Load the qpid_dispatch_internal.management Python module
    PyObject *module = PyImport_ImportModule(MANAGEMENT_INTERNAL_MODULE);

    /*if (!module) {
        qd_error_py();
        qd_log(log_source, QD_LOG_CRITICAL, "Cannot load dispatch extension module '%s'", MANAGEMENT_INTERNAL_MODULE);
        abort();
    }*/


    PyTypeObject *agentAdapterType = &AgentAdapterType;
    Py_INCREF(agentAdapterType);

    //Use the "AgentAdapter" name to add the AgentAdapterType to the management
    PyModule_AddObject(module, "AgentAdapter", (PyObject*) &AgentAdapterType);
    PyObject *adapterType     = PyObject_GetAttrString(module, "AgentAdapter");
    PyObject *adapterInstance = PyObject_CallObject(adapterType, 0);

    //
    //Instantiate the new agent and return it
    //
    qd_agent_t *agent = NEW(qd_agent_t);

    agent->qd = qd;
    agent->work_timer = qd_timer(qd, process_work_queue, agent);
    agent->address = address;
    agent->config_file = config_path;
    agent->log_source = qd_log_source("AGENT");
    DEQ_INIT(agent->work_queue);
    agent->lock = sys_mutex();
    AgentAdapter *adapter = ((AgentAdapter*) adapterInstance);
    agent->adapter = adapter;
    adapter->agent = agent;


    //
    // Initialize the handlers to zeros so we can start clean
    //
    for (int i=0; i < QD_SCHEMA_ENTITY_TYPE_ENUM_COUNT; i++)
        agent->handlers[i] = 0;

    Py_DECREF(agentAdapterType);
    Py_DECREF(module);

    return agent;
}

qd_error_t qd_agent_start(qd_agent_t *agent)
{
    // Load the qpid_dispatch_internal.management Python module
    PyObject *module = PyImport_ImportModule(MANAGEMENT_INTERNAL_MODULE);

    char *class = "ManagementAgent";

    //
    //Instantiate the ManagementAgent class found in qpid_dispatch_internal/management/agent.py
    //
    PyObject* pClass = PyObject_GetAttrString(module, class); QD_ERROR_PY_RET();

    //
    // Constructor Arguments for ManagementAgent
    //
    PyObject* pArgs = PyTuple_New(3);

   // arg 0: management address $management
   PyObject *address = PyString_FromString(agent->address);
   PyTuple_SetItem(pArgs, 0, address);

   // arg 1: adapter instance
   PyTuple_SetItem(pArgs, 1, (PyObject*)agent->adapter);

   // arg 2: config file location
   PyObject *config_file = PyString_FromString((char *)agent->config_file);
   PyTuple_SetItem(pArgs, 2, config_file);

   //
   // Instantiate the ManagementAgent class
   //
   PyObject* pyManagementInstance = PyInstance_New(pClass, pArgs, 0); QD_ERROR_PY_RET();
   if (!pyManagementInstance) {
       qd_log(agent->log_source, QD_LOG_CRITICAL, "Cannot create instance of Python class '%s.%s'", MANAGEMENT_INTERNAL_MODULE, class);
   }
   Py_DECREF(pArgs);
   Py_DECREF(pClass);
   return qd_error_code();
}

static void qd_set_properties(qd_agent_request_t *request,
                              qd_field_iterator_t *reply_to,
                              qd_composed_field_t **fld)
{
    // Set the correlation_id and reply_to on fld
    *fld = qd_compose(QD_PERFORMATIVE_PROPERTIES, 0);
    qd_compose_start_list(*fld);
    qd_compose_insert_null(*fld);                           // message-id
    qd_compose_insert_null(*fld);                           // user-id
    qd_compose_insert_string_iterator(*fld, reply_to);      // to
    qd_compose_insert_null(*fld);                           // subject
    qd_compose_insert_null(*fld);


    qd_parsed_field_t * parsed_field = qd_get_parsed_field(request);
    qd_parsed_field_t *correlation_id = qd_parse_sub_value(parsed_field, (int)QD_REQUEST_CORRELATION_ID);
    uint64_t cid = qd_parse_as_long(correlation_id);
    qd_compose_insert_ulong(*fld, cid);

    //qd_compose_insert_typed_iterator(*fld, correlation_id);
    qd_compose_end_list(*fld);
}


/**
 * Sets the error status on a new composed field.
 */
static void qd_set_response_status(const qd_amqp_error_t *error, qd_composed_field_t **field)
{
    //
    // Insert appropriate success or error
    //
    *field = qd_compose(QD_PERFORMATIVE_APPLICATION_PROPERTIES, *field);
    qd_compose_start_map(*field);

    qd_compose_insert_string(*field, STATUS_DESCRIPTION);
    qd_compose_insert_string(*field, error->description);

    qd_compose_insert_string(*field, STATUS_CODE);
    qd_compose_insert_uint(*field, error->status);

    qd_compose_end_map(*field);
}

void qd_agent_request_free(qd_agent_request_t *request)
{
    free(request->entity_handler);
    free(request->params);
    free(request->in_body);
    free(request->out_body);
}


static void send_response(void *ctx, const qd_amqp_error_t *status, qd_agent_request_t *request)
{
    qd_composed_field_t *fld = 0;

    //qd_field_iterator_t     *correlation_id = qd_agent_get_request_correlation_id(request);
    qd_field_iterator_t     *reply_to       = qd_agent_get_request_reply_to(request);

    // Start composing the message.
    // First set the properties on the message like reply_to, correlation-id etc.
    qd_set_properties(request, reply_to, &fld);

    // Second, set the status on the message, QD_AMQP_OK or QD_AMQP_BAD_REQUEST and so on.
    qd_set_response_status(status, &fld);

    qd_message_t *response_msg = qd_message();

    if (status->status / 100 != 2) {
        if (!request->out_body) {
            request->out_body = qd_compose(QD_PERFORMATIVE_BODY_AMQP_VALUE, 0);
            qd_compose_start_map(request->out_body);
            qd_compose_end_map(request->out_body);
        }
    }

    qdr_core_t *core = (qdr_core_t*)ctx;

    // Finally, compose and send the message.
    qd_message_compose_3(response_msg, fld, request->out_body);

    qdr_send_to1(core, response_msg, reply_to, true, false);

    qd_message_free(response_msg);
    qd_compose_free(fld);
    qd_field_iterator_free(reply_to);
    qd_agent_request_free(request);

}

static void qd_agent_request_insert_empty_query_results(qd_agent_request_t *request)
{
    // The body has not even been created, which means that there are no matching rows for the query
    // Simply insert empty attributeNames and results
    request->out_body  = qd_compose(QD_PERFORMATIVE_BODY_AMQP_VALUE, 0);
    // Start a map in the out_body.
    qd_compose_start_map(request->out_body);
    //add a "attributeNames" key to out_body
    qd_compose_insert_string(request->out_body, "attributeNames");
    qd_compose_start_list(request->out_body);
    qd_compose_end_list(request->out_body);

    qd_compose_insert_string(request->out_body, "results");
    qd_compose_start_list(request->out_body);
    qd_compose_end_list(request->out_body);

    qd_compose_end_map(request->out_body);

}


void qd_agent_request_complete(void *ctx, const qd_amqp_error_t *status, qd_agent_request_t *request)
{
    if (request->respond) {
        if (status->status < 400) {
            switch(request->operation) {
                case QD_SCHEMA_ENTITY_OPERATION_QUERY:
                    if (request->out_body) {
                        // An out_body was created and one or more rows have been inserted.
                        qd_compose_end_list(request->out_body); //end the list for results
                        qd_compose_end_map(request->out_body);  // end the response map.
                    }
                    else {
                        // out_body was not even created. This query did not yield any results.
                        // Insert empty results.
                        qd_agent_request_insert_empty_query_results(request);
                    }
                    break;
                case QD_SCHEMA_ENTITY_OPERATION_DELETE:
                    if (!request->out_body) {
                        //amqp-­‐value section containing a map with zero entries
                        request->out_body  = qd_compose(QD_PERFORMATIVE_BODY_AMQP_VALUE, 0);
                        qd_compose_start_map(request->out_body);
                        qd_compose_end_map(request->out_body);
                    }
                    break;
                default:
                    break;
            }
        }

        send_response(ctx, status, request);
    }

}

static void qd_agent_request_insert_attributes_map(qd_agent_request_t *request, void *object)
{
    int entity_type = qd_agent_get_request_entity_type(request);

    if (!request->out_body) {
       request->out_body  = qd_compose(QD_PERFORMATIVE_BODY_AMQP_VALUE, 0);
    }

    qd_compose_start_map(request->out_body);

    for (int i=0; i<request->attribute_count; i++) {
        // Insert the key
        qd_compose_insert_string(request->out_body, qd_schema_attribute_names[entity_type][i]);

        //Insert the value.
        request->entity_handler->attribute_handler(object, i, request);
    }
    qd_compose_end_map(request->out_body);
}

static void qd_agent_set_columns(qd_agent_request_t *request,
                          qd_parsed_field_t *attribute_names,
                          const char *qdr_columns[],
                          int column_count)
{
    if (!attribute_names ||
        (qd_parse_tag(attribute_names) != QD_AMQP_LIST8 &&
         qd_parse_tag(attribute_names) != QD_AMQP_LIST32) ||
        qd_parse_sub_count(attribute_names) == 0 ||
        qd_parse_sub_count(attribute_names) >= QD_AGENT_MAX_COLUMNS) {
        //
        // Either the attribute_names field is absent, it's not a list, or it's an empty list.
        // In this case, we will include all available attributes.
        //
        int i;
        for (i = 0; i < column_count; i++)
            request->columns[i] = i;
        request->columns[i] = -1;
        assert(i < QD_AGENT_MAX_COLUMNS);
        return;
    }

    //
    // We have a valid, non-empty attribute list.  Set the columns appropriately.
    //
    uint32_t count = qd_parse_sub_count(attribute_names);
    uint32_t idx;

    for (idx = 0; idx < count; idx++) {
        qd_parsed_field_t *name = qd_parse_sub_value(attribute_names, idx);
        if (!name || (qd_parse_tag(name) != QD_AMQP_STR8_UTF8 && qd_parse_tag(name) != QD_AMQP_STR32_UTF8))
            request->columns[idx] = QDR_AGENT_COLUMN_NULL;
        else {
            int j = 0;
            while (qdr_columns[j]) {
                qd_field_iterator_t *iter = qd_parse_raw(name);
                if (qd_field_iterator_equal(iter, (const unsigned char*) qdr_columns[j])) {
                    request->columns[idx] = j;
                    break;
                }
                j+=1;
            }
        }
    }
    request->columns[idx+1] = -1;
}


static void qd_agent_write_columns(qd_agent_request_t *request, const char *columns[], int column_count)
{
    qd_compose_start_list(request->out_body);
    int i = 0;
    while (request->columns[i] >= 0) {
        assert(request->columns[i] < column_count);
        qd_compose_insert_string(request->out_body, columns[request->columns[i]]);
        i++;
    }
    qd_compose_end_list(request->out_body);
}

static void qd_agent_request_insert_attributes_list(qd_agent_request_t *request, void *object)
{
    if (!request->map_initialized) {
        qd_composed_field_t *out_body  = qd_compose(QD_PERFORMATIVE_BODY_AMQP_VALUE, 0);
        request->out_body = out_body;

        // Start a map in the out_body.
        qd_compose_start_map(request->out_body);
        //add a "attributeNames" key to out_body
        qd_compose_insert_string(out_body, "attributeNames");

        // Grab the attribute names from the incoming message body. The attribute names will be used later on in the response.
        qd_parsed_field_t *in_body_attribute_names = 0;

        if (request->in_body != 0 && qd_parse_is_map(request->in_body))
            in_body_attribute_names = qd_parse_value_by_key(request->in_body, "attributeNames");

        qd_agent_set_columns(request, in_body_attribute_names, qd_schema_attribute_names[qd_agent_get_request_entity_type(request)], request->attribute_count);

        qd_agent_write_columns(request, qd_schema_attribute_names[qd_agent_get_request_entity_type(request)], request->attribute_count);

        qd_compose_insert_string(request->out_body, "results"); //add a "results" key
        qd_compose_start_list(request->out_body); //start the list for results

        request->map_initialized = true;
    }

    int i=0;
    qd_compose_start_list(request->out_body);
    if (object) {
        while (request->columns[i] >= 0) {
            request->entity_handler->attribute_handler(object, i, request);
            i+=1;
        }
    }
    qd_compose_end_list(request->out_body);
}


void qd_agent_request_write_object(qd_agent_request_t *request, void *object)
{
    if (request->respond) {
        switch(request->operation) {
            case QD_SCHEMA_ENTITY_OPERATION_QUERY:
                qd_agent_request_insert_attributes_list(request, object);
                break;
            case QD_SCHEMA_ENTITY_OPERATION_READ:
            case QD_SCHEMA_ENTITY_OPERATION_UPDATE:
            case QD_SCHEMA_ENTITY_OPERATION_CREATE:
                qd_agent_request_insert_attributes_map(request, object);
                break;
            default:
                break;

        }
    }
}


void qd_agent_register_handlers(void *ctx,
                                qd_agent_t *agent,
                                int entity_type, // qd_schema_entity_type_t
                                qd_agent_handler_t create_handler,
                                qd_agent_handler_t read_handler,
                                qd_agent_handler_t update_handler,
                                qd_agent_handler_t delete_handler,
                                qd_agent_handler_t query_handler,
                                qd_agent_attribute_handler_t attribute_handler)
{
    qd_entity_type_handler_t *entity_handler = NEW(qd_entity_type_handler_t);

    entity_handler->ctx                   = ctx;
    entity_handler->entity_type           = entity_type;
    entity_handler->delete_handler        = delete_handler;
    entity_handler->update_handler        = update_handler;
    entity_handler->query_handler         = query_handler;
    entity_handler->create_handler        = create_handler;
    entity_handler->read_handler          = read_handler;
    entity_handler->attribute_handler     = attribute_handler;

    //Store the entity_handler in the appropriate cell of the handler array indexed by the enum qd_schema_entity_type_t
    agent->handlers[entity_type] = entity_handler;

}


static qd_parsed_field_t *qd_agent_get_request_body(qd_agent_request_t *request)
{
    if (request->in_body)
        return request->in_body;
    else {
        request->in_body = qd_get_parsed_field_by_index(request, QD_REQUEST_BODY);
        return request->in_body;
    }
}


char *qd_agent_request_get_string(qd_agent_request_t *request, int attr_id)
{
    qd_parsed_field_t *field    = qd_parse_value_by_int_key(qd_agent_get_request_body(request), attr_id);
    if (field) {
        qd_field_iterator_t *iter = qd_parse_raw(field);
        if (iter) {
            return (char*)qd_field_iterator_copy(iter);
        }
    }
    return 0;
}


int qd_agent_request_get_int(qd_agent_request_t *request, int attr_id)
{
    qd_parsed_field_t *field    = qd_parse_value_by_int_key(qd_agent_get_request_body(request), attr_id);
    if (field) {
        return qd_parse_as_int(field);
    }
    return 0;
}

long qd_agent_request_get_long(qd_agent_request_t *request, int attr_id)
{
    qd_parsed_field_t *field    = qd_parse_value_by_int_key(qd_agent_get_request_body(request), attr_id);
    if (field) {
        return qd_parse_as_long(field);
    }
    return 0;
}

bool qd_agent_request_get_bool(qd_agent_request_t *request, int attr_id)
{
    qd_parsed_field_t *field    = qd_parse_value_by_int_key(qd_agent_get_request_body(request), attr_id);
    if (field) {
        return qd_parse_as_bool(field);
    }
    return false;
}

void qd_agent_request_set_string(qd_agent_request_t *request, char *value)
{
    if (value)
        qd_compose_insert_string(request->out_body, value);
    else
        qd_compose_insert_null(request->out_body);
}

void qd_agent_request_set_null(qd_agent_request_t *request)
{
    qd_compose_insert_null(request->out_body);
}

void qd_agent_request_set_int(qd_agent_request_t *request, int value)
{
    if (value)
        qd_compose_insert_int(request->out_body, value);
    else
        qd_compose_insert_null(request->out_body);
}

void qd_agent_request_set_long(qd_agent_request_t *request, long value)
{
    if (value)
        qd_compose_insert_long(request->out_body, value);
    else
        qd_compose_insert_null(request->out_body);
}

void qd_agent_request_set_bool(qd_agent_request_t *request, bool value)
{
    if (value)
        qd_compose_insert_bool(request->out_body, value);
    else
        qd_compose_insert_null(request->out_body);
}

static qd_parsed_field_t *qd_get_parsed_field(qd_agent_request_t *request)
{
    if(!request->params) {
        qd_buffer_list_t *buffers = request->buffers;
        int buff_length = qd_buffer_list_length(buffers);
        qd_field_iterator_t *iter = qd_field_iterator_buffer(DEQ_HEAD(*buffers), 0, buff_length);
        //Store all the request parameters in request->params
        request->params = qd_parse(iter);
    }

    return request->params;
}

static qd_parsed_field_t *qd_get_parsed_field_by_index(qd_agent_request_t *request, qd_request_attributes_t field)
{
    return qd_parse_sub_value(qd_get_parsed_field(request), field);
}


qd_field_iterator_t *qd_agent_get_request_correlation_id(qd_agent_request_t *request)
{
    qd_parsed_field_t *correlation_id = qd_get_parsed_field_by_index(request, QD_REQUEST_CORRELATION_ID);
    if(correlation_id)
        return qd_parse_raw(correlation_id);
    return 0;
}

qd_field_iterator_t *qd_agent_get_request_reply_to(qd_agent_request_t *request)
{
    qd_parsed_field_t *reply_to = qd_get_parsed_field_by_index(request, QD_REQUEST_REPLY_TO);
    if(reply_to)
        return qd_parse_raw(reply_to);
    return 0;
}

qd_field_iterator_t *qd_agent_get_request_name(qd_agent_request_t *request)
{
    qd_parsed_field_t *name = qd_get_parsed_field_by_index(request, QD_REQUEST_NAME);
    if(name)
        return qd_parse_raw(name);
    return 0;
}

qd_field_iterator_t *qd_agent_get_request_identity(qd_agent_request_t *request)
{
    qd_parsed_field_t *identity = qd_get_parsed_field_by_index(request, QD_REQUEST_IDENTITY);
    if(identity)
        return qd_parse_raw(identity);
    return 0;
}

qd_agent_t *qd_agent_get_request_agent(qd_agent_request_t *request)
{
    return request->agent;
}

qd_buffer_list_t *qd_agent_get_request_buffers(qd_agent_request_t *request)
{
    return request->buffers;
}

int qd_agent_get_request_entity_type(qd_agent_request_t *request)
{
    return (int)request->entity_type;
}

int qd_agent_get_request_operation(qd_agent_request_t *request)
{
    return (int)request->operation;
}

int qd_agent_get_request_count(qd_agent_request_t *request)
{
    return request->count;
}

void qd_agent_set_router_core(qd_agent_t *agent, qdr_core_t *router_core)
{
    agent->router_core = router_core;
}

qdr_core_t *qd_agent_get_router_core(qd_agent_t *agent)
{
    return agent->router_core;
}

int qd_agent_get_request_offset(qd_agent_request_t *request)
{
    return request->offset;
}

qd_composed_field_t *qd_agent_get_response_body(qd_agent_request_t *request)
{
    return request->out_body;
}

void qd_agent_free(qd_agent_t *agent)
{

}

