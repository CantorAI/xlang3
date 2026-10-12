import multiprocessing
import time


def child_acquire(lock):
    # Use positional arguments here so this regression fixture isolates Windows
    # handle duplication and child-process sharing rather than keyword binding.
    if not lock.acquire(True, 10):
        raise RuntimeError("child could not acquire shared lock")
    lock.release()


if __name__ == "__main__":
    context = multiprocessing.get_context("spawn")
    lock = context.Lock()
    lock.acquire()
    child = context.Process(target=child_acquire, args=(lock,))
    child.start()
    time.sleep(0.1)
    lock.release()
    child.join(15)
    if child.is_alive():
        child.terminate()
        child.join()
        raise RuntimeError("spawned child did not finish")
    if child.exitcode != 0:
        raise RuntimeError(f"spawned child exit code: {child.exitcode}")
    print("multiprocessing-spawn-semlock-ok")
