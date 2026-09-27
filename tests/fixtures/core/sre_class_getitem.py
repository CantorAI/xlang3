import re


print(re.Pattern[str].__origin__ is re.Pattern)
print(re.Match[bytes].__origin__ is re.Match)
