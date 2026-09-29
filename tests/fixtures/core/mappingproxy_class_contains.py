class ProtocolLike:
    run = None
    value = 1


namespace = ProtocolLike.__dict__
print("run" in namespace, "value" in namespace, "missing" in namespace)
