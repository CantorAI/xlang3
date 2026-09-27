import hashlib


class WheelLike:
    algorithm = hashlib.sha256

    def digest(self, data):
        return self.algorithm(data).hexdigest()


wheel = WheelLike()
print(wheel.digest(b'wheel-record'))
print(wheel.algorithm is WheelLike.algorithm)
