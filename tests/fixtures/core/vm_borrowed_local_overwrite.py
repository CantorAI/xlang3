def exercise_local_overwrite():
    total = 0
    for number in range(64):
        payload = bytearray((number,))
        total += len(payload) + payload[0]
        payload = bytearray((number + 1,))
        total += payload[0]
    return total


print(exercise_local_overwrite())
