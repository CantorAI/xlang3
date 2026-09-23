import os
import stat
import tempfile


with tempfile.TemporaryDirectory() as directory:
    source = os.path.join(directory, "source.txt")
    link = os.path.join(directory, "link.txt")
    with open(source, "w") as stream:
        stream.write("hello")
    os.symlink(source, link)
    print(stat.S_ISREG(os.stat(link).st_mode), os.stat(link).st_size)
    print(stat.S_ISLNK(os.lstat(link).st_mode),
          stat.S_ISLNK(os.stat(link, follow_symlinks=False).st_mode))
    source_directory = os.path.join(directory, "source-directory")
    linked_directory = os.path.join(directory, "linked-directory")
    os.mkdir(source_directory)
    os.symlink(source_directory, linked_directory)
    print(os.path.isdir(linked_directory), stat.S_ISDIR(os.stat(linked_directory).st_mode))
