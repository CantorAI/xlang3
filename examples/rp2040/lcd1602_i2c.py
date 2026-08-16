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


dev = device.connect()
print(dev.info())
i2c = dev.import_module("i2c")
bus = i2c.Bus(4, 5)
addresses = bus.scan()
print(addresses)
address = 39
if len(addresses) > 0:
    address = addresses[0]
lcd = Lcd1602(bus, address)
lcd.clear()
lcd.line(0, "Hello Shawn")
lcd.line(1, "RP2040 LCD")
