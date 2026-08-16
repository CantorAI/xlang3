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
LCD_ADDR = 39
BL = 8
EN = 4

def w(value):
    i2c.write(SDA, SCL, BAUD, LCD_ADDR, [value])

def w4(value, mode):
    data = value + BL + mode
    w(data + EN)
    time.sleep_ms(1)
    w(data)
    time.sleep_ms(1)

def send(value, mode):
    high = value - (value % 16)
    low = (value % 16) * 16
    w4(high, mode)
    w4(low, mode)

def cmd(value):
    send(value, 0)

def ch(value):
    send(value, 1)

w4(48, 0)
w4(48, 0)
w4(48, 0)
w4(32, 0)
cmd(40)
cmd(8)
cmd(1)
cmd(6)
cmd(12)
cmd(128)
ch(88)
ch(76)
ch(97)
ch(110)
ch(103)
ch(51)
cmd(192)
ch(98)
ch(111)
ch(111)
ch(116)
print("embedded lcd wrote")
'''

dev = device.connect()
print(dev.info())
print(dev.exec(source))
