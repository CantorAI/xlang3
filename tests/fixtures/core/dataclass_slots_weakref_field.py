import dataclasses
import weakref


@dataclasses.dataclass(slots=True)
class TaskStateLike:
    key: int
    done: bool = False
    __weakref__: object = dataclasses.field(init=False)


state = TaskStateLike(7)
reference = weakref.ref(state)
print(
    "dataclass-slots-weakref-field",
    state.key,
    state.done,
    hasattr(state, "__dict__"),
    reference() is state,
    [field.name for field in dataclasses.fields(TaskStateLike)],
    [field.init for field in dataclasses.fields(TaskStateLike)],
)
