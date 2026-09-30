import gc
import weakref

from fastapi import FastAPI
from fastapi.testclient import TestClient


class Parent:
    pass


app = FastAPI()


@app.get('/gc-class-cycle')
def gc_class_cycle():
    def create():
        class Child(Parent):
            pass

        Child.self = Child
        return weakref.ref(Child)

    child = create()

    def create_local_pair():
        class LocalParent:
            pass

        class LocalChild:
            pass

        LocalParent.self = LocalParent
        LocalParent.child = LocalChild
        LocalChild.self = LocalChild
        return weakref.ref(LocalChild)

    local_child = create_local_pair()
    gc.collect()
    return {
        'collected': child() is None,
        'local_parent_collected': local_child() is None,
    }


response = TestClient(app).get('/gc-class-cycle')
print(response.status_code, response.json())
