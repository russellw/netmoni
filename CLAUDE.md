netmoni monitors the reliability and performance of the current Internet connection
It is specific to Windows
It is a command line program
Once started, it runs until killed
It prints the information it gathers to the terminal
And also echoes it to a log file
The log will be used later for statistical analysis so CSV is probably the best format
The program is written in C++
As far as the information that is gathered, to pick a concrete use case, let us say the intent is to evaluate the suitability of the connection for a Zoom video call, so the program should gather information that would allow determining whether a Zoom video call at that time would have failed or been degraded
At the same time, it should try to minimize the amount of bandwidth consumed in performing these tests
It will be run on mobile devices which sleep frequently so it should cope with this
It should use Windows specific APIs as appropriate to obtain as much specific information as possible about things like signal quality in the common case where the Internet connection is over Wi-fi
The program runs on Windows but you, Claude Code, are running on WSL, so you cannot run the program directly. You probably cannot even compile it, as it will contain Windows specific code. Instead, when you have written some code that is ready for testing, tell me, so that I can run it and then you can look at the log output
