text = 'aé😀中b'
assembled = '[' + ','.join(['true'] * 4) + ']'
replaced = 'a-b-c'.replace('-', ':')
for source in ('abcdef', text, assembled, replaced):
    print(ascii(source[1:4]))
    print(ascii(source[4:1:-1]))
    print(ascii(source[::2]))
    print(ascii(source[::-1]))
    print(ascii(source[4:1]))
    print(ascii(source[20:30]))
    print(ascii(source[-20:-1]))
    print(ascii(source[-1]))
