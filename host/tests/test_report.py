"""Tests of the three report formats (CONTRACT.md section 7).

The expected strings are the literal outputs of the formats: they must not
change without a change of the frozen contract.
"""

import json
import unittest

from toile.report import report_json, report_md, report_text
from toile.resource import Model, Page, Resource
from toile.scanout import Scanout
from toile.verdict import CMD_INVALID, OK, TRANSFER_OK, Verdict
from toile.vgproto import FMT_B8G8R8A8_UNORM

SOURCE = "fixtures/ref.tcl"

TEXT = (
    "toile v1 text\n"
    "0000 TOILE_OK                   info cmd=0x0101 resource=1\n"
    "0001 TOILE_TRANSFER_OK          info cmd=0x0105 resource=1 "
    "rect=0,0,4,2\n"
    "# state resources=1 scanouts=1 hash=76b2719bee918674 applied=3 "
    "rejected=0 flushes=0 bytes=0 cursor=0,0,0,0,0,0\n"
)

JSON = (
    '{"toile":"1","state":{"resources":1,"scanouts":1,'
    '"hash":"76b2719bee918674","applied":3,"rejected":0,"flushes":0,'
    '"bytes":0,"cursor":{"resource":0,"scanout":0,"x":0,"y":0,"hot_x":0,'
    '"hot_y":0,"visible":0}},"verdicts":[{"i":0,"code":"TOILE_OK",'
    '"sev":"info","cmd":"0x0101","resource":1},{"i":1,'
    '"code":"TOILE_TRANSFER_OK","sev":"info","cmd":"0x0105","resource":1,'
    '"rect":"0,0,4,2"}]}\n'
)

MD = (
    "# Rapport d'audit Toile\n"
    "\n"
    "- fixture : `fixtures/ref.tcl`\n"
    "- verdicts : 2 (erreurs 0, avertissements 0, info 2)\n"
    "- état : 1 ressources, 1 scanouts, hash `76b2719bee918674`\n"
    "\n"
    "| # | code | sévérité | cmd | ressource | scanout | rect | cause |\n"
    "|---|---|---|---|---|---|---|---|\n"
    "| 0000 | TOILE_OK | info | 0x0101 | 1 | – | – | – |\n"
    "| 0001 | TOILE_TRANSFER_OK | info | 0x0105 | 1 | – | 0,0,4,2 | – |\n"
)


def _model():
    """Reference state: one resource, one scanout, invisible cursor."""
    model = Model()
    model.resources[1] = Resource(id=1, fmt=FMT_B8G8R8A8_UNORM, width=4,
                                   height=2, pages=[Page(addr=0x1000, length=4096)])
    model.scanouts[0] = Scanout(id=0, resource_id=1, rect=(0, 0, 4, 2))
    model.cmds_applied = 3
    return model


def _verdicts():
    """Two fixed verdicts: one OK, one successful transfer."""
    return [Verdict(code=OK, cmd_index=0, cmd_type=0x0101, resource_id=1),
            Verdict(code=TRANSFER_OK, cmd_index=1, cmd_type=0x0105,
                    resource_id=1, rect=(0, 0, 4, 2))]


class ReportTest(unittest.TestCase):
    """The three formats, byte for byte (CONTRACT.md section 7)."""

    def test_text(self):
        self.assertEqual(report_text(_model(), _verdicts(), SOURCE), TEXT)

    def test_json(self):
        self.assertEqual(report_json(_model(), _verdicts(), SOURCE), JSON)

    def test_md(self):
        self.assertEqual(report_md(_model(), _verdicts(), SOURCE), MD)

    def test_json_omits_the_keys_that_do_not_apply(self):
        doc = json.loads(report_json(_model(), _verdicts(), SOURCE))
        first, second = doc["verdicts"]
        self.assertEqual(sorted(first), ["cmd", "code", "i", "resource", "sev"])
        self.assertEqual(sorted(second), ["cmd", "code", "i", "rect",
                                          "resource", "sev"])
        self.assertNotIn("scanout", first)
        self.assertNotIn("cause", first)
        self.assertNotIn("rect", first)
        self.assertEqual(doc["state"]["cursor"]["visible"], 0)

    def test_md_empty_cells_are_en_dashes(self):
        rows = report_md(_model(), _verdicts(), SOURCE).splitlines()
        self.assertEqual(rows[-2],
                         "| 0000 | TOILE_OK | info | 0x0101 | 1 | – | – | – |")
        self.assertEqual(rows[-1].count("\u2013"), 2)

    def test_hash_is_sixteen_lowercase_hex(self):
        digest = json.loads(report_json(_model(), _verdicts(), SOURCE))["state"]["hash"]
        self.assertEqual(len(digest), 16)
        self.assertTrue(all(ch in "0123456789abcdef" for ch in digest))

    def test_json_has_no_space_outside_the_strings(self):
        text = report_json(_model(), _verdicts(), SOURCE)
        self.assertEqual(text.count(" "), 0)
        self.assertEqual(json.dumps(json.loads(text), separators=(",", ":")), text[:-1])

    def test_text_payload_carries_the_cause_prefix(self):
        verdicts = [Verdict(code=CMD_INVALID, cmd_index=0, cmd_type=0x0301,
                            scanout_id=0, cause="cursor_not_visible")]
        text = report_text(_model(), verdicts, SOURCE)
        self.assertIn("cmd=0x0301 scanout=0 cause=cursor_not_visible", text)

    def test_reports_end_with_one_newline(self):
        for text in (report_text(_model(), _verdicts(), SOURCE),
                     report_json(_model(), _verdicts(), SOURCE),
                     report_md(_model(), _verdicts(), SOURCE)):
            self.assertTrue(text.endswith("\n"))
            self.assertFalse(text.endswith("\n\n"))
