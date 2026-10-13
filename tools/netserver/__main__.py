"""py -m tools.netserver [--port 6510]"""
import sys

from .server import main

sys.exit(main())
