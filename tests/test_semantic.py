import importlib.util
import json
import pathlib
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("semantic", pathlib.Path(__file__).parents[1] / "scripts/validate_semantics.py")
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)

class SemanticTests(unittest.TestCase):
    def test_strict_schema(self):
        with self.assertRaises(ValueError): m.validate_response({"grammatical": "yes"})
    def test_cache_and_immutable_source(self):
        result = {**{k: True for k in m.FIELDS}, "explanation": "fixture judgment"}
        with tempfile.TemporaryDirectory() as d, patch.object(m.Validator, "request", side_effect=[{"models": [{"name": "fixture", "digest": "sha256:fixture"}]}, {"response": json.dumps(result)}]) as request:
            v = m.Validator("http://127.0.0.1:11434", "fixture", 1, d)
            row = {"text": "the cat sleeps. the cat wakes.", "source_mode": "injection"}
            first = v.validate(row); second = v.validate(row)
            self.assertEqual(first, second); self.assertEqual(request.call_count, 2)
            self.assertEqual(first["source"], row)
    def test_failure_is_not_acceptance(self):
        with tempfile.TemporaryDirectory() as d, patch.object(m.Validator, "request", side_effect=TimeoutError):
            with self.assertRaises(TimeoutError): m.Validator("http://127.0.0.1:11434", "fixture", 1, d)

if __name__ == "__main__": unittest.main()
