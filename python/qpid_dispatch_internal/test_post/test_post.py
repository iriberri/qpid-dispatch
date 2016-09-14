#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#

management_agent = None

class ManagementAgent:

    management_agent = None

    def __init__(self, address, agent_adapter, config_file):
        self.address = address
        self.agent_adapter = agent_adapter
        self.config_file = config_file # full path to config file
        ManagementAgent.management_agent = self

    def post_request(self,
                     operation_ordinality,
                     entity_type_ordinality,
                     cid=None,
                     name=None,
                     identity=None,
                     body=None,
                     reply_to=None,
                     count=0,
                     offset=0):
        """
        Calls the ssl-profile-test
        """
        print "In post_request *******************"
        print 'operation_ordinality ', operation_ordinality
        print 'entity_type_ordinality ', entity_type_ordinality
        print 'cid ', cid
        print 'body ', body
        print 'name ', name
        print 'identity ', identity
        self.agent_adapter.post_management_request(cid=cid,
                                                   reply_to=reply_to,
                                                   name=name,
                                                   identity=identity,
                                                   body=body,
                                                   operation=operation_ordinality,
                                                   entity_type=entity_type_ordinality,
                                                   count=count,
                                                   offset=offset)
