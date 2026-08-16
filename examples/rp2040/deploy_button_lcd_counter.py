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
LCD_ADDR = 39
BUTTON = 18

LCD_BACKLIGHT = 8
LCD_ENABLE = 4
LCD_CMD = 0
LCD_DATA = 1

def lcd_write_byte(value):
    i2c.write(SDA, SCL, BAUD, LCD_ADDR, [value])

def lcd_pulse(value):
    lcd_write_byte(value + LCD_ENABLE + LCD_BACKLIGHT)
    time.sleep_ms(1)
    lcd_write_byte(value + LCD_BACKLIGHT)
    time.sleep_ms(1)

def lcd_send(value, mode):
    high = value - (value % 16)
    low = (value % 16) * 16
    lcd_pulse(high + mode)
    lcd_pulse(low + mode)

def lcd_cmd(value):
    lcd_send(value, LCD_CMD)

def lcd_data(value):
    lcd_send(value, LCD_DATA)

def lcd_init():
    time.sleep_ms(50)
    lcd_pulse(48)
    time.sleep_ms(5)
    lcd_pulse(48)
    time.sleep_ms(1)
    lcd_pulse(48)
    lcd_pulse(32)
    lcd_cmd(40)
    lcd_cmd(12)
    lcd_cmd(6)
    lcd_cmd(1)
    time.sleep_ms(2)

def lcd_clear():
    lcd_cmd(1)
    time.sleep_ms(2)

def lcd_pos(row, col):
    if row == 0:
        lcd_cmd(128 + col)
    else:
        lcd_cmd(192 + col)

def lcd_text(row, text):
    lcd_pos(row, 0)
    text = text + "                "
    i = 0
    while i < 16:
        lcd_data(ord(text[i]))
        i = i + 1

def show_count(count, state):
    lcd_text(0, "GP18 pushes")
    lcd_text(1, "count " + str(count) + " " + state)

gpio.config(BUTTON, gpio.IN, gpio.PULL_UP)
lcd_init()
lcd_clear()
count = 0
show_count(count, "ready")

while True:
    event = gpio.wait_edge(BUTTON, gpio.FALLING, 60000)
    if event["triggered"]:
        count = count + 1
        show_count(count, "down")
        time.sleep_ms(120)
    else:
        show_count(count, "wait")
'''

dev = device.connect()
print(dev.info())
dev.put_data("/main.py", source, dev.flash)
print("deployed /main.py to flash")
print(dev.list_files("/", dev.flash))
print(dev.store_info(dev.flash))
print("reset the Pico to autorun the LCD button counter")
