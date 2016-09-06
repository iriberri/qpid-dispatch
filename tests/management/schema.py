#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# 'License'); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# 'AS IS' BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#


#pylint: disable=wildcard-import,missing-docstring,too-many-public-methods

import unittest, json
from qpid_dispatch_internal.management.schema.qd_schema import Schema, BooleanType, EnumType, AttributeType, ValidationError, EnumValue, EntityType
from qpid_dispatch_internal.management.schema.deprecation import DeprecationHandler
from qpid_dispatch_internal.compat import OrderedDict

def replace_od(thing):
    """Replace OrderedDict with dict"""
    if isinstance(thing, OrderedDict):
        return dict((k, replace_od(v)) for k,v in thing.iteritems())
    if isinstance(thing, list):
        return [replace_od(t) for t in thing]
    return thing

SCHEMA_1 = {
    "prefix": "org.example",
    "entityTypes": {
        "container": {
            "deprecated": True,
            "singleton": True,
            "attributes": {
                "workerThreads": {"type": "integer", "default": 1},
                "routerId": {
                    "description":"(DEPRECATED) Router's unique identity. This attribute has been deprecated. Use id instead",
                    "type": "string",
                    "required": False,
                    "deprecated": True,
                    "create": True
                },
                "name": {"type": "string",
                         "required": True,
                         "unique": True},
                "type": {"type":"string",
                         "required": True}
            }
        },
        "listener": {
            "attributes": {
                "host": {"type": "string"},
                "name": {"type": "string", "required": True, "unique": True},
                "type": {"type":"string",  "required": True}
            }
        },
        "connector": {
            "attributes": {
                "host": {"type": "string"},
                "name": {"type": "string",
                         "required": True,
                         "unique": True},
                "type": {"type":"string",
                         "required": True}
            }
        }
    }
}

sections = [
    [u'container', {u'workerThreads': 2, u'containerName': 'Qpid.Dispatch.Router.A',
                    u'debugDump': '/home/user/test.txt', u'saslConfigPath': '/home/user',
                    u'saslConfigName': 'testsaslConfigName'}],
    [u'router', {u'mode': 'standalone', u'routerId': 'Router.A', u'mobileAddrMaxAge': 60}],
    [u'listener', {u'addr': '0.0.0.0', u'port': 34567, u'authenticatePeer': 'no', u'port': 'amqp',
                   u'allowNoSasl': 'yes', u'requirePeerAuth': 'yes', u'allowUnsecured': 'yes'}],
    [u'connector', {u'addr': '0.0.0.0', u'port': 34568, u'mobileAddrMaxAge': 60}],
    [u'linkRoutePattern', {u'prefix': 'org.apache', u'connector': 'connector-1', u'dir': 'both'}],
    [u'linkRoutePattern', {u'prefix': 'qpid.dispatch', u'connector': 'connector-2', u'dir': 'in'}],
    [u'linkRoutePattern', {u'prefix': 'test.dispatch', u'connector': 'connector-3', u'dir': 'out'}],
    [u'fixedAddress', {u'prefix': 'closest', u'fanout': 'single', u'bias': 'closest'}]
]


class SchemaTest(unittest.TestCase):

    def test_bool(self):
        b = BooleanType()
        self.assertTrue(b.validate('on'))
        self.assertTrue(b.validate(True))
        self.assertFalse(b.validate(False))
        self.assertFalse(b.validate('no'))
        self.assertRaises(ValidationError, b.validate, 'x')

    def test_enum(self):
        e = EnumType(['a', 'b', 'c'])
        self.assertEqual(e.validate('a'), 'a')
        self.assertEqual(e.validate(1), 'b')
        self.assertEqual(e.validate('c'), 2)
        self.assertEqual(e.validate(2), 2)
        self.assertRaises(ValidationError, e.validate, 'foo')
        self.assertRaises(ValidationError, e.validate, 3)

        self.assertEqual('["x"]', json.dumps([EnumValue('x',3)]))

    def test_attribute_def(self):
        a = AttributeType('foo', 'string', default='FOO')
        self.assertEqual('FOO', a.missing_value())
        self.assertEqual(a.validate('x'), 'x')

        a = AttributeType('foo', 'string', default='FOO', required=True)
        self.assertEqual('FOO', a.missing_value())

        a = AttributeType('foo', 'string', required=True)
        self.assertRaises(ValidationError, a.missing_value) # Missing required value.

        a = AttributeType('foo', 'string', value='FOO') # Fixed value
        self.assertEqual('FOO', a.missing_value())
        self.assertEqual(a.validate('FOO'), 'FOO')
        self.assertRaises(ValidationError, a.validate, 'XXX') # Bad fixed value

        self.assertRaises(ValidationError, AttributeType, 'foo', 'string', value='FOO', default='BAR') # Illegal

        a = AttributeType('foo', 'integer')
        self.assertEqual(3, a.validate(3))
        self.assertEqual(3, a.validate('3'))
        self.assertEqual(3, a.validate(3.0))
        self.assertRaises(ValidationError, a.validate, None)
        self.assertRaises(ValidationError, a.validate, "xxx")

    def test_entity_type(self):
        s = Schema()

        e = EntityType('MyEntity', s, attributes={
            'foo': {'type':'string', 'default':'FOO'},
            'req': {'type':'integer', 'required':True},
            'e': {'type':['x', 'y']}})

        e.init()
        self.assertRaises(ValidationError, e.validate, {}) # Missing required 'req'
        self.assertEqual(e.validate({'req':42}), {'foo': 'FOO', 'req': 42})

    def test_schema_validate(self):
        s = Schema(**SCHEMA_1)
        # Duplicate unique attribute 'name'
        m = [{'type': 'listener', 'name':'x'},
             {'type': 'listener', 'name':'x'}]
        self.assertRaises(ValidationError, s.validate_all, m)
        # Duplicate singleton entity 'container'
        m = [{'type': 'container', 'name':'x'},
             {'type': 'container', 'name':'y'}]
        self.assertRaises(ValidationError, s.validate_all, m)
        # Valid model
        m = [{'type': 'container', 'name':'x'},
             {'type': 'listener', 'name':'y'}]
        s.validate_all(m)

    def test_deprecation_handler(self):
        secs = DeprecationHandler(sections).process()
        self.assertEqual(secs,
                         [[u'router', {'debugDump': '/home/user/test.txt', 'workerThreads': 2,
                                       'saslConfigPath': '/home/user', u'mode': 'standalone',
                                       'saslConfigName': 'testsaslConfigName', 'mobileAddrMaxAge': 60, 'id': 'Router.A'}],
                          [u'listener', {'requireEncryption': 'yes', 'host': '0.0.0.0', u'authenticatePeer': 'yes', u'port': 'amqp'}],
                          [u'connector', {'host': '0.0.0.0', u'mobileAddrMaxAge': 60, u'port': 34568}],
                          [u'router.config.address', {'prefix': 'closest', 'distribution': 'closest'}],
                          [u'router.config.linkRoute', {'prefix': 'org.apache', 'connection': 'connector-1', 'dir': 'in'}],
                          [u'router.config.linkRoute', {'prefix': 'org.apache', 'connection': 'connector-1', 'dir': 'out'}],
                          [u'router.config.linkRoute', {'prefix': 'qpid.dispatch', 'connection': 'connector-2', 'dir': 'in'}],
                          [u'router.config.linkRoute', {'prefix': 'test.dispatch', 'connection': 'connector-3', 'dir': 'out'}]])

    def test_schema_entity(self):
        s = Schema(**SCHEMA_1)
        self.assertRaises(ValidationError, s.entity, {'type': 'nosuch'})
        self.assertRaises(ValidationError, s.entity, {'type': 'listener', 'nosuch': 'x'})
        e = s.entity({'host':'foo', 'type': 'listener', 'name': 'listener-1'})
        self.assertEqual(e.attributes, {'host': 'foo', 'name': 'listener-1', 'type': 'org.example.listener'})
        self.assertEqual(e['host'], 'foo')
        self.assertRaises(ValidationError, e.__setitem__, 'nosuch', 'x')

        # The container entity itself has been deprecated
        self.assertTrue(s.entity_types['org.example.container'].deprecated)

        # The routerId attribute of the container entity has been deprecated
        self.assertTrue(s.entity_types['org.example.container'].attributes['routerId'].deprecated)

        # This will make sure that deprecated flag defaults to false for entities
        self.assertFalse(s.entity_types['org.example.connector'].deprecated)
        
        # This will make sure that deprecated flag defaults to false for attributes of entities
        self.assertFalse(s.entity_types['org.example.listener'].attributes['host'].deprecated)

        try:
            e.nosuch = 'x'
            self.fail("Expected exception")
        except: pass

if __name__ == '__main__':
    unittest.main()
