source = {'type': 'string', 'format': 'old'}
merge = lambda handler: {**handler(), 'format': 'path'}
print(merge(lambda: source))
print((lambda value: {**value})(source))
