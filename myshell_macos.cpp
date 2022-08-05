//程序名：myshell
//作者、学号：管嘉瑞 3200102557

//macos version

#include <iostream>
#include <sstream>
#include <fstream>
#include <string>
#include <vector>
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
#define PARENTPATH "\\bin\\zsh" //父进程
#define HELPPATH "./_help" //确保帮助文件存在

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
    //构造函数
    Process(pid_t pid, State st, string name):pid_(pid),st_(st),cmd_line_(name){}
    //显示进程信息
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
string input;//用户输入
string WD;//working directory当前工作目录
string HOMEDIR;//主目录
string SHELLPATH;//shell可执行程序的路径
string promt;//命令行提示符
string output;//输出内容
string error_msg;//错误信息输出
string cmd;//指令名
vector<string> paras;//指令参数组
int para_num;//指令参数个数
int argc;//参数个数
vector<string> argv;
vector<Process> processes;//进程组
bool is_sub;//是否在子进程内的判断变量
bool use_file;//是否以脚本文件作为输入
string prompt;//命令提示符

/*
重要指针，指向父子进程的共享内存，包含前台进程pid信息
-3: fg failure
-2: fg waiting
-1: no fg process
>=0: fg process pid
*/
pid_t* ptr_fg_pid;

//备份的输入输出文件，用于重定向恢复
int STDIN_SAVE;
int STDOUT_SAVE;
int STDERR_SAVE;
//输入、输出、错误，可能被重定向
int FILE_IN;
int FILE_OUT;
int FILE_ERR;


//函数声明
//系统相关
void Init(int argc_, char* argv_[]);//初始化myshell
void Prepare();//每条命令执行前的准备，恢复一些信息
void Pretreat(string input);//对用户输入进行预处理
void Redirect();//对预处理后得到的paras进行分析，生成重定向信息，并删除相关参数
void do_Redirect(int file_in, int file_out, int file_err);//根据重定向信息，执行重定向
void RecoverDirection();//从重定向中恢复

//执行相关
//第一层处理，解析管道和重定向信息
void Exec_input(string input);
//第二层处理，格式标准化、执行重定向、解析并生成进程
void Exec_single(int file_in=STDIN_FILENO, int file_out=STDOUT_FILENO, int file_err=STDERR_FILENO);
//第三层处理，利用系统函数执行指令
void Exec_cmd(string cmd, vector<string> para);

//辅助函数
void erase_side_spaces(string &str);//删除字符串两侧的空格
string GetRealString(string txt);//对字符串进行处理、变量翻译，可能存在去引号、替换操作
void send_output_msg(string msg);//指令输出信息
void send_err_msg(string msg);//指令输出错误
void send_terminal_msg(string msg);//系统向终端发送信息
int cmp_time(const timespec& t1, const timespec& t2);//比较两个时间变量。-1：小于；0：等于；1：大于

//命令执行函数
void cmd_cd(vector<string>  dirs);//cd: 改变工作目录
void cmd_pwd();//pwd: 输出工作路径
void cmd_clr();//clr: 清屏
void cmd_time();//time: 输出系统时间
void cmd_echo(vector<string> txts);//echo: 显示字符串
void cmd_dir(vector<string> dirs);//dir: 列出目录下文件信息
void cmd_set(vector<string> paras);//set: 显示/创建/设置环境变量
void cmd_unset(vector<string> vars);//unset: 删除环境变量
void cmd_umask(vector<string> vals);//umask: 显示/修改umask的值
void cmd_ssleep(vector<string> sec);//ssleeep: 睡眠
void cmd_exec(string file, vector<string> paras);//exec: 执行外部命令或程序并退出myshell
void cmd_jobs(vector<string> jobs);//jobs: 显示未结束的进程信息
void cmd_kill(vector<string> jobs);//kill: 终止后台进程
void cmd_suspend(vector<string> jobs);//suspend: 挂起后台运行进程
void cmd_fg(vector<string> jobs);//fg: 将挂起的或在后台运行的进程提到前台
void cmd_bg(vector<string> jobs);//bg: 将挂起的将进程在后台继续运行
void cmd_test(vector<string> paras);//test: 测试条件
void cmd_help(vector<string> cmds);//help: 显示用户手册或指令帮助
void cmd_exit();//exit: 退出myshell

//信号处理函数
void sighandle_int(int sig);//终止信号处理（ctrl+c）
void sighandle_tsp(int sig);//挂起信号处理（ctrl+z）
void sighandle_chld(int sig);//进程状态改变信号处理（进程表更新）

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
        send_err_msg("shmget failed\n");
		exit(EXIT_FAILURE);
    }
    //将共享内存连接到当前进程的地址空间
	shm = shmat(shmid, 0, 0);
	if(shm == (void*)-1)
	{
		send_err_msg("shmget failed\n");
		exit(EXIT_FAILURE);
    }
    //赋给全局指针
    ptr_fg_pid = (pid_t*) shm;
    *ptr_fg_pid = -1;

    //判断是否通过myshell batchfile输入命令
    string batchfile;
    if(Argc>2)//不能接受2个或以上的参数
    {
        send_err_msg("Too many arguments for myshell");
        return 0;
    }
    else if(Argc==2)//脚本文件输入
    {
        batchfile = Argv[1];
        use_file = true;
    }
    ifstream batchfile_stream(batchfile);//打开文件流读取脚本文件
    if(use_file && !batchfile_stream.is_open())
    {
        send_err_msg("Unable to read file \"" + batchfile + "\"");
        return 0;
    }

    //输入与处理的循环
    while(true)
    {   
        int i;    
        prompt  = "\n\nmyshell > " + WD +" $ ";//生成命令提示符
        if(!use_file)//用户输入
        {
            send_terminal_msg(prompt);
            getline(cin,input);
        }
        else//脚本文件输入
            getline(batchfile_stream, input);

        Exec_input(input);//执行输入的命令行

        if(use_file && batchfile_stream.eof())
            break;
    }

    batchfile_stream.close();
    return 0;
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

    //获取程序自身的路径(macos)
    char buffer[MAXSIZE];
    uint32_t len = MAXSIZE;
    _NSGetExecutablePath(buffer, &len);

    //设置环境变量
    setenv("SHELL", buffer, 1);
    SHELLPATH = buffer;

    //设置父进程的路径
    setenv("PARENT", PARENTPATH, 1);

    //初始化其它全局变量
    is_sub = false;
    use_file = false;
    ptr_fg_pid = NULL;
    FILE_IN = STDIN_FILENO;
    FILE_OUT = STDOUT_FILENO;
    FILE_ERR = STDERR_FILENO;
}

void Prepare()
{
    //字符串清空
    input = "";
    output = "";
    error_msg = "";
    cmd = "";
    paras.clear();
    para_num = 0;
    //设置父进程
    setenv("PARENT", PARENTPATH, 1);
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
    //再次对每条指令清楚两侧空格
    erase_side_spaces(cmd);
}

void Redirect()//对预处理后得到的paras进行分析，重定向并删除相关参数
{
    string file_input, file_output, file_error;
    int input_red = -1;//输入重定向状态：-1:无, 0:重定向
    int output_red = -1;//输出重定向状态：-1:无, 0:覆盖， 1:追加
    int error_red = -1;//错误信息重定向状态：-1:无, 0:覆盖， 1:追加
    int red_start = -1;//重定向开始的位置,-1表示没有发生
    for(int i=0;i<para_num-1;i++)
    {
        if(i==para_num-2&&paras[para_num-1]=="&")
            break;
        string red = paras[i];
        if(red=="<"||red=="0<")//输入重定向
        {
            file_input = paras[i+1];
            input_red = 0;
            red_start = i;
        }
        else if(red==">"||red=="1>")//输出重定向（覆盖）
        {
            file_output = paras[i+1];
            output_red = 0;
            red_start = i;
        }
        else if(red==">>"||red=="1>>")//输出重定向（追加）
        {
            file_output = paras[i+1];
            output_red = 1;
            red_start = i;
        }
        else if(red=="2>")//错误信息重定向（覆盖）
        {
            file_error = paras[i+1];
            error_red = 0;
            red_start = i;
        }
        else if(red=="2>>")//错误信息重定向（追加）
        {
            file_error = paras[i+1];
            error_red = 1;
            red_start = i;
        }
        else
        {
            if( (i - red_start)%2 == 0)
            {
                error_msg += "[Error] Invalid redirection form\n";
                return;
            }
        }
    }

    //删除重定向相关的参数(从start_red开始，不包括可能存在的最后的&)
    if(red_start!=-1)
    {
        if(para_num>=1&&paras[para_num-1]=="&")
        {
            paras.erase(paras.begin()+red_start, paras.end()-1);
            para_num = paras.size();
        }
        else
        {
            paras.erase(paras.begin()+red_start, paras.end());
            para_num = paras.size();
        }
    }

    //进行重定向（修改控制重定向的全局变量文件描述符）
    if(input_red==0)//输入重定向
    {
        int fd = open(file_input.c_str(), O_RDONLY, 0444);
        if(fd<0)
            error_msg += "input file \"" + file_input + "\" open failed\n";
        else
        {
            FILE_IN = fd;
        }
    }
    if(output_red==0)//输出重定向（覆盖）
    {
        int fd = open(file_output.c_str(), O_WRONLY|O_TRUNC|O_CREAT, 0666);
        //清空文件
        if(fd<0)
            error_msg += "output file(covering) \"" + file_output + "\" open failed\n";
        else
        {
            FILE_OUT = fd;
        }
    }
    else if(output_red==1)//输出重定向（追加）
    {
        int fd = open(file_output.c_str(), O_WRONLY|O_APPEND|O_CREAT, 0666);
        if(fd<0)
            error_msg += "output file(appending) \"" + file_output + "\" open failed\n";
        else
        {
            FILE_OUT = fd;
        }
    }
    if(error_red==0)//错误信息重定向（覆盖）
    {
        int fd = open(file_error.c_str(), O_WRONLY|O_TRUNC|O_CREAT, 0666);
        //清空文件
        if(fd<0)
            error_msg += "error file(covering) \"" + file_error + "\" open failed\n";
        else
        {
            FILE_ERR = fd;
        }
    }
    else if(error_red==1)//错误信息重定向（追加）
    {
        int fd = open(file_error.c_str(), O_WRONLY|O_APPEND|O_CREAT, 0666);
        if(fd<0)
            error_msg += "error file(appending) \"" + file_error + "\" open failed\n";
        else
        {
            FILE_ERR = fd;
        }
    }
}

void do_Redirect(int file_in, int file_out, int file_err)
{
    //利用dup2函数执行重定向
    dup2(file_in, STDIN_FILENO);
    dup2(file_out, STDOUT_FILENO);
    dup2(file_err, STDERR_FILENO);
}

void RecoverDirection()
{
    //恢复可能的重定向 
    dup2(STDIN_SAVE, STDIN_FILENO);
    dup2(STDOUT_SAVE, STDOUT_FILENO);
    dup2(STDERR_SAVE, STDERR_FILENO);
    dup2(STDOUT_FILENO, STDERR_FILENO);//错误信息重定向到标准输出

    FILE_IN = STDIN_SAVE;
    FILE_OUT = STDOUT_SAVE;
    FILE_ERR = STDERR_SAVE;
}

void Exec_input(string input)//按管道符拆分，从左向右依次调用Exec_single执行
{
    //按空格分割获取每个单词
    stringstream stringstr(input);//使用串流实现对string的输入输出操作
    string temp;
    int words_num = 0;
    while(stringstr >> temp) //依次分解为每个单词
    {
        if(words_num==0)
            cmd = temp;
        else
            paras.push_back(temp);
        words_num++;
    }
    para_num = paras.size();

    vector<string> cmd_lines;//按顺序包含每个命令
    while(true)
    {
        int pos = input.find('|');
        if(pos==-1)
        {
            cmd_lines.push_back(input);//以 ｜ 为界，分割命令
            break;
        }
        cmd_lines.push_back(input.substr(0, pos));
        input = input.substr(pos+1, input.length()-pos-1);
    }
    
    //管道
    int pipe1[2] = {-1, -1};//上一条指令的管道两端
    int pipe2[2] = {-1, -1};//当前命令的管道两端
    //cmd_lines内是依次从左到右的命令
    for(int i=0;i!=cmd_lines.size();i++)
    {
        //先对每条指令预处理、重定向
        Prepare();
        Pretreat(cmd_lines[i]);
        Redirect();
        //在处理完单独指令的重定向后，进行管道重定向
        //管道重定向：当前指令的输入是上一条指令的输出，可能覆盖指令本身的重定向
        if(i==0)//第一条指令，输入不需要来自管道
        {
            pipe1[0] = FILE_IN;
            pipe1[1] = -1;
            pipe(pipe2);//生成当前指令的管道
        }
        if(i==cmd_lines.size()-1)//最后一个指令，输出不需要通向管道
        {
            pipe2[0] = -1;
            pipe2[1] = FILE_OUT;
        }
        else//中间的指令
        {
            pipe(pipe2);//生成当前指令的管道
        }
        //执行单条命令，重定向文件与管道相关
        Exec_single(pipe1[0], pipe2[1], FILE_ERR);
        //pipe2赋值给pipe1
        pipe1[0] = pipe2[0];
        pipe1[1] = pipe2[1];
        pipe2[0] = pipe2[1] = -1;
    }
}

void Exec_single(int file_in, int file_out, int file_err)
{
    if(error_msg!="")
    {  
        send_err_msg(error_msg);//在指令分析处理阶段发现错误，输出错误信息，不会执行
        RecoverDirection();
        return;
    }

    bool last_bg = (para_num>=1 && paras[para_num-1]=="&");//last_bg表示指令是否以&结尾
    bool is_fg = (cmd=="fg");//是否是fg指令
    bool bg = last_bg || is_fg;//fg强制后台执行
    //生成处理后的指令字符串
    string cmd_line = cmd;
    for(int i=0;i<para_num;i++)
    {
        if(last_bg && i==para_num-1)
            break;
        cmd_line += " "+ paras[i];
    }

    //备份标准输入输出
    STDIN_SAVE = dup(STDIN_FILENO);
    STDOUT_SAVE = dup(STDOUT_FILENO);
    STDERR_SAVE = dup(STDERR_FILENO);

    //内嵌指令，与系统变量相关，直接执行，不生成子进程
    if(cmd=="exit" || cmd=="cd" || cmd=="set" || cmd=="unset" || cmd=="umask")
    {
        do_Redirect(file_in, file_out, file_err);//父进程重定向
        if(last_bg)
        {
            paras.erase(paras.end()-1);
            para_num--;
            error_msg += "[Warning] add background identifier & for build-in command \"" + cmd +"\"\n";
        }
        Exec_cmd(cmd, paras);//执行
        RecoverDirection();//恢复重定向
        return;
    }

    pid_t pid = fork();//生成子进程！
    if(pid)//父进程
    {
        Process subpro(pid, bg?RUNNING_BG:RUNNING_FG, cmd_line);//生成进程信息
        if(bg==false)//前台运行
        {
            *ptr_fg_pid = pid;
        }
        else if(is_fg)
        {
            *ptr_fg_pid = -2;
        }
        processes.push_back(subpro);//进程表新增项
        setenv("PARENT", SHELLPATH.c_str(), 1);//设置环境变量 
    }
    else//子进程
    {
        is_sub = true;
        setpgid(0, 0);//使子进程单独成为一个进程组。后台进程组自动忽略Ctrl+Z、Ctrl+C等信号
        do_Redirect(file_in, file_out, file_err);//子进程内重定向
        if(last_bg)//末尾有&，后台运行子进程
        {
            paras.erase(paras.end()-1);
            para_num--;
            Exec_cmd(cmd, paras);   
        }
        else
        {
            Exec_cmd(cmd, paras);//前台运行子进程
        }
        //子进程执行完毕，返回父进程
        exit(0);
    }
    while(true)//如果是fg，等待其完成
    {
        if(*ptr_fg_pid!=-2)
            break;
    }
    if(*ptr_fg_pid==-3)//fg指令执行失败，没有进程被提到前台
    {
        *ptr_fg_pid = -1;
        return;
    }
    //赋予fg运行信息
    for(int i=0;i<processes.size();i++)
    {
        if(processes[i].pid_==*ptr_fg_pid && processes[i].st_!=RUNNING_FG)
            processes[i].st_ = RUNNING_FG;
    }
    //等待前台进程完成
    while(true)
    {
        if(*ptr_fg_pid==-1)
            break;
    }
    
    //恢复可能的重定向 
    RecoverDirection();
}

void Exec_cmd(string cmd, vector<string> paras)
{
    bool exit = false;//是否要退出myshell
    //根据cmd名，执行具体指令，paras是参数组
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
    else if(cmd=="test")
    {
        cmd_test(paras);
    }
    else if(cmd=="help")
    {
        cmd_help(paras);
    }
    else if(cmd=="")
    {
        output = "";
    }
    else
    {
        if(cmd=="exec")
        {
            //paras的第一个元素是执行的文件名
            //exec file para1 para2
            if(para_num==0)
            {
                error_msg += "[Error] exec: no input file\n";
                return;
            }
            //参数从第二个开始
            cmd = paras[0];
            paras.erase(paras.begin());
            para_num--;
            exit = true;
        }
        cmd_exec(cmd, paras);
        if(exit)//exec完成后退出myshell
            cmd_exit();
    }

    //(子进程内)输出信息
    send_output_msg(output);
    send_err_msg(error_msg);
}

void cmd_cd(vector<string>  dirs)
{
    if(para_num!=1)//cd必须有且只有一个参数
    {
        error_msg += "[Error] cd: No argument or too many arguments\n";
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
        error_msg += "[Error] cd: Unable to change directory to " + dir + "\n";
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
    output += WD;
}

void cmd_clr()
{
    system("clear");
}

void cmd_time()
{
    //获取系统时间信息
    time_t tt = time(NULL);
    struct tm * t = localtime(&tt);
    //用stringstream生成返回信息
    string Week[] = {"星期一", "星期二", "星期三", "星期四", "星期五", "星期六", "星期日"};
    output += to_string(t->tm_year + 1900) + "年" + to_string(t->tm_mon + 1) + "月" 
    + to_string(t->tm_mday) + "日"+ " " + Week[t->tm_wday]
    + " " + to_string(t->tm_hour) + "时" + to_string(t->tm_min) + "分" + to_string(t->tm_sec) + "秒";
}

void cmd_echo(vector<string> txts)
{
    //按空格分割单词
    for(int i=0;i<para_num;i++)
    {
        output += GetRealString(txts[i]);//解析每个字符串
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
            output += "Can not open directory \"" + dir + "\"\n";
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
        error_msg += "[Error] set: too many arguments!\n";
    }
}

void cmd_unset(vector<string> vars)
{
    //依次删除每个环境变量
    if(para_num==0)
    {
        error_msg += "[Error] unset: not enough arguments\n";
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
        output += to_string((mode >> 9) & 7) + to_string((mode >> 6) & 7)
                 + to_string((mode >> 3) & 7) + to_string(mode & 7);
    }
    //一个参数，设置值
    else if(para_num==1)
    {
        string umask_str = paras[0];
        if(umask_str.length()>=5)
        {
            error_msg += "[Error] umask: too many digits for umask\n";
            return;
        }
        char* temp = NULL;
        //8进制转化
        mode_t mode = strtol(umask_str.c_str(), &temp, 8);
        if(*temp!='\0')//输入的umask值中出现了非0～7的字符
        {
            error_msg += "[Error] umask: invalid umask expression\n";
            return;
        }
        umask(mode);
        output += "set: umask=" + to_string((mode >> 9) & 7) + to_string((mode >> 6) & 7)
            + to_string((mode >> 3) & 7) + to_string(mode & 7);
    }
    else
    {
        error_msg += "[Error] umask: too many arguments\n";
    }
}

void cmd_ssleep(vector<string> sec)
{
    if(para_num!=1)
    {
        error_msg += "[Error] sleep: need exactly one argument\n";
        return;
    }
    system(((string)"sleep " + sec[0]).c_str());//系统睡眠
}

void cmd_exec(string file, vector<string> paras)
{
    //设置参数
    char* arg[MAXSIZE];
    string strs[MAXSIZE];
    arg[0] = const_cast<char *>(file.c_str());
    //生成参数组
    for (int i = 0; i < para_num; i++)
    {
        strs[i] = GetRealString(paras[i]);
        arg[i+1] = const_cast<char *>(strs[i].c_str());
    }
    arg[para_num+1] = NULL;
    //执行（execvp）
    if(execvp(file.c_str(), arg)<0)//execvp负责执行外部命令或程序！
    {
        string input = file;
        for(int i=0;i<paras.size();i++)
            input += " " + paras[i];
        error_msg += "[Error] " + input + ": command or program not found";
        return;
    }
    //exec执行成功后会退出源程序。如果能执行到下面的语句，说明exec出错
    error_msg += "[Error] exec: unable to execute \""+ file + "\"\n";
}

void cmd_fg(vector<string> jobs)
{
    if(para_num != 1)//fg必须有且只有一个pid作为参数
    {
        error_msg += "[Error] fg: only support exactly one argument\n";
        *ptr_fg_pid = -3;
        return;
    }

    pid_t pid = atoi(paras[0].c_str());
    bool exist = false;
    //找到该进程并提到前台
    for(int i=0;i<processes.size();i++)
    {
        if(processes[i].pid_ == pid)
        {
            if(processes[i].st_==SUSPENDED)//SUSPENDED->FG
            {
                *ptr_fg_pid = pid;
                kill(-pid, SIGCONT);//发送继续进程的信号
            }
            else if(processes[i].st_==RUNNING_BG)//BG->FG
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
        error_msg += "[Error] fg: no input jobs\n";
        return;
    }
    //找到该进程并后台继续运行
    for(int i=0;i<para_num;i++)
    {
        pid_t pid = atoi(paras[i].c_str());
        bool exist = false;
        for(int i=0;i<processes.size();i++)
        {
            if(processes[i].pid_ == pid)
            {
                if(!(processes[i].st_==SUSPENDED))
                {
                    output += "Process with pid <" + to_string(pid) + "> is NOT suspended.\n";
                }
                else
                {
                    //给后台挂起的进程发送SIGCONT信号
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
    for(int i=0;i<processes.size();i++)
    {
        if(processes[i].st_!=DONE && processes[i].st_!=KILLED)
        {
            kill(-processes[i].pid_, SIGKILL);
        }
    }
    exit(0);
}

void cmd_jobs(vector<string> jobs)
{
    bool first = true;
    if(para_num==0)//无参数，显示所有进程
    {
        for(int i=0;i<processes.size();i++)
        {
            if(!first)
            {
                output += "\n";
                first = false;
            }
            if(processes[i].st_ == DONE || processes[i].st_ == KILLED)
            {
                continue;
                output += "  ---";
            }
            output += processes[i].show_msg();
        }
    }
    else//找到并显示指定名称的进程
    {
        for(int i=0;i<jobs.size();i++)
        {
            for(int j=0;j<processes.size();j++)
            {
                if(!first)
                {
                    output += "\n";
                    first = false;
                }
                if(processes[j].cmd_line_==paras[i])
                    output +=  processes[j].show_msg();
            }
        }
    }
}

void cmd_kill(vector<string> jobs)
{
    if(para_num == 0)
    {
        error_msg += "[Error] kill: no argument\n";
        return;
    }
    //找到并杀死指定的后台进程
    for(int i=0;i<para_num;i++)
    {
        pid_t pid = atoi(paras[i].c_str());
        bool exist = false;
        for(int i=0;i<processes.size();i++)
        {
            if(processes[i].pid_ == pid)
            {
                if(!(processes[i].st_==RUNNING_BG||processes[i].st_==SUSPENDED))
                {
                    output += "Process with pid <" + to_string(pid) + "> is NOT running background or suspended.\n";
                }
                else
                {
                    //发送SIGINT信号终止进程
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
        error_msg += "[Error] suspend: no argument\n";
        return;
    }
    //找到并挂起指定的后台进程
    for(int i=0;i<para_num;i++)
    {
        pid_t pid = atoi(paras[i].c_str());
        bool exist = false;
        for(int i=0;i<processes.size();i++)
        {
            if(processes[i].pid_ == pid)
            {
                if(!(processes[i].st_==RUNNING_BG))
                {
                    output += "Process with pid <" + to_string(pid) + "> is NOT running background.\n";
                }
                else
                {
                    //发送SIGTSP信号挂起进程
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

void cmd_test(vector<string> paras)
{
    if (para_num == 3){//二元运算符，有且仅有3个参数
        string val1 = GetRealString(paras[0]);
        string val2 = GetRealString(paras[2]);
        if (paras[1] == "-ef"){//文件1和文件2的设备和inode相同
            struct stat buf1, buf2;
            int ret1 = lstat(val1.c_str(), &buf1);
            int ret2 = lstat(val2.c_str(), &buf2);
            if (ret1 == 0 && ret2 == 0 && buf1.st_dev == buf2.st_dev && buf1.st_ino == buf2.st_ino) output +="true\n";
            else output +="false\n";
        }else if (paras[1] == "-nt"){//文件1比文件2更新
            struct stat buf1, buf2;
            int ret1 = lstat(val1.c_str(), &buf1);
            int ret2 = lstat(val2.c_str(), &buf2);
            if (ret1 == 0 && ret2 == 0 && cmp_time(buf1.st_mtimespec, buf2.st_mtimespec) == 1) output +="true\n";
            else output +="false\n";
        }else if (paras[1] == "-ot"){//文件1比文件2更旧
            struct stat buf1, buf2;
            int ret1 = lstat(val1.c_str(), &buf1);
            int ret2 = lstat(val2.c_str(), &buf2);
            if (ret1 == 0 && ret2 == 0 && cmp_time(buf1.st_mtimespec, buf2.st_mtimespec) == -1) output +="true\n";
            else output +="false\n";
        }else if (paras[1] == "="){//字符串相等
            if (val1 == val2) output +="true\n";
            else output +="false\n";
        }else if (paras[1] == "!="){//字符串不等
            if (val1 != val2) output +="true\n";
            else output +="false\n";
        }
        else
        {
            int Value1, Value2;
            Value1 = atoi(val1.c_str());
            Value2 = atoi(val2.c_str());
            if (paras[1] == "-eq"){//整数==
                if (Value1 == Value2) output +="true\n";
                else output +="false\n";
            }
            else if (paras[1] == "-ge"){//整数>=
                if (Value1 >= Value2) output +="true\n";
                else output +="false\n";
            }
            else if (paras[1] == "-gt"){//整数>
                if (Value1 > Value2) output +="true\n";
                else output +="false\n";
            }
            else if (paras[1] == "-le"){//整数<=
                if (Value1 <= Value2) output +="true\n";
                else output +="false\n";
            }
            else if (paras[1] == "-lt"){//整数<
                if (Value1 < Value2) output +="true\n";
                else output +="false\n";
            }
            else if (paras[1] == "-ne"){//整数!=
                if (Value1 != Value2) output +="true\n";
                else output +="false\n";
            }
            else{//无法识别的运算符
                error_msg += "[Error] test: Unknown option " + paras[1] + "\n";
            }
        }
    }
    else if (para_num== 2){//一元运算符，有且仅有2个参数
        string val = GetRealString(paras[1]);
        if (paras[0] == "-e"){//文件存在
            struct stat buf;
            int ret = lstat(val.c_str(), &buf);
            if (ret == 0) output +="true\n";
            else output +="false\n";
        }else if (paras[0] == "-r"){//文件可读
            struct stat buf;
            int ret = lstat(val.c_str(), &buf);
            if (ret == 0 && access(val.c_str(), R_OK)) output +="true\n";
            else output +="false\n";
        }else if (paras[0] == "-w"){//文件可写
            struct stat buf;
            int ret = lstat(val.c_str(), &buf);
            if (ret == 0 && access(val.c_str(), W_OK)) output +="true\n";
            else output +="false\n";
        }else if (paras[0] == "-x"){//文件可执行
            struct stat buf;
            int ret = lstat(val.c_str(), &buf);
            if (ret == 0 && access(val.c_str(), X_OK)) output +="true\n";
            else output +="false\n";
        }else if (paras[0] == "-s"){//文件至少有一个字符
            struct stat buf;
            int ret = lstat(val.c_str(), &buf);
            if (ret == 0 && buf.st_size) output +="true\n";
            else output +="false\n";
        }else if (paras[0] == "-d"){//文件为目录
            struct stat buf;
            int ret = lstat(val.c_str(), &buf);
            if (ret == 0 && S_ISDIR(buf.st_mode)) output +="true\n";
            else output +="false\n";
        }else if (paras[0] == "-f"){//文件为普通文件
            struct stat buf;
            int ret = lstat(val.c_str(), &buf);
            if (ret == 0 && S_ISREG(buf.st_mode)) output +="true\n";
            else output +="false\n";
        }else if (paras[0] == "-c"){//文件为字符型特殊文件
            struct stat buf;
            int ret = lstat(val.c_str(), &buf);
            if (ret == 0 && S_ISCHR(buf.st_mode)) output +="true\n";
            else output +="false\n";
        }else if (paras[0] == "-b"){//文件为块特殊文件
            struct stat buf;
            int ret = lstat(val.c_str(), &buf);
            if (ret == 0 && S_ISBLK(buf.st_mode)) output +="true\n";
            else output +="false\n";
        }else if (paras[0] == "-h" || paras[0] == "-L"){//文件为符号链接
            struct stat buf;
            int ret = lstat(val.c_str(), &buf);
            if (ret == 0 && S_ISLNK(buf.st_mode)) output +="true\n";
            else output +="false\n";
        }else if (paras[0] == "-p"){//文件为命名管道
            struct stat buf;
            int ret = lstat(val.c_str(), &buf);
            if (ret == 0 && S_ISFIFO(buf.st_mode)) output +="true\n";
            else output +="false\n";
        }else if (paras[0] == "-S"){//文件为嵌套字
            struct stat buf;
            int ret = lstat(val.c_str(), &buf);
            if (ret == 0 && S_ISSOCK(buf.st_mode)) output +="true\n";
            else output +="false\n";
        }else if (paras[0] == "-G"){//文件被实际组拥有
            struct stat buf;
            int ret = lstat(val.c_str(), &buf);
            if (ret == 0 && buf.st_gid == getgid()) output +="true\n";
            else output +="false\n";
        }else if (paras[0] == "-O"){//文件被实际用户拥有
            struct stat buf;
            int ret = lstat(val.c_str(), &buf);
            if (ret == 0 && buf.st_uid == getuid()) output +="true\n";
            else output +="false\n";
        }else if (paras[0] == "-g"){//文件有设置组位
            struct stat buf;
            int ret = lstat(val.c_str(), &buf);
            if (ret == 0 && (S_ISGID & buf.st_mode)) output +="true\n";
            else output +="false\n";
        }else if (paras[0] == "-u"){//文件有设置用户位
            struct stat buf;
            int ret = lstat(val.c_str(), &buf);
            if (ret == 0 && (S_ISUID & buf.st_mode)) output +="true\n";
            else output +="false\n";
        }else if (paras[0] == "-k"){//文件有设置粘滞位
            struct stat buf;
            int ret = lstat(val.c_str(), &buf);
            if (ret == 0 && (S_ISVTX & buf.st_mode)) output +="true\n";
            else output +="false\n";
        }else if (paras[0] == "-n"){//字符串长度非0
            if (val.length()) output +="true\n";
            else output +="false\n";
        }else if (paras[0] == "-z"){//字符串长度为0
            if (val.length() == 0) output +="true\n";
            else output +="false\n";
        }else{
            error_msg += "[Error] test: Unknown option " + paras[0] + "\n";
        }
    }
    else
    {
        error_msg += "[Error] test: Invalid test form\n";
    }
}

void cmd_help(vector<string> cmds)
{
   if (para_num >= 2){
        error_msg += "[Error] help: Too many parameters.\n";
        return;
    }
    string Target;//帮助的命令名（默认为myshell，表示查看用户手册）
    if (para_num == 0) Target = "myshell";//若无参, 输出全局帮助手册
    else Target = paras[0];//否则输出对应指令的帮助手册

    //打开帮助文件
    ifstream ifs(HELPPATH);
    if (!ifs.is_open()){
        error_msg += "[Error] help: Help manual file is not found\n";
        return;
    }
    string line;//一行的信息
    while(!ifs.eof())//先找到对应的区域
    {
        getline(ifs, line);
        if(line[0]=='#')
        {
            line = line.substr(1, line.rfind('#')-1);
            // #Target# 表示一块新的帮助区域
            if(line==Target)
            {
                //开始输出对应部分的提示信息
                string msg_line;
                while(!ifs.eof())//逐行读取这个区域
                {
                    getline(ifs, msg_line);
                    if(msg_line[0]!='#')
                        output += msg_line + "\n";
                    else
                    {
                        ifs.close();
                        return;
                    }
                }
            }
        }
    }
    ifs.close();
    error_msg += "[Error] help: There's no help manual for " + Target + ".\n";
}

//ctrl + c
void sighandle_int(int sig)
{
    pid_t pid = *ptr_fg_pid;

    if(pid!=-1)
    {
        kill(-pid, SIGINT);//终止前台进程组
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
    pid_t pid = *ptr_fg_pid;
    if(pid!=-1)
    {
        kill(-pid, SIGTSTP);//挂起前台进程组
    }
    else//无前台子进程，挂起shell进程
    {
        send_err_msg("You can not suspend the shell!");
    }
}

//子进程状态改变信号的处理函数，重要！
void sighandle_chld(int sig)
{
    pid_t pid = -1;
    int status;//进程状态
    //发送完信号后，waitpid函数会等到进程状态改变
    while((pid = waitpid(-1, &status, WNOHANG|WUNTRACED|WCONTINUED)) > 0)
    {
        for(int i=0;i<processes.size();i++)
        {
            if(processes[i].pid_ == pid)
            {
                bool is_fg = (processes[i].st_==RUNNING_FG);
                //根据状态决定后台子进程表的更新
                if(WIFEXITED(status))//进程结束
                {
                    processes[i].st_ = DONE;
                    if(pid==*ptr_fg_pid)
                        *ptr_fg_pid = -1;
                    string sig_str = "\n<sig>: Process " + to_string(pid) + " (" + processes[i].cmd_line_ +") is done\n";
                    if(!is_fg&&!use_file)
                    {
                        send_terminal_msg(sig_str);
                        send_err_msg(prompt);
                    }
                }
                else if(WIFSIGNALED(status))//进程被终止
                {
                    processes[i].st_ = KILLED;
                    if(pid==*ptr_fg_pid)
                        *ptr_fg_pid = -1;
                    string sig_str = "\n<sig>: Process " + to_string(pid) + " (" + processes[i].cmd_line_ +") is killed\n";
                    if(!use_file)
                    {
                        send_terminal_msg(sig_str);
                        if(!is_fg)
                            send_err_msg(prompt);
                    }
                }
                else if(WIFSTOPPED(status))//进程被挂起（停止）
                {
                    processes[i].st_ = SUSPENDED;
                    if(pid==*ptr_fg_pid)
                        *ptr_fg_pid = -1;
                    string sig_str = "\n<sig>: Process " + to_string(pid) + " (" + processes[i].cmd_line_ +") is suspended\n";
                    if(!use_file)
                    {
                        send_terminal_msg(sig_str);
                        if(!is_fg)
                            send_err_msg(prompt);
                    }
                }
                else if(WIFCONTINUED(status))//进程继续执行
                {
                    if(*ptr_fg_pid==pid)//fg
                    {
                        processes[i].st_ = RUNNING_FG;
                        *ptr_fg_pid = pid;
                        string sig_str = "\n<sig>: Process " + to_string(pid) + " (" + processes[i].cmd_line_ +") is running(fg)\n";
                        if(!use_file)
                        {
                            send_terminal_msg(sig_str);
                            if(!is_fg)
                                send_err_msg(prompt);
                        }
                    }
                    else//bg
                    {
                        processes[i].st_ = RUNNING_BG;
                        string sig_str = "\n<sig>: Process " + to_string(pid) + " (" + processes[i].cmd_line_ +") is running(bg)\n";
                        if(!use_file)
                        {
                            send_terminal_msg(sig_str);
                            if(!is_fg)
                                send_err_msg(prompt);
                        }
                    }
                }
                else//意外情况
                {
                    string sig_str = "\n<sig>: Unexpected signal for subprocess\n";
                    if(!use_file)
                    {
                        send_terminal_msg(sig_str);
                        if(!is_fg)
                            send_err_msg(prompt);
                    }
                }
            }
        }
    }
}

int cmp_time(const timespec& t1, const timespec& t2)
{
    //依次比较时间项
    if (t1.tv_sec < t2.tv_sec)
        return -1;
    if (t1.tv_sec > t2.tv_sec)
        return 1;
    if (t1.tv_nsec < t2.tv_nsec)
        return -1;
    if (t1.tv_nsec > t2.tv_nsec)
        return 1;
    return 0;
}


void erase_side_spaces(string &str)
{
    //移除两侧空格
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
        else if(ret=="$#")//参数个数
        {
            ret = to_string(argc - 1);
        }
        else//环境变量解释
        {
            ret = getenv(ret.substr(1).c_str());
        }
    }
    return ret;
}

void send_output_msg(string msg)
{
    //写入标准输出，可能被重定向
    write(STDOUT_FILENO, msg.c_str(), msg.length()); 
}

void send_err_msg(string msg)
{
    //写入标准错误，可能被重定向
    write(STDERR_FILENO, msg.c_str(), msg.length());
}

void send_terminal_msg(string msg)
{
    //写入标准输出，可能被重定向
    write(STDOUT_FILENO, msg.c_str(), msg.length()); 
}