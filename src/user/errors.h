#ifndef KOS_ERRORS_H
#define KOS_ERRORS_H

/* ABI error descriptions shared by kernel and freestanding applications. */
static inline const char* kos_error_text(int error) {
    static const char* const errors[] = { "Success",
                                          "Not found",
                                          "Disk I/O error or filesystem unavailable",
                                          "Invalid path or argument (8.3 names required)",
                                          "Disk or directory full",
                                          "Already exists",
                                          "Not a directory",
                                          "Is a directory",
                                          "Directory not empty",
                                          "Read-only",
                                          "Corrupt filesystem",
                                          "Out of memory",
                                          "Resource busy",
                                          "Bad file descriptor or access mode",
                                          "Too many open files",
                                          "Broken pipe",
                                          "Not a seekable file",
                                          "Try again",
                                          "Not a child process",
                                          "Operation not permitted",
                                          "No such process" };
    if (error > 0 || error < -20) {
        return "Unknown error";
    }
    return errors[-error];
}
#endif
