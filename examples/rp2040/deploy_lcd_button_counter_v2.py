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
import gpio
import i2c
import time

SDA = 4
SCL = 5
BAUD = 100000
ADDR = 39
BUTTON = 18

BL = 8
EN = 4
CMD = 0
DATA = 1

def w(value):
    i2c.write(SDA, SCL, BAUD, ADDR, [value])

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

def write_text(value):
    i = 0
    while i < len(value):
        ch(ord(value[i]))
        i = i + 1

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
    pulse(48)
    time.sleep_ms(5)
    pulse(48)
    time.sleep_ms(1)
    pulse(48)
    pulse(32)
    cmd(40)
    cmd(8)
    cmd(1)
    time.sleep_ms(3)
    cmd(6)
    cmd(12)

def show(count, state):
    line(0, "XLang3 Pico")
    line(1, "push " + str(count) + " " + state)

gpio.config(BUTTON, gpio.IN, gpio.PULL_UP)
init_lcd()
show(0, "ready")

count = 0
while True:
    event = gpio.wait_edge(BUTTON, gpio.FALLING, 60000)
    if event["triggered"]:
        count = count + 1
        show(count, "down")
        time.sleep_ms(150)
    else:
        show(count, "wait")
'''

dev = device.connect()
print(dev.info())
dev.put_data("/main.py", source, dev.flash)
print("deployed /main.py")
print(dev.store_info(dev.flash))
print(dev.list_files("/", dev.flash))
