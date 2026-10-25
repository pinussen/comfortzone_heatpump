"""Reject schema mistakes before they can change a firmware register binding."""
import copy
import importlib.util
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
if not (ROOT/'registers_v170.json').exists():
    ROOT = ROOT/'components/comfortzone/third_party/comfortzone_heatpump'
spec = importlib.util.spec_from_file_location('generate_registers', ROOT/'tools/generate_registers.py')
generator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(generator)


class RegisterSchemaTest(unittest.TestCase):
    def setUp(self):
        self.schema = json.loads((ROOT/'registers_v170.json').read_text())
        self.status = (ROOT/'comfortzone_status.h').read_text()

    def test_generated_files_are_current(self):
        for name, data in generator.render(self.schema, self.status).items():
            self.assertEqual((ROOT/name).read_text(), data)

    def test_reject_ambiguous_or_unsafe_definition(self):
        mutations = [
            {'bank': 255}, {'address': 'ffff', 'type': 'u16'},
            {'type': 'float'}, {'access': 'write'}, {'divisor': 0},
            {'binding': 'not_a_status_field'}, {'raw_bounds': [10, 1]},
            {'raw_bounds': [0, 256]}, {'invalid_raw': [-1]},
            {'evidence': {'level': 'guess', 'reference': 'x', 'note': 'x'}},
        ]
        for mutation in mutations:
            with self.subTest(mutation=mutation):
                schema = copy.deepcopy(self.schema)
                schema['fields'][0].update(mutation)
                with self.assertRaises(ValueError):
                    generator.validate(schema, self.status)

    def test_reject_overlapping_bytes_and_duplicate_bindings(self):
        for key in ('address', 'binding', 'offline_name', 'id'):
            with self.subTest(key=key):
                schema = copy.deepcopy(self.schema)
                schema['fields'][1][key] = schema['fields'][0][key]
                with self.assertRaises(ValueError):
                    generator.validate(schema, self.status)


if __name__ == '__main__':
    unittest.main()
