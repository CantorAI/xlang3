class Frame:
    def __init__(self, name):
        self.name = name


class Summary:
    def __init__(self, frames):
        self.frames = frames

    def format_frame_summary(self, frame_summary, *, colorize=False):
        return frame_summary.name, colorize

    def format(self):
        return [
            self.format_frame_summary(frame_summary, colorize=False)
            for frame_summary in self.frames
        ]


print(Summary([Frame("outer"), Frame("inner")]).format())
