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
typedef struct qd_agent_request_t qd_agent_request_t;

/**
 * Creates a new agent with the passed in address and whose configuration is located at config path.
 * Creates an AgentAdapter object and sticks the new agent reference into the adapter object.
 * @see qd_agent_start to start the agent
 */
qd_agent_t* qd_agent(qd_dispatch_t *qd, char *address, const char *config_path);

/**
 * Start the agent.
 * Loads the contents of the config file located in config_path
 * Agent starts listening on the provided address
 */
qd_error_t qd_agent_start(qd_agent_t *agent);


/**
 * Free the agent and its components
 */
void qd_agent_free(qd_agent_t *agent);

/**
 * Called on successful execution of an agent request.
 * @param ctx - the context required for successfully sending a response back to the caller
 * @param request - the request object
 * @param status - the status that will be sent back in the response
 * @param object - the object that was created if applicable.
 */
void qd_agent_request_success(qd_agent_request_t *request, void *object);

void qd_agent_request_complete(void *ctx, qd_amqp_error_t *status, qd_agent_request_t *request);

typedef void (*qd_agent_handler_t) (void *context, qd_agent_request_t *request);

typedef void (*qd_agent_attribute_handler_t) (void *object, int attr_id, qd_agent_request_t *request);

/**
 * Register CRUDQ and response handlers for a particular entity type
 */
// separate types for query and create
void qd_agent_register_handlers(qd_agent_t *agent,
                                void *ctx,
                                int entity_type, // qd_schema_entity_type_t
                                qd_agent_handler_t create_handler,
                                qd_agent_handler_t read_handler,
                                qd_agent_handler_t update_handler,
                                qd_agent_handler_t delete_handler,
                                qd_agent_handler_t query_handler,
                                qd_agent_attribute_handler_t attribute_handler);

/**
 * Functions that return the components of a request (qd_agent_request_t).
 */
int qd_agent_get_request_entity_type(qd_agent_request_t *request);
int qd_agent_get_request_count(qd_agent_request_t *request);
int qd_agent_get_request_offset(qd_agent_request_t *request);
qd_field_iterator_t *qd_agent_get_request_correlation_id(qd_agent_request_t *request);
qd_field_iterator_t *qd_agent_get_request_reply_to(qd_agent_request_t *request);
qd_field_iterator_t *qd_agent_get_request_name(qd_agent_request_t *request);
qd_field_iterator_t *qd_agent_get_request_identity(qd_agent_request_t *request);
qd_parsed_field_t *qd_agent_get_request_body(qd_agent_request_t *request);
qd_parsed_field_t *qd_agent_get_request_body(qd_agent_request_t *request);
qd_composed_field_t *qd_agent_get_response_body(qd_agent_request_t *request);
/**
 * Returns specific attr values based on the attr_id
 */
char *qd_agent_request_get_string(qd_agent_request_t *request, int attr_id);
long qd_agent_request_get_long(qd_agent_request_t *request, int attr_id);
bool qd_agent_request_get_bool(qd_agent_request_t *request, int attr_id);

/**
 * sets attr values based on attr_id
 */
void qd_agent_request_set_string(qd_agent_request_t *request, char *value);
void qd_agent_request_set_long(qd_agent_request_t *request, long value);
void qd_agent_request_set_bool(qd_agent_request_t *request, bool value);


#endif
