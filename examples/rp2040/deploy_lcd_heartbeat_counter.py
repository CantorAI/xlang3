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

source = '''
import i2c
import time

SDA = 4
SCL = 5
BAUD = 100000
ADDR = 39

BL = 8
EN = 4
CMD = 0
DATA = 1

def send(value, mode):
    high = value - (value % 16)
    low = (value % 16) * 16
    i2c.write(SDA, SCL, BAUD, ADDR, [high + mode + EN + BL])
    time.sleep_ms(1)
    i2c.write(SDA, SCL, BAUD, ADDR, [high + mode + BL])
    time.sleep_ms(1)
    i2c.write(SDA, SCL, BAUD, ADDR, [low + mode + EN + BL])
    time.sleep_ms(1)
    i2c.write(SDA, SCL, BAUD, ADDR, [low + mode + BL])
    time.sleep_ms(1)

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

def init_lcd():
    time.sleep_ms(80)
    i2c.write(SDA, SCL, BAUD, ADDR, [48 + EN + BL, 48 + BL])
    time.sleep_ms(5)
    i2c.write(SDA, SCL, BAUD, ADDR, [48 + EN + BL, 48 + BL])
    time.sleep_ms(1)
    i2c.write(SDA, SCL, BAUD, ADDR, [48 + EN + BL, 48 + BL])
    i2c.write(SDA, SCL, BAUD, ADDR, [32 + EN + BL, 32 + BL])
    cmd(40)
    cmd(8)
    cmd(1)
    time.sleep_ms(3)
    cmd(6)
    cmd(12)

init_lcd()
line(0, "main.py alive")

n = 0
while True:
    line(1, "tick " + str(n))
    n = n + 1
    time.sleep_ms(1000)
'''

dev = device.connect()
print(dev.info())
dev.put_data("/main.py", source, dev.flash)
print("deployed heartbeat /main.py")
print(dev.store_info(dev.flash))
print(dev.list_files("/", dev.flash))
