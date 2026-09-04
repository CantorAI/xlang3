import xlang1_compat_sample

print(xlang1_compat_sample.add(20, 22))
print(xlang1_compat_sample.make_list()[1])
print(xlang1_compat_sample.make_dict()["answer"])
print(xlang1_compat_sample.is_changed_event())

def on_changed(left, right):
    return left + right

cookie = xlang1_compat_sample.changed.subscribe(on_changed)
print(xlang1_compat_sample.fire_changed(30, 12))
print(xlang1_compat_sample.changed.fire(5, 7))
xlang1_compat_sample.changed.unsubscribe(cookie)
print(xlang1_compat_sample.fire_changed(1, 2))

counter = xlang1_compat_sample.Counter()
print(counter.add(5))
print(counter.add(7))
print(counter.total)
