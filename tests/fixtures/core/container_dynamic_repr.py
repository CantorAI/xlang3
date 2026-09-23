class Item:
    def __repr__(self):
        return "dynamic-repr"

    def __hash__(self):
        return 7


item = Item()
print(repr([item]))
print(repr((item,)))
print(repr({item: item}))
print(repr({item}))
print(repr(frozenset({item})))

recursive_list = []
recursive_list.append(recursive_list)
print(repr(recursive_list))

recursive_dict = {}
recursive_dict["self"] = recursive_dict
print(repr(recursive_dict))
