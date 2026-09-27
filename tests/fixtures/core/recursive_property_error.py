import sys
import threading


sys.setrecursionlimit(80)


class Recursive:
    @property
    def value(self):
        return self.value


def check():
    try:
        Recursive().value
    except RecursionError as error:
        print(type(error).__name__, str(error))


check()
thread = threading.Thread(target=check)
thread.start()
thread.join()
