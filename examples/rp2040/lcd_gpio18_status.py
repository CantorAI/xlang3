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
gpio = dev.import_module("gpio")
i2c = dev.import_module("i2c")
button = gpio.Pin(18, gpio.IN, gpio.PULL_UP)
bus = i2c.Bus(SDA, SCL, BAUD)

def pulse(value):
    bus.write(ADDR, [value + EN + BL, value + BL])

def send(value, mode):
    high = value - (value % 16)
    low = (value % 16) * 16
    bus.write(ADDR, [high + mode + EN + BL, high + mode + BL, low + mode + EN + BL, low + mode + BL])

def cmd(value):
    send(value, CMD)

def ch(value):
    send(value, DATA)

def line(row, value):
    if row == 0:
        cmd(128)
    else:
        cmd(192)
    text = value + "                "
    i = 0
    while i < 16:
        ch(ord(text[i]))
        i = i + 1

state = button.read()
line(0, "main.py running")
if state:
    line(1, "GP18 HIGH open")
else:
    line(1, "GP18 LOW press")
print(state)
