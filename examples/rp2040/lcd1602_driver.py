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


class Lcd1602:
    def __init__(self, bus, address):
        self.bus = bus
        self.address = address
        self.backlight = 8
        self.init()

    def _write4(self, value, mode):
        data = value + self.backlight + mode
        self.bus.write(self.address, [data + 4])
        self.bus.write(self.address, [data])

    def _send(self, value, mode):
        high = value - (value % 16)
        low = (value % 16) * 16
        self._write4(high, mode)
        self._write4(low, mode)

    def command(self, value):
        self._send(value, 0)

    def write_char(self, value):
        self._send(value, 1)

    def init(self):
        self._write4(48, 0)
        self._write4(48, 0)
        self._write4(48, 0)
        self._write4(32, 0)
        self.command(40)
        self.command(8)
        self.command(1)
        self.command(6)
        self.command(12)

    def clear(self):
        self.command(1)

    def set_cursor(self, col, row):
        offset = 0
        if row == 1:
            offset = 64
        self.command(128 + offset + col)

    def write(self, text):
        i = 0
        while i < len(text):
            self.write_char(ord(text[i]))
            i = i + 1

    def write_at(self, col, row, text):
        self.set_cursor(col, row)
        self.write(text)

    def line(self, row, text):
        self.set_cursor(0, row)
        i = 0
        while i < 16:
            if i < len(text):
                self.write_char(ord(text[i]))
            else:
                self.write_char(32)
            i = i + 1
