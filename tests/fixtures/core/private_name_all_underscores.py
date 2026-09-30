class Plain:
    ___ = 3
    __secret = 4


class Slotted:
    __slots__ = ('___', '__secret')


print('___' in Plain.__dict__, '_Plain__secret' in Plain.__dict__)
print('___' in Slotted.__dict__, '_Slotted__secret' in Slotted.__dict__)
