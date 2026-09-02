import enum
import collections
import collections.abc
import concurrent.futures
import errno
import heapq
import itertools
import os
import socket
import stat
import subprocess
import threading
import time
import traceback
import sys
import warnings
import weakref

print("pre")
try:
    import ssl
except ImportError as e:
    print("ssl-caught", type(e).__name__)

print("enumdict", enum.EnumDict("X"))
print("prepare", enum.EnumType.__prepare__("_SendfileMode", (enum.Enum,)))
import asyncio.constants
print("constants-ok")
