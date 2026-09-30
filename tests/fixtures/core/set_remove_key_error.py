for missing in (3, "absent"):
    try:
        {1, 2}.remove(missing)
    except KeyError as exc:
        print(type(exc).__name__, exc.args == (missing,), str(exc))
