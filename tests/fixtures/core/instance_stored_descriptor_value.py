class Holder:
    def __init__(self):
        self.value = property(lambda owner: 42)


holder = Holder()
stored = vars(holder)["value"]
print(holder.value is stored, isinstance(holder.value, property))
