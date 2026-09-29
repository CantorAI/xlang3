class TaskState:
    def __init__(self):
        self.packet_pending = True
        self.task_waiting = False
        self.task_holding = False

    def isTaskHoldingOrWaiting(self):
        return self.task_holding or (not self.packet_pending and self.task_waiting)

    def isWaitingWithPacket(self):
        return self.packet_pending and self.task_waiting and not self.task_holding


state = TaskState()
print(state.isTaskHoldingOrWaiting(), state.isWaitingWithPacket())
state.packet_pending = False
state.task_waiting = True
print(state.isTaskHoldingOrWaiting(), state.isWaitingWithPacket())
state.task_holding = True
print(state.isTaskHoldingOrWaiting(), state.isWaitingWithPacket())


class Truthy:
    def __init__(self, result):
        self.result = result
        self.calls = 0

    def __bool__(self):
        self.calls += 1
        return self.result


# Cached inlining is only valid for exact bool slots. Reassignment to an
# arbitrary truthy value must keep Python's short-circuit return value and call
# its __bool__ exactly where ordinary evaluation would.
held = Truthy(True)
state.task_holding = held
print(state.isTaskHoldingOrWaiting() is held, held.calls)
pending = Truthy(True)
state.packet_pending = pending
state.task_waiting = True
state.task_holding = False
print(state.isWaitingWithPacket(), pending.calls)


class CustomLookup(TaskState):
    def __getattribute__(self, name):
        return object.__getattribute__(self, name)


custom = CustomLookup()
print(custom.isTaskHoldingOrWaiting(), custom.isWaitingWithPacket())
