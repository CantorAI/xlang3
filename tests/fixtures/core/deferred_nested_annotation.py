import annotationlib


def outer():
    def inner():
        class Model:
            ann: List[Dict[str, str]]

        Dict = dict
        return Model

    List = list
    return inner()


model = outer()
try:
    annotations = annotationlib.get_annotations(
        model, format=annotationlib.Format.VALUE)
    print('value', annotations['ann'] == list[dict[str, str]])
except NameError as error:
    print('name-error', error.name)
print('captured', {'List', 'Dict'} <= set(model.__annotate__.__code__.co_freevars))
