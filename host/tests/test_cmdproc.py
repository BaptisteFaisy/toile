"""Tests of the fixture parser and of the command processor.

Contract: CONTRACT.md sections 4 to 6 (grammar, invariants, verdicts).
"""

import unittest

from toile.cmdproc import parse_line, run
from toile.resource import Model
from toile.verdict import (BACKING_MISSING, MODEL_CONSISTENT, MODEL_COUNTEREXAMPLE,
                           OK, RESOURCE_MISSING, TRANSFER_OK)
from toile.vgproto import (CMD_GET_DISPLAY_INFO, CMD_MOVE_CURSOR, CMD_RESOURCE_ATTACH_BACKING,
                           CMD_RESOURCE_CREATE_2D, CMD_RESOURCE_DETACH_BACKING, CMD_RESOURCE_FLUSH,
                           CMD_RESOURCE_UNREF, CMD_SET_SCANOUT, CMD_TRANSFER_FROM_HOST,
                           CMD_TRANSFER_TO_HOST_2D, CMD_UPDATE_CURSOR, ID_NONE)

BOOT = [
    "resource_create_2d id=1 format=B8G8R8A8_UNORM width=1280 height=800",
    "resource_attach_backing id=1 pages=0x1000:4096",
    "set_scanout scanout=0 id=1 x=0 y=0 width=1280 height=800",
    "transfer_to_host_2d id=1",
    "resource_flush id=1",
]

# Verdicts of the boot sequence: (code, index, type, resource, scanout, rect).
EXPECTED_BOOT = [
    (OK, 0, CMD_RESOURCE_CREATE_2D, 1, ID_NONE, (0, 0, 0, 0)),
    (OK, 1, CMD_RESOURCE_ATTACH_BACKING, 1, ID_NONE, (0, 0, 0, 0)),
    (OK, 2, CMD_SET_SCANOUT, 1, 0, (0, 0, 0, 0)),
    (TRANSFER_OK, 3, CMD_TRANSFER_TO_HOST_2D, 1, ID_NONE, (0, 0, 1280, 800)),
    (OK, 4, CMD_RESOURCE_FLUSH, 1, ID_NONE, (0, 0, 0, 0)),
    (MODEL_CONSISTENT, ID_NONE, ID_NONE, ID_NONE, ID_NONE, (0, 0, 0, 0)),
]


def _shape(v):
    """Comparable shape of a verdict."""
    return (v.code, v.cmd_index, v.cmd_type, v.resource_id, v.scanout_id, tuple(v.rect))


class ParseLineTest(unittest.TestCase):
    """The grammar of the fixtures (CONTRACT.md section 4)."""

    def test_one_line_per_command_type(self):
        lines = [
            ("get_display_info", CMD_GET_DISPLAY_INFO),
            ("resource_create_2d id=1 format=B8G8R8A8_UNORM width=4 height=2", CMD_RESOURCE_CREATE_2D),
            ("resource_unref id=1", CMD_RESOURCE_UNREF),
            ("set_scanout scanout=0 id=1 x=0 y=0 width=4 height=2", CMD_SET_SCANOUT),
            ("resource_flush id=1", CMD_RESOURCE_FLUSH),
            ("transfer_to_host_2d id=1", CMD_TRANSFER_TO_HOST_2D),
            ("resource_attach_backing id=1 pages=0x1000:4096", CMD_RESOURCE_ATTACH_BACKING),
            ("resource_detach_backing id=1", CMD_RESOURCE_DETACH_BACKING),
            ("transfer_from_host id=1", CMD_TRANSFER_FROM_HOST),
            ("update_cursor scanout=0 x=1 y=2", CMD_UPDATE_CURSOR),
            ("move_cursor scanout=0 x=1 y=2", CMD_MOVE_CURSOR),
        ]
        for line, ctype in lines:
            cmd, err = parse_line(line, 1)
            self.assertEqual(err, "", line)
            self.assertEqual(cmd["type"], ctype, line)

    def test_aliases(self):
        cmd, _ = parse_line("resource_create_2d resource=2 format=B8G8R8A8_UNORM w=4 h=2", 1)
        self.assertEqual(cmd["resource_id"], 2)
        self.assertEqual((cmd["width"], cmd["height"]), (4, 2))

    def test_values_and_rectangle(self):
        cmd, _ = parse_line("resource_flush id=1", 1)
        self.assertEqual(cmd["rect"], (0, 0, 0, 0))
        cmd, _ = parse_line("resource_flush id=1 x=2 y=3 width=4 height=5", 1)
        self.assertEqual(cmd["rect"], (2, 3, 4, 5))
        cmd, _ = parse_line("resource_attach_backing id=1 pages=0x1000:4096,0x2000:4096", 1)
        self.assertEqual(cmd["pages"], [{"addr": 0x1000, "length": 4096},
                                         {"addr": 0x2000, "length": 4096}])
        cmd, _ = parse_line("resource_attach_backing id=0x10 pages=0x1000:0x1000", 1)
        self.assertEqual(cmd["resource_id"], 16)
        self.assertEqual(cmd["pages"][0]["length"], 0x1000)

    def test_errors(self):
        cases = [
            ("resource_flush id=1 z=2", "1:unknown_key"),
            ("resource_flush id=abc", "1:bad_value"),
            ("resource_flush id=1 id=2", "1:duplicate_key"),
            ("resource_flush", "1:missing_required_key"),
            ("nope id=1", "1:unknown_command"),
        ]
        for line, err in cases:
            cmd, error = parse_line(line, 1)
            self.assertIsNone(cmd, line)
            self.assertEqual(error, err, line)

    def test_blank_and_comment(self):
        for line in ("", "   ", "# comment", "  # comment"):
            cmd, err = parse_line(line, 3)
            self.assertIsNone(cmd)
            self.assertEqual(err, "")


class RunTest(unittest.TestCase):
    """The processor of the model (CONTRACT.md sections 5 and 6)."""

    def _boot(self):
        """Model and verdicts of the boot sequence."""
        model = Model()
        cmds = [parse_line(line, index + 1)[0] for index, line in enumerate(BOOT)]
        return model, run(cmds, model)

    def test_boot_graphique(self):
        model, verdicts = self._boot()
        self.assertEqual([_shape(v) for v in verdicts], EXPECTED_BOOT)
        self.assertEqual(model.resource_count(), 1)
        self.assertEqual(model.cmds_applied, 5)
        self.assertEqual(model.cmds_rejected, 0)
        self.assertEqual(model.flushes, 1)
        self.assertEqual(model.bytes_transferred, 1280 * 800 * 4)
        self.assertEqual(tuple(verdicts[3].rect), (0, 0, 1280, 800))
        last = verdicts[-1]
        self.assertEqual(last.code, MODEL_CONSISTENT)
        self.assertEqual(last.cmd_index, ID_NONE)
        self.assertEqual(last.cmd_type, ID_NONE)
        self.assertEqual(last.cause, "")

    def test_two_rejections_leave_the_state_unchanged(self):
        model = Model()
        create = parse_line("resource_create_2d id=1 format=B8G8R8A8_UNORM "
                            "width=4 height=2", 1)[0]
        run([create], model)
        before = (model.resource_count(), model.cmds_applied, model.flushes, model.bytes_transferred)
        cmds = [parse_line("resource_unref id=7", 2)[0],
                parse_line("transfer_to_host_2d id=1", 3)[0]]
        verdicts = run(cmds, model)
        self.assertEqual([v.code for v in verdicts],
                         [RESOURCE_MISSING, BACKING_MISSING, MODEL_CONSISTENT])
        self.assertEqual((model.resource_count(), model.cmds_applied,
                          model.flushes, model.bytes_transferred), before)
        self.assertEqual(model.cmds_rejected, 2)

    def test_fail_fast_on_counterexample(self):
        model, _ = self._boot()
        model.resources[1].width = 0  # invariant I4 broken on purpose
        cmd = {"type": CMD_GET_DISPLAY_INFO, "flags": 0, "fence_id": 0}
        verdicts = run([cmd], model)
        self.assertEqual([v.code for v in verdicts], [OK, MODEL_COUNTEREXAMPLE])
        self.assertEqual(verdicts[1].cause, "invariant_i4")
        self.assertEqual(verdicts[1].cmd_index, 0)

    def test_empty_sequence_has_no_verdict(self):
        self.assertEqual(run([], Model()), [])
