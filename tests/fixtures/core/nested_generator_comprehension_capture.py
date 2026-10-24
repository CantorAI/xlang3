single_tokens = {"(", ")", "[", "]"}


def selected_keys():
    return list(
        key.upper()
        for key, value in {"SELECT": 1, "INSERT": 2, "(": 3}.items()
        if value in (4, 5, 6) or " " in key or any(single in key for single in single_tokens)
    )


print(selected_keys())
