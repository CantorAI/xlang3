values = [1, 2, 3]
mapping = {"a": 1, "b": 2}

class Object:
    def __init__(self):
        self.value = 3
    def __len__(self):
        return 2
    def __bool__(self):
        return False
    def __getitem__(self, key):
        return self.value + key
    def __setitem__(self, key, value):
        self.value = key + value
    def __delitem__(self, key):
        self.value = 0
    def __repr__(self):
        return "BridgeObject"
    def __str__(self):
        return "bridge object"

def generate():
    yield 3
    yield 4
