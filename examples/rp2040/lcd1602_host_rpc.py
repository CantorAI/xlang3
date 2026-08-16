#
# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

import device

SDA = 4
SCL = 5
BAUD = 100000
ADDR = 39
BL = 8
EN = 4
CMD = 0
DATA = 1

dev = device.connect()
print(dev.info())
i2c = dev.import_module("i2c")
bus = i2c.Bus(SDA, SCL, BAUD)
print(bus.scan())

def w(value):
    bus.write(ADDR, [value])

def pulse(value):
    w(value + EN + BL)
    w(value + BL)

def send(value, mode):
    high = value - (value % 16)
    low = (value % 16) * 16
    pulse(high + mode)
    pulse(low + mode)

def cmd(value):
    send(value, CMD)

def ch(value):
    send(value, DATA)

def text(value):
    i = 0
    while i < len(value):
        ch(ord(value[i]))
        i = i + 1

pulse(48)
pulse(48)
pulse(48)
pulse(32)
cmd(40)
cmd(8)
cmd(1)
cmd(6)
cmd(12)
cmd(128)
text("XLang3 RPC")
cmd(192)
text("LCD alive")
print("lcd rpc write done")
