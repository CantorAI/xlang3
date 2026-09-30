import csv


for row, options in (
    ('a,"', {}),
    ('"a', {}),
    ('^', {'escapechar': '^'}),
):
    print(list(csv.reader([row], **options)))

for row, options in (
    ('a,"', {}),
    ('"a', {}),
    ('^', {'escapechar': '^'}),
):
    try:
        list(csv.reader([row], strict=True, **options))
    except csv.Error as exc:
        print(type(exc).__name__, str(exc))
