#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <ctime>
#include <unistd.h>
#include <dirent.h>
#include <pwd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <signal.h>
#include <fcntl.h>

#include <mach-o/dyld.h>
#include <string.h>

using namespace std;

int main()
{
    int i = 0;
    while(i++<30)
    {
        cout<<"counting i = "<<i<<endl;
        system("sleep 1");
    }
}