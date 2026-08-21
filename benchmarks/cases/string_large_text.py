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

def main():
    f = open("benchmarks/data/large_text.txt")
    text = f.read()
    f.close()

    lines = text.split("\n")
    total = 0
    hits = 0
    i = 0
    while i < len(lines):
        line = lines[i].strip()
        if len(line) > 0:
            line = line.replace("ERROR", "WARN").replace("alpha", "ALPHA")
            parts = line.split("|")
            if len(parts) >= 4:
                msg = parts[3].strip()
                words = msg.replace(",", " ").replace(".", " ").split(" ")
                j = 0
                while j < len(words):
                    word = words[j].strip()
                    if len(word) > 3:
                        total = total + len(word)
                    j = j + 1
                joined = "-".join(parts)
                total = total + len(joined)
                if msg.startswith("ALPHA"):
                    hits = hits + 1
                if msg.startswith("beta"):
                    hits = hits + 3
        i = i + 1

    print(total + hits)


main()
