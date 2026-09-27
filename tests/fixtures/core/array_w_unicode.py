import array


letters = array.array('w', 'Aé💡')
print('initial', letters.itemsize, [ord(item) for item in letters])
letters.append('中')
letters[1] = 'Z'
print('changed', [ord(item) for item in letters])

try:
    letters.append(1)
except Exception as exc:
    print('invalid', type(exc).__name__, str(exc))
