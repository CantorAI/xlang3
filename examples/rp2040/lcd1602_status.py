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
import device
from lcd1602_driver import Lcd1602


def pause():
    i = 0
    total = 0
    while i < 25000:
        total = total + i
        i = i + 1
    return total


def show_page(lcd, line0, line1):
    lcd.line(0, line0)
    lcd.line(1, line1)


dev = device.connect()
i2c = dev.import_module("i2c")
bus = i2c.Bus(4, 5)
addresses = bus.scan()
address = 39
if len(addresses) > 0:
    address = addresses[0]

lcd = Lcd1602(bus, address)
lcd.clear()
info = dev.info()
show_page(lcd, "XLang3 RP2040", "I2C addr " + str(address))
pause()

show_page(lcd, "CPU " + info["cpu"], str(info["clock_hz"]) + " Hz")
pause()

i = 0
while i < 4:
    stats = dev.stats()
    uptime_s = stats["uptime_ms"] / 1000
    show_page(lcd, "Up " + str(uptime_s) + "s", "RPC " + str(stats["rpc_requests"]))
    pause()
    stats = dev.stats()
    show_page(lcd, "RAM files " + str(stats["ram_store_files"]), "RAM " + str(stats["ram_store_bytes"]) + " bytes")
    pause()
    i = i + 1

show_page(lcd, "Status demo done", "XLang3 device")
