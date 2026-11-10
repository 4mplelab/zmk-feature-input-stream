"""Host verification of the firmware Noise backend against published vectors."""

import json
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).parent


class NoiseVectorTest(unittest.TestCase):
    def test_ik_reference_vector(self):
        compiler = shutil.which("cc")
        self.assertIsNotNone(compiler, "a host C compiler is required")
        vendor = ROOT / "vendor/noise-c"
        sources = re.findall(
            r"\$\{NOISE\}(/[^\s\)]+\.c)", (ROOT / "cmake/noise.cmake").read_text()
        )
        vector = json.loads((ROOT / "tests/host/ik-vector.json").read_text())
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)
            header = "\n".join(
                f'#define {k.upper()} "{v}"'
                for k, v in vector.items()
                if isinstance(v, str)
            )
            for field in ["payload", "ciphertext"]:
                header += (
                    f"\nstatic const char *{field}s[] = {{"
                    + ",".join(json.dumps(m[field]) for m in vector["messages"])
                    + "};\n"
                )
            (path / "vector.h").write_text(header)
            port = (ROOT / "src/security/noise_port.c").read_text()
            port = re.sub(r"#include <zephyr/[^>]+>\n", "", port)
            port = re.sub(
                r"if\s*\(sys_csrand_get\(out, len\)\)\s*\{\s*k_panic\(\);\s*\}",
                'FILE *f=fopen("/dev/urandom","rb"); if(!f || fread(out,1,len,f)!=len) abort(); fclose(f);',
                port,
            )
            (path / "port.c").write_text(
                "#include <stdio.h>\n#include <stdlib.h>\n" + port
            )
            command = (
                [
                    compiler,
                    "-O2",
                    "-I" + str(path),
                    "-I" + str(vendor / "include"),
                    "-I" + str(vendor / "src"),
                    "-I" + str(vendor / "src/protocol"),
                    str(ROOT / "tests/host/noise_vector.c"),
                    str(path / "port.c"),
                ]
                + [str(vendor / s.lstrip("/")) for s in sources]
                + ["-o", str(path / "vector")]
            )
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run(
                [str(path / "vector")], capture_output=True, text=True
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("PASS", result.stdout)
