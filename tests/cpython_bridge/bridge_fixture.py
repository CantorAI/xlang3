def echo(value):
    return value

def add(left, right=1):
    return left + right

def fail():
    raise ValueError("bridge fixture failure")

class Counter:
    def __init__(self, count=0):
        self.count = count

    def add(self, amount):
        self.count += amount
        return self.count

values = [10, 20, 30]
mapping = {"key": "value"}

def invoke_python(callback, value):
    return callback(value, offset=3)

def mutate_python(value):
    value.count += 5
    value.extra = "from XLang3"
    return value.count

def mutate_container(value):
    value["changed"] = value["original"] + 1
    return value["changed"]

def iterate_python(value):
    total = 0
    for item in value:
        total += item
    return total

def python_len(value):
    return len(value)

def catch_python_error(callback):
    try:
        callback()
    except ValueError:
        return "caught ValueError"
    return "not caught"

saved_python = None

def save_python(value):
    global saved_python
    saved_python = value

def call_saved(value):
    return saved_python(value)

def release_python():
    global saved_python
    saved_python = None

def construct_python(klass):
    return klass()

def python_truth(value):
    if value:
        return True
    return False

def delete_python_item(value, key):
    del value[key]

def delete_python_attr(value):
    del value.extra

def first_python(value):
    return value[0]

def python_boolean_ops(value):
    return [bool(value), not value, value and 17, value or 23]

def catch_python_truth(value):
    try:
        if value:
            return "true"
    except ValueError:
        return "caught truth error"
    return "false"
