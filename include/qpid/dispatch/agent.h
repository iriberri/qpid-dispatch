#ifndef __agent_h__
#define __agent_h__

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


typedef struct qd_agent_t qd_agent_t;

/**
 * Represents every request that is ever handled by the agent.
 */
typedef struct qd_agent_request_t qd_agent_request_t;

/**
 * Creates a new agent with the passed in address and whose configuration is located at config path.
 * Creates an AgentAdapter object and sticks the new agent reference into the adapter object.
 * @see qd_agent_start to start the agent
 */
qd_agent_t* qd_agent(qd_dispatch_t *qd, char *address, const char *config_path);

/**
 * Start the agent.
 */
qd_error_t qd_agent_start(qd_agent_t *agent);


/**
 * Free the agent and its components
 */
void qd_agent_free(qd_agent_t *agent);

/**
 * Writes a object to the response. Can be called multiple times if the response can contain several objects.
 * @param request - The request
 * @param object  - The object that needs to be written out
 */
void qd_agent_request_write_object(qd_agent_request_t *request, void *object);

/**
 * Called as a final step to indicate the response if ready to be sent out
 * @param ctx - context
 * @param status - The status of the response
 * @param request - The request
 */
void qd_agent_request_complete(void *ctx, qd_amqp_error_t *status, qd_agent_request_t *request);

typedef void (*qd_agent_handler_t) (void *context, qd_agent_request_t *request);

typedef void (*qd_agent_attribute_handler_t) (void *object, int attr_id, qd_agent_request_t *request);

/**
 * Register CRUDQ and response handlers for a particular entity type
 */
void qd_agent_register_handlers(void *ctx,
                                qd_agent_t *agent,
                                int entity_type, // qd_schema_entity_type_t
                                qd_agent_handler_t create_handler,
                                qd_agent_handler_t read_handler,
                                qd_agent_handler_t update_handler,
                                qd_agent_handler_t delete_handler,
                                qd_agent_handler_t query_handler,
                                qd_agent_attribute_handler_t attribute_handler);

/**
 * Getters for the components of a request (qd_agent_request_t).
 */
int qd_agent_get_request_entity_type(qd_agent_request_t *request);
int qd_agent_get_request_count(qd_agent_request_t *request);
int qd_agent_get_request_offset(qd_agent_request_t *request);
qd_field_iterator_t *qd_agent_get_request_correlation_id(qd_agent_request_t *request);
qd_field_iterator_t *qd_agent_get_request_reply_to(qd_agent_request_t *request);
qd_field_iterator_t *qd_agent_get_request_name(qd_agent_request_t *request);
qd_field_iterator_t *qd_agent_get_request_identity(qd_agent_request_t *request);

/**
 * Returns specific attr values based on the attr_id
 */
char *qd_agent_request_get_string(qd_agent_request_t *request, int attr_id);
long qd_agent_request_get_long(qd_agent_request_t *request, int attr_id);
bool qd_agent_request_get_bool(qd_agent_request_t *request, int attr_id);

/**
 * Sets attr values based on attr_id
 */
void qd_agent_request_set_string(qd_agent_request_t *request, char *value);
void qd_agent_request_set_long(qd_agent_request_t *request, long value);
void qd_agent_request_set_bool(qd_agent_request_t *request, bool value);

//This function is not part of the API, please ignore
void qdr_agent_set_router_core(qd_agent_t *agent, qdr_core_t *router_core);

#endif
