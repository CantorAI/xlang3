import sys
import os
import subprocess
import copyreg
import types
sys.path.insert(0, sys.argv[1])
import xlang3


def family():
    events = []

    class Meta(type):
        @classmethod
        def __prepare__(meta, name, bases):
            events.append(("prepare", name))
            return {}

        def __new__(meta, name, bases, namespace):
            assert "marker" in namespace
            events.append(("new", name))
            return super().__new__(meta, name, bases, namespace)

        def __init__(cls, name, bases, namespace):
            events.append(("init", name))
            super().__init__(name, bases, namespace)

        def __call__(cls, value=40):
            return super().__call__(value)

    class Base(metaclass=Meta):
        marker = 1

        def __init_subclass__(cls, **kwargs):
            events.append(("subclass", cls.__name__))
            assert cls.marker == 2
            super().__init_subclass__(**kwargs)

        def answer(self):
            return 40

    class Derived(Base):
        marker = 2
        __slots__ = ("value",)

        def __init__(self, value):
            self.value = value

        def answer(self):
            return super().answer() + 2

    return Meta, Base, Derived, Derived(), events


source = family()
payload = xlang3.dumps(source)
meta, base, derived, instance, events = xlang3.loads(payload, trusted=True)
assert type(base) is meta and type(derived) is meta and type(instance) is derived
assert derived.__bases__ == (base,)
assert instance.answer() == derived().answer() == 42
assert events.count(("prepare", "Derived")) == 2
assert events.count(("new", "Derived")) == 2
assert events.count(("init", "Derived")) == 2
assert events.count(("subclass", "Derived")) == 2
environment = os.environ.copy()
environment["PYTHONPATH"] = sys.argv[1]
code = """import sys,xlang3
meta,base,derived,instance,events=xlang3.loads(sys.stdin.buffer.read(),trusted=True)
assert type(base) is meta and type(derived) is meta and type(instance) is derived
assert instance.answer()==derived().answer()==42
assert events.count(('subclass','Derived'))==2
print('fresh-process metaclass and subclass hooks PASS')
"""
result = subprocess.run([sys.executable, "-S", "-c", code], input=payload,
                        capture_output=True, env=environment, timeout=20)
assert result.returncode == 0, result.stderr.decode()
print(result.stdout.decode().strip())

def keyword_class():
    class Meta(type):
        def __new__(meta, name, bases, namespace, *, token):
            cls = super().__new__(meta, name, bases, namespace)
            cls.token = token
            return cls
    class Local(metaclass=Meta, token=42):
        pass
    def rebuild(name, token):
        return Meta(name, (), {}, token=token)
    copyreg.pickle(Meta, lambda cls: (rebuild, (cls.__name__, cls.token)))
    return Local

keyword = xlang3.loads(xlang3.dumps(keyword_class()), trusted=True)
assert keyword.token == 42 and keyword.__name__ == "Local"

# Two functions can share globals while retaining different builtin dictionaries.
globals_dict = {}
globals_dict["__builtins__"] = {"len": lambda x: 41}
first = types.FunctionType((lambda: len(())).__code__, globals_dict)
globals_dict["__builtins__"] = {"len": lambda x: 42}
second = types.FunctionType(first.__code__, globals_dict)
first, second = xlang3.loads(xlang3.dumps((first, second)), trusted=True)
assert first() == 41 and second() == 42
assert first.__globals__ is second.__globals__
print("Class restoration: metaclass identity, prepare/new/init/call, subclass hooks, super and slots PASS")
