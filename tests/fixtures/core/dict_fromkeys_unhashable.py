from typing import Literal


try:
    dict.fromkeys([[1]])
except TypeError as error:
    print(type(error).__name__, str(error))

print(Literal[['a', 1]].__args__)
