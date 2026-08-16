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


dev = device.connect()
source = "print('hello from flash main')\n"

print("before", dev.store_info(dev.flash))
dev.put_data("/main.py", source, dev.flash)
print("root", dev.list_files("/", dev.flash))
print("ir dir", dev.list_files("/.xlang3/ir", dev.flash))
print("source", dev.get_data("/main.py", dev.flash))
print("after", dev.store_info(dev.flash))
print("reboot the board to auto-run /main.py from flash IR cache")
