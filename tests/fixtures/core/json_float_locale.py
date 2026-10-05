import json
import locale

previous = locale.setlocale(locale.LC_NUMERIC)
try:
    # The available locales differ between CI hosts. Exercise a decimal-comma
    # locale when installed, alongside the portable C locale.
    locales = ["C"]
    for candidate in ("fr_FR.UTF-8", "de_DE.UTF-8", "French_France.1252"):
        try:
            locale.setlocale(locale.LC_NUMERIC, candidate)
        except locale.Error:
            continue
        locales.append(candidate)
        break
    for selected in locales:
        locale.setlocale(locale.LC_NUMERIC, selected)
        values = json.loads('[1.25, -0.0, 1e-3, 1e400, -1e400, 1e-400, -1e-400, 1.7976931348623157e308, 5e-324]')
        assert values[0] == 1.25
        assert str(values[1]) == "-0.0"
        assert values[2] == 0.001
        assert values[3] == float("inf")
        assert values[4] == float("-inf")
        assert values[5] == 0.0
        assert str(values[6]) == "-0.0"
        assert values[7] == 1.7976931348623157e308
        assert values[8] == 5e-324
finally:
    locale.setlocale(locale.LC_NUMERIC, previous)
print("JSON float locale and boundary cases pass")
