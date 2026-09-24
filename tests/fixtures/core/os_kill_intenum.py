import os
import signal


try:
    os.kill(99999999, signal.SIGTERM)
except OSError:
    print("signal enum accepted")

try:
    os.kill("invalid", signal.SIGTERM)
except TypeError:
    print("invalid pid rejected")
