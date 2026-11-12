"""Targeted unit tests for the Toile host model (resource + scanout)."""

import unittest

from toile import resource
from toile.resource import Model, Page
from toile.scanout import check_invariants, scanout_count, scanout_set
from toile.verdict import (
    BACKING_MISSING, CMD_INVALID, FLUSH_NO_SCANOUT, MODEL_COUNTEREXAMPLE, OK,
    RESOURCE_EXISTS, RESOURCE_MISSING, SCANOUT_INVALID, TRANSFER_OK,
)
from toile.vgproto import (
    CMD_RESOURCE_ATTACH_BACKING, CMD_RESOURCE_CREATE_2D, CMD_SET_SCANOUT,
    MAX_PAGES, MAX_RESOURCES, decode, encode)

FMT = 1  # B8G8R8A8_UNORM, 4 bytes per pixel
REFERENCE = ("toile-state-v1\nR 1 1 4 2 1\nP 0000000000001000 4096\n"
             "S 0 1 0 0 4 2 0\nC 0 0 0 0 0 0 0\nK 3 0 0 0\n")  # §8


def make_model() -> Model:
    """Reference model: resource 1 (4x2), one page, scanout 0."""
    model = Model()
    resource.resource_create(model, 1, FMT, 4, 2)
    resource.resource_attach_backing(model, 1, [Page(addr=0x1000, length=4096)])
    scanout_set(model, 0, 1, 0, 0, 4, 2)
    return model


class TestModel(unittest.TestCase):
    """Creation, backing, flush, transfers, unref, invariants, wire."""

    def test_create_and_refusals(self) -> None:
        model = Model()
        self.assertEqual(resource.resource_create(model, 1, FMT, 4, 2), OK)
        self.assertEqual(resource.resource_create(model, 1, FMT, 4, 2), RESOURCE_EXISTS)
        self.assertEqual(resource.resource_create(model, 2, 999, 4, 2), CMD_INVALID)
        self.assertEqual(model.last_cause, "bad_format")
        self.assertEqual(resource.resource_create(model, 2, FMT, 0, 2), CMD_INVALID)
        self.assertEqual(model.last_cause, "bad_geometry")
        self.assertEqual(model.resource_count(), 1)

    def test_table_full(self) -> None:
        model = Model()
        for rid in range(MAX_RESOURCES):
            self.assertEqual(resource.resource_create(model, rid + 1, FMT, 2, 2), OK)
        self.assertEqual(resource.resource_create(model, 999, FMT, 2, 2), CMD_INVALID)
        self.assertEqual(model.last_cause, "resource_table_full")
        self.assertEqual(model.resource_count(), MAX_RESOURCES)

    def test_backing(self) -> None:
        model = make_model()
        self.assertTrue(resource.resource_has_backing(model, 1))
        self.assertEqual(resource.resource_attach_backing(model, 1, []), CMD_INVALID)
        self.assertEqual(model.last_cause, "backing_entry_count")
        pages = [Page(0x1000 * i, 4096) for i in range(1, MAX_PAGES + 2)]
        self.assertEqual(resource.resource_attach_backing(model, 1, pages), CMD_INVALID)
        self.assertEqual(resource.resource_detach_backing(model, 1), OK)
        self.assertFalse(resource.resource_has_backing(model, 1))

    def test_flush(self) -> None:
        model = make_model()
        self.assertEqual(resource.resource_flush(model, 1), OK)
        self.assertEqual(model.flushes, 1)
        self.assertEqual(model.resources[1].flush_count, 1)
        scanout_set(model, 0, 0, 0, 0, 0, 0)
        self.assertEqual(resource.resource_flush(model, 1), FLUSH_NO_SCANOUT)
        self.assertEqual(model.flushes, 1)

    def test_transfers(self) -> None:
        model = make_model()
        self.assertEqual(resource.resource_transfer_to_host(model, 1), TRANSFER_OK)
        self.assertEqual(model.bytes_transferred, 4 * 2 * 4)
        self.assertEqual(
            resource.resource_transfer_to_host(model, 1, (0, 0, 2, 2)), TRANSFER_OK
        )
        self.assertEqual(model.bytes_transferred, 4 * 2 * 4 + 2 * 2 * 4)
        self.assertEqual(resource.resource_transfer_from_host(model, 1), OK)
        resource.resource_detach_backing(model, 1)
        self.assertEqual(resource.resource_transfer_to_host(model, 1), BACKING_MISSING)

    def test_unref(self) -> None:
        model = make_model()
        self.assertEqual(resource.resource_unref(model, 7), RESOURCE_MISSING)
        self.assertEqual(resource.resource_unref(model, 1), SCANOUT_INVALID)
        self.assertEqual(model.last_cause, "resource_referenced_by_scanout")
        scanout_set(model, 0, 0, 0, 0, 0, 0)
        self.assertEqual(resource.resource_unref(model, 1), OK)
        self.assertIsNone(resource.resource_find(model, 1))

    def test_invariants(self) -> None:
        self.assertEqual(check_invariants(make_model()), (0, ""))
        model = make_model()
        model.resources.pop(1)
        code, cause = check_invariants(model)
        self.assertEqual(code, MODEL_COUNTEREXAMPLE)
        self.assertEqual(cause, "invariant_i1")

    def test_scanout_guards(self) -> None:
        model = make_model()
        self.assertEqual(scanout_set(model, 16, 1, 0, 0, 4, 2), CMD_INVALID)
        self.assertEqual(model.last_cause, "scanout_id_out_of_range")
        self.assertEqual(scanout_set(model, 0, 42, 0, 0, 4, 2), SCANOUT_INVALID)
        self.assertEqual(scanout_set(model, 1, 1, 0, 0, 0, 2), CMD_INVALID)
        self.assertEqual(model.last_cause, "bad_geometry")
        self.assertEqual(scanout_count(model), 1)

    def test_refusal_keeps_state(self) -> None:
        model = make_model()
        before = model.serialize().split("K ")[0]
        resource.resource_create(model, 1, FMT, 4, 2)
        resource.resource_attach_backing(model, 1, [])
        resource.resource_transfer_to_host(model, 9)
        self.assertEqual(model.serialize().split("K ")[0], before)
        self.assertEqual(model.cmds_rejected, 3)
        self.assertEqual(model.cmds_applied, 3)

    def test_hash_is_deterministic(self) -> None:
        first, second = make_model(), make_model()
        self.assertEqual(first.serialize(), second.serialize())
        self.assertEqual(first.state_hash(), second.state_hash())
        resource.resource_create(second, 2, FMT, 2, 2)
        self.assertNotEqual(first.state_hash(), second.state_hash())

    def test_reference_state(self) -> None:
        model = make_model()
        self.assertEqual(model.cmds_applied, 3)
        self.assertEqual(model.cmds_rejected, 0)
        self.assertEqual(model.flushes, 0)
        self.assertEqual(model.bytes_transferred, 0)
        self.assertEqual(model.serialize(), REFERENCE)

    def test_round_trip(self) -> None:
        commands = [
            {"type": CMD_RESOURCE_CREATE_2D, "resource_id": 1, "format": FMT,
             "width": 4, "height": 2},
            {"type": CMD_SET_SCANOUT, "scanout_id": 0, "resource_id": 1,
             "rect": (0, 0, 4, 2)},
            {"type": CMD_RESOURCE_ATTACH_BACKING, "resource_id": 1,
             "pages": [{"addr": 0x1000, "length": 4096}]},
        ]
        for cmd in commands:
            decoded = decode(encode(cmd))
            for key, value in cmd.items():
                self.assertEqual(decoded[key], value)
    def test_invalid_buffer(self) -> None:
        with self.assertRaises(ValueError):
            decode(b"\x00\x01")
        with self.assertRaises(ValueError):
            decode(encode({"type": 0x0999}))
