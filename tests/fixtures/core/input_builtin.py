import sys


class InputStream:
    def __init__(self, line):
        self.line = line

    def readline(self):
        return self.line


class OutputStream:
    def __init__(self):
        self.value = ""
        self.flushed = False

    def write(self, value):
        self.value += value

    def flush(self):
        self.flushed = True


old_stdin = sys.stdin
old_stdout = sys.stdout
capture = OutputStream()
sys.stdin = InputStream("answer\r\n")
sys.stdout = capture
answer = input("prompt> ")
sys.stdin = old_stdin
sys.stdout = old_stdout
print(answer, capture.value, capture.flushed)

sys.stdin = InputStream("")
try:
    input()
except EOFError as error:
    print(type(error).__name__, str(error))
finally:
    sys.stdin = old_stdin
