"""Makes `import opticalnet` work from a source checkout (the built extension lives in <repo>/python/opticalnet)."""
import os
import sys
from pathlib import Path

# OPTICALNET_PYTHON_DIR selects another directory that contains the `opticalnet` package (e.g. a build tree).
PYTHON_DIR = Path(os.environ.get("OPTICALNET_PYTHON_DIR") or Path(__file__).resolve().parent.parent / "python")
if str(PYTHON_DIR) not in sys.path:
    sys.path.insert(0, str(PYTHON_DIR))

import opticalnet  # noqa: E402,F401  (fails with a clear ImportError if the extension has not been built)
