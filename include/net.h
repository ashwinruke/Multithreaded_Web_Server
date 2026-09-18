#pragma once
#include <string>

// Creates a listening socket. reusePort enables SO_REUSEPORT so several
// reactor threads can each own a listener on the same port and let the kernel
// balance incoming connections between them.
int createListener(const std::string& ip, int port, bool reusePort, std::string& errorOut);
bool setNonBlocking(int fd);
