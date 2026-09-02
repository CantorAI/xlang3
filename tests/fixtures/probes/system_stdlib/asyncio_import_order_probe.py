# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.

import enum


def check(label):
    class _SendfileMode(enum.Enum):
        UNSUPPORTED = enum.auto()
        TRY_NATIVE = enum.auto()
        FALLBACK = enum.auto()

    print("enum-after", label, _SendfileMode.FALLBACK.value)


check("start")

import collections
import collections.abc
check("collections")

import concurrent.futures
check("concurrent")

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
check("base-imports")
