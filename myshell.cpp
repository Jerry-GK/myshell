//程序名：myshell
//作者、学号：管嘉瑞 3200102557

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
#include <sys/shm.h>
#include <signal.h>
#include <fcntl.h>

#include <mach-o/dyld.h>
#include <unordered_set>
#include <string.h>
#define MAXSIZE 1024
#define PARENTPATH "\\bin\\zsh"

using namespace std;
//进程状态
enum State
{
    /*
    (No actual "build-in" cmd except for "exit")
    cmd  --->  FG
    cmd &  --->  BG

    kill: BG -> KILLED
    suspend: BG -> SUSPENDED
    ctrl + c: FG -> KILLED 
    ctrl + z: FG -> SUSPENDED
    fg: SUSPENDED -> FG  /  BG -> FG
    bg: SUSPENDED -> BG

    FG / BG  ...--->  DONE / KILLED
    (At most one FG subprocess running by myshell)  
    */

    RUNNING_FG,//前台运行
    RUNNING_BG,//后台运行
    SUSPENDED,//挂起
    KILLED,//被强制终止
    DONE//正常结束
};

//进程类
class Process
{
public:
    Process(pid_t pid, State st, string name):pid_(pid),st_(st),cmd_line_(name){}
    string show_msg()
    {
        string st_str;
        if(st_==RUNNING_FG)
            st_str = "running(fg)";
        else if(st_==RUNNING_BG)
            st_str = "running(bg)";
        else if(st_==SUSPENDED)
            st_str = "suspended";
        else if(st_==KILLED)
            st_str = "killed";
        else if(st_==DONE)
            st_str = "done";
        return (string)"[" + to_string(pid_) + "] " + st_str + " " + cmd_line_ + "\n";
    }
    pid_t pid_;//进程pid
    State st_;//进程状态
    string cmd_line_;//处理后的指令字符串
};

//全局变量
char buffer[MAXSIZE] = {0};
string input;
string WD;//working directory当前工作目录
string HOMEDIR;//主目录
string SHELLPATH;//shell可执行程序的路径
string promt;//命令行提示符
string output;//输出内容
string error_msg;//错误信息输出
string cmd;//指令类型
vector<string> paras;//指令参数组
int para_num;//指令参数个数
int argc;
vector<string> argv;
vector<Process> pros;//进程组
unordered_set<pid_t> proc_last_end;
bool is_sub;//是否在子进程内的判断变量
/*
pointer to pid of fg process, 
-3: fg failure
-2: fg waiting
-1: no fg process
>=0: fg process pid
*/
pid_t* ptr_fg_pid;

//输入输出文件(可能被重定向)
int FILE_INPUT;
int FILE_OUTPUT;
int FILE_ERROR;

//函数声明
void Init(int argc_, char* argv_[]);
void Prepare();
void Pretreat(string input);
void Exec_cmd(string cmd, vector<string> para, bool bg);
void Exec(string input);
void erase_side_spaces(string &str);//删除字符串两侧的空格
string GetRealString(string txt);//对字符串进行处理，可能存在去印号、替换操作
pid_t get_fg_proc();//返回当前进程表中的前台子进程的pid，如果没有前台进程，返回-1
void set_pro_status(pid_t pid, State st);

//命令执行函数
void cmd_cd(vector<string>  dirs);
void cmd_pwd();
void cmd_clr();
void cmd_time();
void cmd_echo(vector<string> txts);
void cmd_dir(vector<string> dirs);
void cmd_set(vector<string> paras);
void cmd_unset(vector<string> vars);
void cmd_umask(vector<string> vals);
void cmd_ssleep(vector<string> sec);
void cmd_exec(string file, vector<string> paras);
void cmd_jobs(vector<string> jobs);
void cmd_kill(vector<string> jobs);
void cmd_suspend(vector<string> jobs);
void cmd_fg(vector<string> jobs);
void cmd_bg(vector<string> jobs);
void cmd_exit();

//信号处理函数
void sighandle_int(int sig);
void sighandle_tsp(int sig);
void sighandle_chld(int sig);

//main函数
int main(int Argc, char * Argv[])
{
    //信号处理
    signal(SIGINT,  sighandle_int); // ctrl-c, kill signal
    signal(SIGTSTP, sighandle_tsp); // ctrl-z, suspend signal
    signal(SIGCHLD, sighandle_chld); //status changed of subprocess

    //初始化
    Init(Argc, Argv);

    //共享内存，父子进程通信
    void *shm = NULL;//分配的共享内存的原始首地址
    int shmid;//共享内存标识符
	//创建共享内存
	shmid = shmget((key_t)1234, sizeof(pid_t), 0666|IPC_CREAT);
	if(shmid == -1)
	{
		fprintf(stderr, "shmget failed\n");
		exit(EXIT_FAILURE);
    }
    //将共享内存连接到当前进程的地址空间
	shm = shmat(shmid, 0, 0);
	if(shm == (void*)-1)
	{
		fprintf(stderr, "shmat failed\n");
		exit(EXIT_FAILURE);
    }
    ptr_fg_pid = (pid_t*) shm;
    //cout<<"1 is_sub = "<<is_sub<<": "<<ptr_fg_pid<<" value= "<<*ptr_fg_pid<<endl;

    //输入与处理的循环
    while(true)
    {   
        int i;
        Prepare();
        
        cout<<"\n\nmyshell > "<<WD<<" $ ";
        getline(cin,input);
        Exec(input);
        // write(FILE_OUTPUT, output.c_str(), output.length());
        // write(FILE_ERROR, error_msg.c_str(), error_msg.length());
    }
}

//其他函数体
void Init(int argc_, char* argv_[])
{
    //初始化环境变量
    WD = getenv("PWD");
    HOMEDIR = getenv("HOME");
    argc = argc_;
    for(int i=0;i<argc;i++)
    {
        argv.push_back(argv_[i]);
    }

    //获取程序自身的路径
    char buffer[MAXSIZE];
    uint32_t len = MAXSIZE;
    _NSGetExecutablePath(buffer, &len);
    // int len = readlink("/proc/self/exe", buffer, MAXSIZE);//获取自身位置
    //buffer[len] = '\0';

    // memcpy(buffer, argv[0].c_str(),argv[0].size()+1);
    setenv("SHELL", buffer, 1);
    SHELLPATH = buffer;
    //设置父进程的路径
    setenv("PARENT", PARENTPATH, 1);

    is_sub = false;
    ptr_fg_pid = NULL;
}

void Prepare()
{
    input = "";
    output = "";
    error_msg = "";
    cmd = "";
    paras.clear();
    para_num = 0;
    //设置父进程
    setenv("PARENT", PARENTPATH, 1);

    //从可能的重定向中恢复
    FILE_INPUT = STDIN_FILENO;
    FILE_OUTPUT = STDOUT_FILENO;
    FILE_ERROR = STDERR_FILENO;
}

void Pretreat(string input)
{
    //去除输入两侧的空格
    if(input.empty())
    {
        return;
    }
    erase_side_spaces(input);
    //按空格分割获取每个单词，第一个单词是指令名，后续都是参数名
    stringstream stringstr(input);//   使用串流实现对string的输入输出操作
    string temp;
    int words_num = 0;
    while(stringstr >> temp) //  依次分解为每个单词
    {
        if(words_num==0)
            cmd = temp;
        else
            paras.push_back(temp);
        words_num++;
    }
    para_num = paras.size();

    // if(input.find(' ')!=-1)
    // {
    //     cmd = input.substr(0, input.find(' '));
    //     paras = input.substr(input.find(' ')+1, input.length()-input.find(' ')-1);
    // }
    // else
    // {
    //     cmd = input;
    // }
    erase_side_spaces(cmd);
}

void Exec(string input)
{
    Pretreat(input);
    if(cmd=="exit")
    {
        cmd_exit();//exit without forking sub process
    }
    pid_t pid = fork();
    bool last_bg = (para_num>=1 && paras[para_num-1]=="&");//last_bg表示指令是否以&结尾
    bool is_fg = (cmd=="fg");
    bool bg = last_bg || is_fg;//fg强制前台执行
    //生成处理后的指令字符串
    string cmd_line = cmd;
    for(int i=0;i<para_num;i++)
    {
        if(last_bg && i==para_num-1)
            break;
        cmd_line += " "+ paras[i];
    }

    if(pid)//父进程
    {
        Process subpro(pid, bg?RUNNING_BG:RUNNING_FG, cmd_line);
        if(bg==false)//前台运行
        {
            *ptr_fg_pid = pid;
        }
        else if(is_fg)
        {
            *ptr_fg_pid = -2;
        }
        pros.push_back(subpro);//新增进程表
        setenv("PARENT", SHELLPATH.c_str(), 1);//设置环境变量 
        //进程表会在signal处理函数中自动更新
        // bool first = true;
        // for(int i=0;i<pros.size();i++)
        // {
        //     if(!first)
        //     {
        //         cout<< "\n";
        //         first = false;
        //     }
        //     cout << pros[i].show_msg();
        // }
    }
    else//子进程
    {
        is_sub = true;
        setpgid(0, 0);//使子进程单独成为一个进程组。后台进程组自动忽略Ctrl+Z、Ctrl+C等信号
        if(last_bg)//末尾有&，后台运行子进程
        {
            paras.erase(paras.end()-1);
            para_num--;
            Exec_cmd(cmd, paras, true);   
        }
        else
        {
            Exec_cmd(cmd, paras, false);//前台运行子进程
        }
        //子进程执行完毕，返回父进程
        exit(0);
    }
    //cout<<"2 is_sub ="<<is_sub<<"cmd = "<<cmd<<" fgpid = "<<*ptr_fg_pid<<endl;
    while(true)//wait for fg complete
    {
        if(*ptr_fg_pid!=-2)
            break;
    }
    if(*ptr_fg_pid==-3)//fg指令执行失败，没有进程被提到前台
    {
        *ptr_fg_pid = -1;
        return;
    }
    for(int i=0;i<pros.size();i++)
    {
        if(pros[i].pid_==*ptr_fg_pid && pros[i].st_!=RUNNING_FG)
            pros[i].st_ = RUNNING_FG;
    }
    while(true)
    {
        //proc_last_end是全局变量，只在SIFCHLD产生时可能添加状态改变的pid，然后在这里马上清空，跳出等待循环
        if(*ptr_fg_pid==-1)
            break;
    }
    // while(true)
    // {
    //     //proc_last_end是全局变量，只在SIFCHLD产生时可能添加状态改变的pid，然后在这里马上清空，跳出等待循环
    //     if(proc_last_end.find(pid)!=proc_last_end.end())
    //     {
    //         proc_last_end.clear();
    //         break;
    //     }
    // }
    // pid = -1;
    // *ptr_fg_pid = -1;
}

void Exec_cmd(string cmd, vector<string> paras, bool bg)
{
    //cout<<" is sub = "<<is_sub<<" exec: cmd = "<<cmd<<endl;

    if(cmd=="cd")
    {
        cmd_cd(paras);
    }
    else if(cmd=="pwd")
    {
        cmd_pwd();
    }
    else if(cmd=="clr")
    {
        cmd_clr();
    }
    else if(cmd=="time")
    {
        cmd_time();
    }
    else if(cmd=="echo")
    {
        cmd_echo(paras);
    }
    else if(cmd=="exit")
    {
        cmd_exit();
    }
    else if(cmd=="dir")
    {
        cmd_dir(paras);
    }
    else if(cmd=="set")
    {
        cmd_set(paras);
    }
    else if(cmd=="unset")
    {
        cmd_unset(paras);
    }
    else if(cmd=="umask")
    {
        cmd_umask(paras);
    }
    else if(cmd=="ssleep")
    {
        cmd_ssleep(paras);
    }
    else if(cmd=="jobs")
    {
        cmd_jobs(paras);
    }
    else if(cmd=="kill")
    {
        cmd_kill(paras);
    }
    else if(cmd=="suspend")
    {
        cmd_suspend(paras);
    }
    else if(cmd=="fg")
    {
        cmd_fg(paras);
    }
    else if(cmd=="bg")
    {
        cmd_bg(paras);
    }
    else if(cmd=="")
    {
        output = "";
    }
    //exec，或其他指令，程序调用(相当于前面加了exec)。
    //如果此时是父进程（sub=false），则生成子进程并前台运行。
    //如果是子进程（sub=true），说明末尾加了&，直接后台运行即可
    else
    {
        if(cmd=="exec")
        {
            //paras的第一个元素是执行的文件名
            //exec file para1 para2
            if(para_num==0)
            {
                error_msg = "[Error] exec: no input file";
                return;
            }
            //参数从第二个开始
            cmd = paras[0];
            paras.erase(paras.begin());
            para_num--;
        }

        if(bg == false)//前台运行
        {
            cmd_exec(cmd, paras);
        }
        else if(bg == true)//后台运行
        {
            cmd_exec(cmd, paras);
        }
    }
    write(FILE_OUTPUT, output.c_str(), output.length());
    write(FILE_ERROR, error_msg.c_str(), error_msg.length());
}

void cmd_cd(vector<string>  dirs)
{
    if(para_num!=1)
    {
        error_msg = "[Error] cd: No argument or too many arguments";
        return;
    }
    string dir = dirs[0];
    dir = GetRealString(dir);
    if(dir[0]=='~')//主目录字符替换
    {
        dir.erase(dir.begin());
        dir = HOMEDIR + dir;
    }
    if(chdir(dir.c_str())==-1)
    {
        //chdir调用失败
        error_msg = "[Error] cd: Unable to change directory to " + dir;
    }
    else
    {
        //chdir调用成功
        char buffer[MAXSIZE];
        getcwd(buffer, MAXSIZE);//获取程序当前工作路径
        WD = buffer;
        //更新WD环境变量
        setenv("PWD", buffer, 1);
    }
}

void cmd_pwd()
{
    output = WD;
}

void cmd_clr()
{
    system("clear");
}

void cmd_time()
{
    time_t tt = time(NULL);
    struct tm * t = localtime(&tt);
    //用stringstream生成返回信息
    string Week[] = {"星期一", "星期二", "星期三", "星期四", "星期五", "星期六", "星期日"};
    output = to_string(t->tm_year + 1900) + "年" + to_string(t->tm_mon + 1) + "月" 
    + to_string(t->tm_mday) + "日"+ " " + Week[t->tm_wday]
    + " " + to_string(t->tm_hour) + "时" + to_string(t->tm_min) + "分" + to_string(t->tm_sec) + "秒";
}

void cmd_echo(vector<string> txts)
{
    for(int i=0;i<para_num;i++)
    {
        output += GetRealString(txts[i]);
        if(i!=para_num-1)
            output += " ";
    }
}

void cmd_dir(vector<string> dirs)
{
    //无参数默认显示工作目录内容
    if(para_num==0)
    {
        paras.push_back(".");
        dirs=paras;
        para_num = 1;
    }
    for(int i=0;i<para_num;i++)
    {
        //获取真实路径并显示
        string dir = GetRealString(dirs[i]);
        output += dir + ":\n";
        if(dir[0]=='~')//主目录字符替换
        {
            dir.erase(dir.begin());
            dir = HOMEDIR + dir;
        }
        //获取每个文件的名字
        vector<string> files;
        DIR* p_cur_dir;//当前目录的指针
        struct dirent* ptr;
        if(!(p_cur_dir = opendir(dir.c_str())))
        {
            output = "Can not open directory \"" + dir + "\"\n";
        }
        else//输出目录下的文件名
        {
            while((ptr = readdir(p_cur_dir)) != NULL)
            {
                files.push_back(ptr->d_name);
            }
            closedir(p_cur_dir);
            for(int i=0;i<files.size();i++)
            {
                if(files[i]=="." || files[i]=="..")//不输出 . 和 .. 
                    continue;
                output += files[i];
                if(i!=files.size()-1)
                    output += "\n";
            }
            if(i!=para_num-1)
                output += "\n";
        }
    }
}

void cmd_set(vector<string> paras)
{
    extern char** environ;//获取环境变量表
    //无参数显示全部环境变量
    if(para_num==0)
    {
        for(int i=0;environ[i]!=NULL;i++)
        {
            if(i!=0)
                output += "\n";
            output += (string)environ[i];
        }
    }
    //一个参数，将该参数解释为环境变量名并显示
    else if(para_num==1)
    {
        if(getenv(paras[0].c_str())==NULL)
            output += "No environment variable named \"" + paras[0] + "\"";
        else
            output += paras[0] + "=" + getenv(paras[0].c_str());
    }
    //两个参数，set var val，设置环境变量的值。如果var不存在则新建
    else if(para_num==2)
    {
        if(getenv(paras[0].c_str())==NULL)//创建新环境变量
        {    
            string env = paras[0] + "=" + GetRealString(paras[1]).c_str();
            char buffer[MAXSIZE];
            int i;
            for(i=0;i<env.length();i++)
                buffer[i] = env[i];
            buffer[i] = '\0';
            putenv(buffer);
            output += (string)"create: " + paras[0] + "=" + getenv(paras[0].c_str());
        }
        else//修改已存在的环境变量
        {
            setenv(paras[0].c_str(), GetRealString(paras[1]).c_str(), 1);
            output += (string)"modify: " + paras[0] + "=" + getenv(paras[0].c_str());
        }
    }
    else
    {
        error_msg = "[Error] set: too many arguments!";
    }
}

void cmd_unset(vector<string> vars)
{
    //依次删除每个环境变量
    if(para_num==0)
    {
        error_msg = "[Error] unset: not enough arguments";
        return;
    }
    for(int i=0;i<para_num;i++)
    {
        if(getenv(vars[i].c_str())==NULL)
        {
            output += "No environment variable named \"" + vars[i] + "\"\n";
            continue;
        }
        unsetenv(vars[i].c_str());
        output += "unset " + vars[i] + "\n";
    }
}

void cmd_umask(vector<string> vals)
{
    //无参数，直接输出umask值
    if(para_num==0)
    {
        mode_t mode = umask(0);//mode接受umask的值，umask被改成0
        umask(mode);//恢复umask的值
        //以8进制的形式输出umask
        output = to_string((mode >> 9) & 7) + to_string((mode >> 6) & 7)
                 + to_string((mode >> 3) & 7) + to_string(mode & 7);
    }
    //一个参数，设置值
    else if(para_num==1)
    {
        string umask_str = paras[0];
        if(umask_str.length()>=5)
        {
            error_msg = "[Error] umask: too many digits for umask";
            return;
        }
        char* temp = NULL;
        mode_t mode = strtol(umask_str.c_str(), &temp, 8);
        if(*temp!='\0')//输入的umask值中出现了非0～7的字符
        {
            error_msg = "[Error] umask: invalid umask expression";
            return;
        }
        umask(mode);
        output = "set: umask=" + to_string((mode >> 9) & 7) + to_string((mode >> 6) & 7)
            + to_string((mode >> 3) & 7) + to_string(mode & 7);
    }
    else
    {
        error_msg = "[Error] umask: too many arguments";
    }
}

void cmd_ssleep(vector<string> sec)
{
    if(para_num!=1)
    {
        error_msg = "[Error] sleep: need exactly one argument";
        return;
    }
    system(((string)"sleep " + sec[0]).c_str());    
}

void cmd_exec(string file, vector<string> paras)
{
    //设置参数
    char* arg[MAXSIZE];
    string strs[MAXSIZE];
    arg[0] = const_cast<char *>(file.c_str());
    for (int i = 0; i < para_num; i++)
    {
        strs[i] = GetRealString(paras[i]);
        arg[i+1] = const_cast<char *>(strs[i].c_str());
    }
    // cout<<"external execution: "<<file<<" ";
    // for (int i = 0; i < para_num; i++)
    //     cout<<arg[i+1]<<" ";
    // cout<<endl;
    arg[para_num+1] = NULL;
    //执行
    //cout<<"exe!"<<endl;
    if(execvp(file.c_str(), arg)<0)
    {
        string input = file;
        for(int i=0;i<paras.size();i++)
            input += " " + paras[i];
        error_msg += "[Error] " + input + ": command or program not found";
        return;
    }
    //exec执行成功后会退出源程序。如果能执行到下面的语句，说明exec出错
    error_msg = "[Error] exec: unable to execute \""+ file + "\"";
}

void cmd_fg(vector<string> jobs)
{
    if(para_num != 1)
    {
        error_msg = "[Error] fg: only support exactly one argument";
        *ptr_fg_pid = -3;
        return;
    }

    pid_t pid = atoi(paras[0].c_str());
    bool exist = false;
    for(int i=0;i<pros.size();i++)
    {
        if(pros[i].pid_ == pid)
        {
            if(pros[i].st_==SUSPENDED)//SUSPENDED->FG
            {
                *ptr_fg_pid = pid;
                kill(-pid, SIGCONT);
            }
            else if(pros[i].st_==RUNNING_BG)//BG->FG
            {
                *ptr_fg_pid = pid;
                //pros[i].st_=RUNNING_FG;
            }
            else
            {
                output += "Process with pid <" + to_string(pid) + "> is NOT suspended or running background.\n";
                *ptr_fg_pid = -3;
            }
            exist = true;
            break;
        }
    }
    if(!exist)
    {
        output += "No process with pid <" + to_string(pid) + "> is suspended.\n";
        *ptr_fg_pid = -3;
    }
}

void cmd_bg(vector<string> jobs)
{
    if(para_num == 0)
    {
        error_msg = "[Error] fg: no input jobs";
        return;
    }
    for(int i=0;i<para_num;i++)
    {
        pid_t pid = atoi(paras[i].c_str());
        bool exist = false;
        for(int i=0;i<pros.size();i++)
        {
            if(pros[i].pid_ == pid)
            {
                if(!(pros[i].st_==SUSPENDED))
                {
                    output += "Process with pid <" + to_string(pid) + "> is NOT suspended.\n";
                }
                else
                {
                    kill(-pid, SIGCONT);
                }
                exist = true;
                break;
            }
        }
        if(!exist)
            output += "No process with pid <" + to_string(pid) + "> is suspended.\n";
    }
}

void cmd_exit()
{
    //终止所有子进程，防止僵尸进程
    for(int i=0;i<pros.size();i++)
    {
        if(pros[i].st_!=DONE && pros[i].st_!=KILLED)
        {
            kill(-pros[i].pid_, SIGKILL);
        }
    }
    exit(0);
}

void erase_side_spaces(string &str)
{
    str.erase(0,str.find_first_not_of(" "));
    str.erase(str.find_last_not_of(" ") + 1);
}

string GetRealString(string txt)
{
    //txt已经过两侧去空格处理
    string ret;
    if(txt.length()<=1)
        return txt;
    //form字符串格式: 0无引号， 1单引号， 2双引号 
    int form = 0;
    if(txt.front()=='\''&&txt.back()=='\'')
    {
        form = 1;
        ret = txt.substr(1, txt.length()-2);
    }
    else if(txt.front()=='\"'&&txt.back()=='\"')
    {
        form = 2;
        ret = txt.substr(1, txt.length()-2);
    }
    else
        ret = txt;
    
    if(form==1)//单引号不解释字符串内的内容
        return ret;
    //无引号或双引号,且以$开头时，解释$后的内容
    if(ret.front()=='$')
    {
        if(ret.length()==2 && ret[1]>='1' && ret[1]<='9')//参数仅支持$1~$9
        {
            int para_index = ret[1] - '0';
            if(para_index<argc)
            {
                ret = argv[para_index];
            }
            else
                ret = "";
        }
        else if(ret=="$#")
        {
            ret = to_string(argc - 1);
        }
        else
        {
            ret = getenv(ret.substr(1).c_str());
        }
    }
    return ret;
}

void cmd_jobs(vector<string> jobs)
{
    bool first = true;
    if(para_num==0)//无参数，显示所有进程
    {
        for(int i=0;i<pros.size();i++)
        {
            if(!first)
            {
                output += "\n";
                first = false;
            }
            if(pros[i].st_ == DONE || pros[i].st_ == KILLED)
            {
                continue;
                output += "  ---";
            }
            output += pros[i].show_msg();
        }
    }
    else//显示指定的进程
    {
        for(int i=0;i<jobs.size();i++)
        {
            for(int j=0;j<pros.size();j++)
            {
                if(!first)
                {
                    output += "\n";
                    first = false;
                }
                if(pros[j].cmd_line_==paras[i])
                    output +=  pros[j].show_msg();
            }
        }
    }
}

void cmd_kill(vector<string> jobs)
{
    if(para_num == 0)
    {
        error_msg = "[Error] kill: no argument";
        return;
    }
    for(int i=0;i<para_num;i++)
    {
        pid_t pid = atoi(paras[i].c_str());
        bool exist = false;
        for(int i=0;i<pros.size();i++)
        {
            if(pros[i].pid_ == pid)
            {
                if(!(pros[i].st_==RUNNING_BG||pros[i].st_==SUSPENDED))
                {
                    output += "Process with pid <" + to_string(pid) + "> is NOT running background or suspended.\n";
                }
                else
                {
                    kill(-pid, SIGINT);
                }
                exist = true;
                break;
            }
        }
        if(!exist)
            output += "No process with pid <" + to_string(pid) + "> running backgroud.\n";
    }
}

void cmd_suspend(vector<string> jobs)
{
    if(para_num == 0)
    {
        error_msg = "[Error] suspend: no argument";
        return;
    }
    for(int i=0;i<para_num;i++)
    {
        pid_t pid = atoi(paras[i].c_str());
        bool exist = false;
        for(int i=0;i<pros.size();i++)
        {
            if(pros[i].pid_ == pid)
            {
                if(!(pros[i].st_==RUNNING_BG))
                {
                    output += "Process with pid <" + to_string(pid) + "> is NOT running background.\n";
                }
                else
                {
                    kill(-pid, SIGTSTP);
                }
                exist = true;
                break;
            }
        }
        if(!exist)
            output += "No process with pid <" + to_string(pid) + "> running backgroud.\n";
    }
}

//ctrl + c
void sighandle_int(int sig)
{
    pid_t pid = get_fg_proc();
    cout<<"is_sub = "<<is_sub<<" c: pid="<<pid<<endl;

    if(pid!=-1)
    {
        cout<<"kill = "<<kill(-pid, SIGINT)<<endl;//终止前台进程组
    }
    else//无前台子进程，终止shell进程
    {
        cmd_exit();
        exit(0);
    }
}

//ctrl + z
void sighandle_tsp(int sig)
{
    pid_t pid = get_fg_proc();
    cout<<"is_sub = "<<is_sub<<" z: pid="<<pid<<endl;
    if(pid!=-1)
    {
        kill(-pid, SIGTSTP);//挂起前台进程组
    }
    else//无前台子进程，挂起shell进程
    {
        cout<<"You can not suspend the shell!"<<endl;
    }
}

//sub process status change
void sighandle_chld(int sig)
{
    //cout<<"sig handle"<<endl;
    pid_t pid = -1;
    int status;
    while((pid = waitpid(-1, &status, WNOHANG|WUNTRACED|WCONTINUED)) > 0)
    {
        for(int i=0;i<pros.size();i++)
        {
            if(pros[i].pid_ == pid)
            {
                //根据状态决定后台子进程表的更新
                if(WIFEXITED(status))
                {
                    pros[i].st_ = DONE;
                    if(pid==*ptr_fg_pid)
                        *ptr_fg_pid = -1;
                    cout<<"<sig>: Process "<<pid<<" is done"<<endl;
                }
                else if(WIFSIGNALED(status))
                {
                    pros[i].st_ = KILLED;
                    if(pid==*ptr_fg_pid)
                        *ptr_fg_pid = -1;
                    cout<<"<sig>: Process "<<pid<<" is killed"<<endl;
                }
                else if(WIFSTOPPED(status))
                {
                    pros[i].st_ = SUSPENDED;
                    if(pid==*ptr_fg_pid)
                        *ptr_fg_pid = -1;
                    cout<<"<sig>: Process "<<pid<<" is suspended"<<endl;
                }
                else if(WIFCONTINUED(status))
                {
                    if(*ptr_fg_pid==pid)//fg
                    {
                        pros[i].st_ = RUNNING_FG;
                        *ptr_fg_pid = pid;
                        cout<<"<sig>: Process "<<pid<<" is running(fg)"<<endl;
                    }
                    else//bg
                    {
                        pros[i].st_ = RUNNING_BG;
                        cout<<"<sig>: Process "<<pid<<" is running(bg)"<<endl;
                    }
                }
                else
                    cout<<"Unknown status"<<endl;
                string sigstr = (string)"\n<sig " + to_string(sig) + ">: " + pros[i].show_msg();
                //write(FILE_OUTPUT, sigstr.c_str(), sigstr.length());//任务终止的输出提示
                //proc_last_end.insert(pid);
               // return;//一个信号函数只处理一次
            }
        }
    }
}

pid_t get_fg_proc()//返回当前进程表中的前台子进程的pid，如果没有前台进程，返回-1
{
    pid_t fg_get_pid = -1;
    for(int i=0;i<pros.size();i++)
    {
        if(pros[i].st_ == RUNNING_FG)
        {
            fg_get_pid = pros[i].pid_;
            break;//最多有一个前台子进程
        }
    }
    if(*ptr_fg_pid>=0 && fg_get_pid!=*ptr_fg_pid)
    {
        cout<<"fgpid not equal error!"<<endl;
    }
    return fg_get_pid;
}

void set_pro_status(pid_t pid, State st)
{
    if(pid<0)
        cout<<"Unexpected set process status"<<endl;
    if(is_sub==1)
        cout<<"Changing process status in sub process(invalid)"<<endl;
    for(int i=0;i<pros.size();i++)
    {
        if(pros[i].pid_ == pid)
        {
            pros[i].st_ = st;
        }
    }  
}