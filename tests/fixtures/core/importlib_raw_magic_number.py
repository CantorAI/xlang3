import importlib._bootstrap_external as bootstrap_external


print(
    hasattr(bootstrap_external, "_RAW_MAGIC_NUMBER")
)
