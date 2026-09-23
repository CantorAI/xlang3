from typing import Literal, get_args

left = bool | Literal["all", "no_enums"]
right = Literal["all", "no_enums"] | None
combined = bool | Literal["all", "no_enums"] | None

for annotation in (left, right, combined):
    args = get_args(annotation)
    print(len(args), *(str(arg) for arg in args), sep="|")
print(type.__or__(int, 1))
