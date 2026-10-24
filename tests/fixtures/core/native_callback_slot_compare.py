import heapq


class TimerLike:
    __slots__ = ("_when",)

    def __init__(self, when):
        self._when = when

    def __lt__(self, other):
        if isinstance(other, TimerLike):
            return self._when < other._when
        return NotImplemented


heap = [TimerLike(value) for value in (8.0, 2.0, 6.0, 1.0, 5.0, 3.0)]
heapq.heapify(heap)
print("native-callback-slot-compare", [item._when for item in heapq.nsmallest(6, heap)])
print("native-callback-noninstance", TimerLike.__lt__(heap[0], object()) is NotImplemented)

original_compare_code = TimerLike.__lt__.__code__


def always_true(self, other):
    return True


TimerLike.__lt__.__code__ = always_true.__code__
code_rebound_heap = [TimerLike(1.0)]
heapq.heappush(code_rebound_heap, TimerLike(2.0))
print("native-callback-code-rebind", code_rebound_heap[0]._when == 2.0)
TimerLike.__lt__.__code__ = original_compare_code

original_compare = TimerLike.__lt__


def always_true_compare(self, other):
    return True


TimerLike.__lt__ = always_true_compare
method_rebound_heap = [TimerLike(1.0)]
heapq.heappush(method_rebound_heap, TimerLike(2.0))
print("native-callback-method-rebind", method_rebound_heap[0]._when == 2.0)
TimerLike.__lt__ = original_compare

OriginalTimerLike = TimerLike


class ReplacementTimerLike:
    __slots__ = ("_when",)

    def __init__(self, when):
        self._when = when


TimerLike = ReplacementTimerLike
try:
    heapq.heappush([OriginalTimerLike(1.0)], OriginalTimerLike(2.0))
except TypeError:
    print("native-callback-global-rebind", "TypeError")
else:
    print("native-callback-global-rebind", "unexpected success")
TimerLike = OriginalTimerLike


class SubTimerLike(TimerLike):
    pass


sub_heap = [SubTimerLike(value) for value in (4.0, 1.0, 3.0)]
heapq.heapify(sub_heap)
print("native-callback-subclass", [item._when for item in heapq.nsmallest(3, sub_heap)])


descriptor_reads = []


class DescriptorTimerLike:
    __slots__ = ("_stored",)

    def __init__(self, when):
        self._stored = when

    @property
    def _when(self):
        descriptor_reads.append("read")
        return self._stored

    def __lt__(self, other):
        if isinstance(other, DescriptorTimerLike):
            return self._when < other._when
        return NotImplemented


descriptor_heap = [DescriptorTimerLike(value) for value in (3.0, 1.0, 2.0)]
heapq.heapify(descriptor_heap)
print("native-callback-descriptor", descriptor_reads.count("read") > 0)


saved_isinstance = isinstance
isinstance_calls = []


def isinstance(value, expected):
    isinstance_calls.append(expected)
    return False


try:
    heapq.heappush([TimerLike(1.0)], TimerLike(2.0))
except TypeError:
    print("native-callback-custom-isinstance", bool(isinstance_calls))
else:
    print("native-callback-custom-isinstance", "unexpected success")

isinstance = saved_isinstance
