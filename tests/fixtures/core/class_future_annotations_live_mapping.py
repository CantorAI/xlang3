from __future__ import annotations

enabled = False


class AnnotatedSlots:
    key: int
    value: list[str]
    __slots__ = tuple(__annotations__)


class ConditionalSlots:
    if enabled:
        skipped: str
    live: int
    __slots__ = tuple(__annotations__)


print(
    AnnotatedSlots.__slots__,
    AnnotatedSlots.__annotations__,
    ConditionalSlots.__slots__,
    ConditionalSlots.__annotations__,
)
