#!/usr/bin/env python3
"""Unit test cho tools/layer_check.py: `python3 -m unittest tools/test_layer_check.py`."""
from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from layer_check import LayerChecker, strip_code  # noqa: E402

MATRIX = json.loads((Path(__file__).resolve().parent / "layer_matrix.json").read_text(encoding="utf-8"))


class LayerCheckTest(unittest.TestCase):
    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory()
        self.root = Path(self._tmp.name)

    def tearDown(self) -> None:
        self._tmp.cleanup()

    def write(self, rel: str, content: str) -> None:
        path = self.root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8")

    def rules(self) -> list[str]:
        _, violations = LayerChecker(self.root, MATRIX).run()
        return [v.rule for v in violations]

    def test_app_including_hal_is_rejected(self) -> None:
        self.write("src/apps/src/bad.cpp", '#include "hal_api/display.hpp"\n')
        self.assertEqual(self.rules(), ["apps -> hal_api"])

    def test_app_including_vendor_sdk_is_rejected(self) -> None:
        self.write("src/apps/src/bad.cpp", '#include "freertos/FreeRTOS.h"\n#include "esp_log.h"\n')
        self.assertEqual(self.rules(), ["apps -> vendor", "apps -> vendor"])

    def test_framework_public_header_must_not_leak_hal(self) -> None:
        self.write("src/framework/include/framework/x.hpp", '#include "hal_api/touch.hpp"\n')
        self.assertEqual(self.rules(), ["framework_public -> hal_api"])

    def test_framework_private_source_may_use_hal(self) -> None:
        self.write("src/framework/src/x.cpp", '#include "hal_api/touch.hpp"\n#include "lvgl.h"\n')
        self.assertEqual(self.rules(), [])

    def test_driver_including_upper_layer_is_rejected(self) -> None:
        self.write("src/drivers/src/x.cpp", '#include "framework/app.hpp"\n#include "driver/spi_master.h"\n')
        self.assertEqual(self.rules(), ["drivers -> framework", "drivers -> vendor"])

    def test_osal_port_may_use_rtos(self) -> None:
        self.write("src/osal/port/freertos/x.cpp", '#include "freertos/FreeRTOS.h"\n')
        self.assertEqual(self.rules(), [])

    def test_platform_may_include_anything(self) -> None:
        self.write("platform/esp32/main/main.cpp", '#include "esp_log.h"\n#include "apps/builtin_apps.hpp"\n')
        self.assertEqual(self.rules(), [])

    def test_local_private_header_counts_as_same_layer(self) -> None:
        self.write("src/apps/src/helper.hpp", "\n")
        self.write("src/apps/src/a.cpp", '#include "helper.hpp"\n')
        self.assertEqual(self.rules(), [])

    def test_banned_patterns(self) -> None:
        self.write("src/drivers/src/x.cpp",
                   "void f() { int* p = new int; delete p; char b[4]; sprintf(b, \"x\"); void* q = malloc(4); }\n")
        self.assertEqual(sorted(self.rules()),
                         ["banned:delete-expression", "banned:dynamic-alloc", "banned:new-expression",
                          "banned:unsafe-string"])

    def test_deleted_functions_and_comments_are_not_flagged(self) -> None:
        self.write("src/common/include/common/x.hpp",
                   "struct A { A(const A&) = delete; };\n// malloc(1) new Foo\n/* throw */ const char* s = \"free(x)\";\n")
        self.assertEqual(self.rules(), [])

    def test_strip_code_preserves_line_numbers(self) -> None:
        src = "a /* x\ny */ b // c\n\"s\"\n"
        self.assertEqual(strip_code(src, keep_strings=False).count("\n"), src.count("\n"))


if __name__ == "__main__":
    unittest.main()
