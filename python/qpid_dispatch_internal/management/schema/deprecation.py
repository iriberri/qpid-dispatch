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

import traceback
try:
    from qpid_dispatch_internal.dispatch import LOG_WARNING
except:
    LOG_WARNING = 10

class DeprecationHandler(object):
    def __init__(self, sections, log_adapter=None):
        self.sections = sections
        self.log_adapter = log_adapter
        self.dep_entities = ["container", "waypoint", "fixedAddress", "linkRoutePattern"]

        self.dep_attributes = {
            "router": {
                "routerId": "id",
                "mobileAddrMaxAge": "mobileAddrMaxAge"
            },

            "listener": {
                "addr": "host",
                "allowNoSasl": "authenticatePeer",
                "requirePeerAuth": "authenticatePeer",
                "allowUnsecured": "requireEncryption",
            },

            "connector": {
                "addr": "host",
            }
        }

    def log(self, level, text):
        info = traceback.extract_stack(limit=2)[0] # Caller frame info
        if self.log_adapter:
            self.log_adapter.log(level, text, info[0], info[1])

    def replace_dep_attributes(self):
        for section in self.sections:
            name = section[0]
            dep_attrs = self.dep_attributes.get(name)
            if dep_attrs:
                section_attrs = section[1]
                for key in section_attrs.keys():
                    if dep_attrs.get(key):
                        if key == "mobileAddrMaxAge":
                            self.log(LOG_WARNING, "'%s' attribute of '%s' is deprecated and is no longer "
                                                  "used in the router" % (key, name))
                        else:
                            self.log(LOG_WARNING, "'%s' attribute of '%s' is deprecated. Use '%s' instead" %
                                     (key, name, dep_attrs.get(key)))
                        section_attrs[dep_attrs.get(key)] = section_attrs.pop(key)

    def set_attr(self, attr, source_attr, target_attrs):
        s_val = source_attr.get(attr)
        if s_val:
            t_val = target_attrs.get(attr)
            if not t_val:
                target_attrs[attr] = s_val

    def handle_container(self):
        router_section = None
        router_dict = {}
        container_section = None
        for section in self.sections:
            if section[0] == u"router":
                router_section = section
                router_dict = section[1]
            elif section[0] == u"container":
                container_section = section

        if not router_dict:
            self.log(LOG_WARNING, "No 'router' configuration defined")

        if router_section:
            if container_section:
                self.log(LOG_WARNING, "'container' configuration is deprecated, switch to using 'router' instead.")
                router_attrs = router_section[1]
                container_attrs = container_section[1]
                self.set_attr("workerThreads", container_attrs, router_attrs)
                self.set_attr("debugDump", container_attrs, router_attrs)
                self.set_attr("saslConfigPath", container_attrs, router_attrs)
                self.set_attr("saslConfigName", container_attrs, router_attrs)
                self.sections.remove(container_section)
        else:
            # There should be a router section no matter what
            self.log(LOG_WARNING, "No 'router' configuration defined")

    def handle_waypoint(self):
        for section in self.sections:
            if section[0] == "waypoint":
                self.log(LOG_WARNING, "'waypoint' configuration is not supported, switch to using 'autoLink' instead")
                self.sections.remove(section)

    def handle_fixedAddress(self):
        sections_to_remove = []
        for section in self.sections:
            # There can be more than one fixedAddress
            if section[0] == "fixedAddress":
                self.log(LOG_WARNING, "'fixedAddress' configuration is deprecated, switch to using 'address' instead")
                faddr_attrs = section[1]
                address_section = []
                address_section.append(u"router.config.address")
                address_attrs = {}
                self.set_attr("prefix", faddr_attrs, address_attrs)

                # Convert fanout + bias to distribution
                fanout = faddr_attrs.get("faddr_attrs")
                bias = faddr_attrs.get("bias") if faddr_attrs.get("bias") else "closest"

                if fanout == "multiple":
                    distribution = "multicast"
                else:
                    if bias == "closest":
                        distribution = "closest"
                    else:
                        distribution = "balanced"

                address_attrs['distribution'] = distribution
                address_section.append(address_attrs)
                self.sections.append(address_section)
                sections_to_remove.append(section)

        for section in sections_to_remove:
            self.sections.remove(section)

    def handle_linkRoutePattern(self):
        sections_to_remove = []
        for section in self.sections:
            # There can be more than one linkRoutePattern
            if section[0] == "linkRoutePattern":
                lrp_attrs = section[1]
                link_route_section_1 = []
                link_route_section_2 = []
                link_route_section_1.append(u"router.config.linkRoute")
                link_route_attrs_1 = {}
                link_route_attrs_2 = {}

                self.set_attr("prefix", lrp_attrs, link_route_attrs_1)
                if lrp_attrs.get("connector"):
                    link_route_attrs_1['connection'] = lrp_attrs.get("connector")

                if lrp_attrs.get("dir") == "in" or lrp_attrs.get("dir") == "out":
                    if lrp_attrs.get("dir") == "in":
                        link_route_attrs_1['dir'] = "in"
                    elif lrp_attrs.get("dir") == "out":
                        link_route_attrs_1['dir'] = "out"
                elif lrp_attrs.get("dir") == "both" or lrp_attrs.get("dir") is None:
                    link_route_attrs_1['dir'] = "in"
                    self.set_attr("prefix", lrp_attrs, link_route_attrs_2)
                    link_route_section_2.append(u"router.config.linkRoute")
                    link_route_attrs_2['dir'] = "out"
                    if lrp_attrs.get("connector"):
                        link_route_attrs_2['connection'] = lrp_attrs.get("connector")

                link_route_section_1.append(link_route_attrs_1)
                if link_route_attrs_2:
                    link_route_section_2.append(link_route_attrs_2)

                self.sections.append(link_route_section_1)
                if link_route_section_2:
                    self.sections.append(link_route_section_2)
                sections_to_remove.append(section)

        for section in sections_to_remove:
            self.sections.remove(section)

    def replace_dep_entities(self):
        for ent in self.dep_entities:
            method = str("handle_" + ent)
            getattr(self, method)()

    def process(self):
        self.replace_dep_entities()
        self.replace_dep_attributes()
        return self.sections