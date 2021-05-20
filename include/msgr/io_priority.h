#ifndef IO_PRIORITY_0db7c3de31894475b3afa259539605b6_H
#define IO_PRIORITY_0db7c3de31894475b3afa259539605b6_H

#include <string>

extern pid_t msgr_gettid();

#ifndef IOPRIO_WHO_PROCESS
# define IOPRIO_WHO_PROCESS 1
#endif
#ifndef IOPRIO_PRIO_VALUE
# define IOPRIO_CLASS_SHIFT 13
# define IOPRIO_PRIO_VALUE(class, data) \
		(((class) << IOPRIO_CLASS_SHIFT) | (data))
#endif
#ifndef IOPRIO_CLASS_RT
# define IOPRIO_CLASS_RT 1
#endif
#ifndef IOPRIO_CLASS_BE
# define IOPRIO_CLASS_BE 2
#endif
#ifndef IOPRIO_CLASS_IDLE
# define IOPRIO_CLASS_IDLE 3
#endif

extern int msgr_ioprio_set(int whence, int who, int ioprio);

extern int msgr_ioprio_string_to_class(const std::string& s);

#endif
