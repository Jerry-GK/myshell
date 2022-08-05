//��������myshell
//���ߡ�ѧ�ţ��ܼ��� 3200102557

//linux version

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

#include <unordered_set>
#include <string.h>
#define MAXSIZE 1024
#define PARENTPATH "\\bin\\bash" //������
#define HELPPATH "./_help" //ȷ�������ļ�����

using namespace std;
//����״̬
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

    RUNNING_FG,//ǰ̨����
    RUNNING_BG,//��̨����
    SUSPENDED,//����
    KILLED,//��ǿ����ֹ
    DONE//��������
};

//������
class Process
{
public:
    //���캯��
    Process(pid_t pid, State st, string name):pid_(pid),st_(st),cmd_line_(name){}
    //��ʾ������Ϣ
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
    pid_t pid_;//����pid
    State st_;//����״̬
    string cmd_line_;//������ָ���ַ���
};

//ȫ�ֱ���
char buffer[MAXSIZE] = {0};
string input;//�û�����
string WD;//working directory��ǰ����Ŀ¼
string HOMEDIR;//��Ŀ¼
string SHELLPATH;//shell��ִ�г����·��
string promt;//��������ʾ��
string output;//�������
string error_msg;//������Ϣ���
string cmd;//ָ����
vector<string> paras;//ָ�������
int para_num;//ָ���������
int argc;//��������
vector<string> argv;
vector<Process> processes;//������
bool is_sub;//�Ƿ����ӽ����ڵ��жϱ���
bool use_file;//�Ƿ��Խű��ļ���Ϊ����
string prompt;//������ʾ��

/*
��Ҫָ�룬ָ���ӽ��̵Ĺ����ڴ棬����ǰ̨����pid��Ϣ
-3: fg failure
-2: fg waiting
-1: no fg process
>=0: fg process pid
*/
pid_t* ptr_fg_pid;

//���ݵ���������ļ��������ض���ָ�
int STDIN_SAVE;
int STDOUT_SAVE;
int STDERR_SAVE;
//���롢��������󣬿��ܱ��ض���
int FILE_IN;
int FILE_OUT;
int FILE_ERR;


//��������
//ϵͳ���
void Init(int argc_, char* argv_[]);//��ʼ��myshell
void Prepare();//ÿ������ִ��ǰ��׼�����ָ�һЩ��Ϣ
void Pretreat(string input);//���û��������Ԥ����
void Redirect();//��Ԥ�����õ���paras���з����������ض�����Ϣ����ɾ����ز���
void do_Redirect(int file_in, int file_out, int file_err);//�����ض�����Ϣ��ִ���ض���
void RecoverDirection();//���ض����лָ�

//ִ�����
//��һ�㴦�������ܵ����ض�����Ϣ
void Exec_input(string input);
//�ڶ��㴦����ʽ��׼����ִ���ض��򡢽��������ɽ���
void Exec_single(int file_in=STDIN_FILENO, int file_out=STDOUT_FILENO, int file_err=STDERR_FILENO);
//�����㴦������ϵͳ����ִ��ָ��
void Exec_cmd(string cmd, vector<string> para);

//��������
void erase_side_spaces(string &str);//ɾ���ַ�������Ŀո�
string GetRealString(string txt);//���ַ������д����������룬���ܴ���ȥ���š��滻����
void send_output_msg(string msg);//ָ�������Ϣ
void send_err_msg(string msg);//ָ���������
void send_terminal_msg(string msg);//ϵͳ���ն˷�����Ϣ
int cmp_time(const timespec& t1, const timespec& t2);//�Ƚ�����ʱ�������-1��С�ڣ�0�����ڣ�1������

//����ִ�к���
void cmd_cd(vector<string>  dirs);//cd: �ı乤��Ŀ¼
void cmd_pwd();//pwd: �������·��
void cmd_clr();//clr: ����
void cmd_time();//time: ���ϵͳʱ��
void cmd_echo(vector<string> txts);//echo: ��ʾ�ַ���
void cmd_dir(vector<string> dirs);//dir: �г�Ŀ¼���ļ���Ϣ
void cmd_set(vector<string> paras);//set: ��ʾ/����/���û�������
void cmd_unset(vector<string> vars);//unset: ɾ����������
void cmd_umask(vector<string> vals);//umask: ��ʾ/�޸�umask��ֵ
void cmd_ssleep(vector<string> sec);//ssleeep: ˯��
void cmd_exec(string file, vector<string> paras);//exec: ִ���ⲿ���������˳�myshell
void cmd_jobs(vector<string> jobs);//jobs: ��ʾδ�����Ľ�����Ϣ
void cmd_kill(vector<string> jobs);//kill: ��ֹ��̨����
void cmd_suspend(vector<string> jobs);//suspend: �����̨���н���
void cmd_fg(vector<string> jobs);//fg: ������Ļ��ں�̨���еĽ����ᵽǰ̨
void cmd_bg(vector<string> jobs);//bg: ������Ľ������ں�̨��������
void cmd_test(vector<string> paras);//test: ��������
void cmd_help(vector<string> cmds);//help: ��ʾ�û��ֲ��ָ�����
void cmd_exit();//exit: �˳�myshell

//�źŴ�����
void sighandle_int(int sig);//��ֹ�źŴ���ctrl+c��
void sighandle_tsp(int sig);//�����źŴ���ctrl+z��
void sighandle_chld(int sig);//����״̬�ı��źŴ������̱���£�

//main����
int main(int Argc, char * Argv[])
{
    //�źŴ���
    signal(SIGINT,  sighandle_int); // ctrl-c, kill signal
    signal(SIGTSTP, sighandle_tsp); // ctrl-z, suspend signal
    signal(SIGCHLD, sighandle_chld); //status changed of subprocess

    //��ʼ��
    Init(Argc, Argv);

    //�����ڴ棬���ӽ���ͨ��
    void *shm = NULL;//����Ĺ����ڴ��ԭʼ�׵�ַ
    int shmid;//�����ڴ��ʶ��
	//���������ڴ�
	shmid = shmget((key_t)1234, sizeof(pid_t), 0666|IPC_CREAT);
	if(shmid == -1)
	{
        send_err_msg("shmget failed\n");
		exit(EXIT_FAILURE);
    }
    //�������ڴ����ӵ���ǰ���̵ĵ�ַ�ռ�
	shm = shmat(shmid, 0, 0);
	if(shm == (void*)-1)
	{
		send_err_msg("shmget failed\n");
		exit(EXIT_FAILURE);
    }
    //����ȫ��ָ��
    ptr_fg_pid = (pid_t*) shm;
    *ptr_fg_pid = -1;

    //�ж��Ƿ�ͨ��myshell batchfile��������
    string batchfile;
    if(Argc>2)//���ܽ���2�������ϵĲ���
    {
        send_err_msg("Too many arguments for myshell");
        return 0;
    }
    else if(Argc==2)//�ű��ļ�����
    {
        batchfile = Argv[1];
        use_file = true;
    }
    ifstream batchfile_stream(batchfile);//���ļ�����ȡ�ű��ļ�
    if(use_file && !batchfile_stream.is_open())
    {
        send_err_msg("Unable to read file \"" + batchfile + "\"");
        return 0;
    }

    //�����봦���ѭ��
    while(true)
    {   
        int i;    
        prompt  = "\n\nmyshell > " + WD +" $ ";//����������ʾ��
        if(!use_file)//�û�����
        {
            send_terminal_msg(prompt);
            getline(cin,input);
        }
        else//�ű��ļ�����
            getline(batchfile_stream, input);

        Exec_input(input);//ִ�������������

        if(use_file && batchfile_stream.eof())
            break;
    }

    batchfile_stream.close();
    return 0;
}

//����������
void Init(int argc_, char* argv_[])
{
    //��ʼ����������
    WD = getenv("PWD");
    HOMEDIR = getenv("HOME");
    argc = argc_;
    for(int i=0;i<argc;i++)
    {
        argv.push_back(argv_[i]);
    }

    //��ȡ���������·��(linux)
    char buffer[MAXSIZE];
    ssize_t len = readlink("/proc/self/exe", buffer, MAXSIZE);//��ȡ����λ��
    buffer[len] = '\0';

    //���û�������
    setenv("SHELL", buffer, 1);
    SHELLPATH = buffer;

    //���ø����̵�·��
    setenv("PARENT", PARENTPATH, 1);

    //��ʼ������ȫ�ֱ���
    is_sub = false;
    use_file = false;
    ptr_fg_pid = NULL;
    FILE_IN = STDIN_FILENO;
    FILE_OUT = STDOUT_FILENO;
    FILE_ERR = STDERR_FILENO;
}

void Prepare()
{
    //�ַ������
    input = "";
    output = "";
    error_msg = "";
    cmd = "";
    paras.clear();
    para_num = 0;
    //���ø�����
    setenv("PARENT", PARENTPATH, 1);
}

void Pretreat(string input)
{
    //ȥ����������Ŀո�
    if(input.empty())
    {
        return;
    }
    erase_side_spaces(input);
    //���ո�ָ��ȡÿ�����ʣ���һ��������ָ�������������ǲ�����
    stringstream stringstr(input);//   ʹ�ô���ʵ�ֶ�string�������������
    string temp;
    int words_num = 0;
    while(stringstr >> temp) //  ���ηֽ�Ϊÿ������
    {
        if(words_num==0)
            cmd = temp;
        else
            paras.push_back(temp);
        words_num++;
    }
    para_num = paras.size();
    //�ٴζ�ÿ��ָ���������ո�
    erase_side_spaces(cmd);
}

void Redirect()//��Ԥ�����õ���paras���з������ض���ɾ����ز���
{
    string file_input, file_output, file_error;
    int input_red = -1;//�����ض���״̬��-1:��, 0:�ض���
    int output_red = -1;//����ض���״̬��-1:��, 0:���ǣ� 1:׷��
    int error_red = -1;//������Ϣ�ض���״̬��-1:��, 0:���ǣ� 1:׷��
    int red_start = -1;//�ض���ʼ��λ��,-1��ʾû�з���
    for(int i=0;i<para_num-1;i++)
    {
        if(i==para_num-2&&paras[para_num-1]=="&")
            break;
        string red = paras[i];
        if(red=="<"||red=="0<")//�����ض���
        {
            file_input = paras[i+1];
            input_red = 0;
            red_start = i;
        }
        else if(red==">"||red=="1>")//����ض��򣨸��ǣ�
        {
            file_output = paras[i+1];
            output_red = 0;
            red_start = i;
        }
        else if(red==">>"||red=="1>>")//����ض���׷�ӣ�
        {
            file_output = paras[i+1];
            output_red = 1;
            red_start = i;
        }
        else if(red=="2>")//������Ϣ�ض��򣨸��ǣ�
        {
            file_error = paras[i+1];
            error_red = 0;
            red_start = i;
        }
        else if(red=="2>>")//������Ϣ�ض���׷�ӣ�
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

    //ɾ���ض�����صĲ���(��start_red��ʼ�����������ܴ��ڵ�����&)
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

    //�����ض����޸Ŀ����ض����ȫ�ֱ����ļ���������
    if(input_red==0)//�����ض���
    {
        int fd = open(file_input.c_str(), O_RDONLY, 0444);
        if(fd<0)
            error_msg += "input file \"" + file_input + "\" open failed\n";
        else
        {
            FILE_IN = fd;
        }
    }
    if(output_red==0)//����ض��򣨸��ǣ�
    {
        int fd = open(file_output.c_str(), O_WRONLY|O_TRUNC|O_CREAT, 0666);
        //����ļ�
        if(fd<0)
            error_msg += "output file(covering) \"" + file_output + "\" open failed\n";
        else
        {
            FILE_OUT = fd;
        }
    }
    else if(output_red==1)//����ض���׷�ӣ�
    {
        int fd = open(file_output.c_str(), O_WRONLY|O_APPEND|O_CREAT, 0666);
        if(fd<0)
            error_msg += "output file(appending) \"" + file_output + "\" open failed\n";
        else
        {
            FILE_OUT = fd;
        }
    }
    if(error_red==0)//������Ϣ�ض��򣨸��ǣ�
    {
        int fd = open(file_error.c_str(), O_WRONLY|O_TRUNC|O_CREAT, 0666);
        //����ļ�
        if(fd<0)
            error_msg += "error file(covering) \"" + file_error + "\" open failed\n";
        else
        {
            FILE_ERR = fd;
        }
    }
    else if(error_red==1)//������Ϣ�ض���׷�ӣ�
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
    //����dup2����ִ���ض���
    dup2(file_in, STDIN_FILENO);
    dup2(file_out, STDOUT_FILENO);
    dup2(file_err, STDERR_FILENO);
}

void RecoverDirection()
{
    //�ָ����ܵ��ض��� 
    dup2(STDIN_SAVE, STDIN_FILENO);
    dup2(STDOUT_SAVE, STDOUT_FILENO);
    dup2(STDERR_SAVE, STDERR_FILENO);
    dup2(STDOUT_FILENO, STDERR_FILENO);//������Ϣ�ض��򵽱�׼���

    FILE_IN = STDIN_SAVE;
    FILE_OUT = STDOUT_SAVE;
    FILE_ERR = STDERR_SAVE;
}

void Exec_input(string input)//���ܵ�����֣������������ε���Exec_singleִ��
{
    //���ո�ָ��ȡÿ������
    stringstream stringstr(input);//ʹ�ô���ʵ�ֶ�string�������������
    string temp;
    int words_num = 0;
    while(stringstr >> temp) //���ηֽ�Ϊÿ������
    {
        if(words_num==0)
            cmd = temp;
        else
            paras.push_back(temp);
        words_num++;
    }
    para_num = paras.size();

    vector<string> cmd_lines;//��˳�����ÿ������
    while(true)
    {
        int pos = input.find('|');
        if(pos==-1)
        {
            cmd_lines.push_back(input);//�� �� Ϊ�磬�ָ�����
            break;
        }
        cmd_lines.push_back(input.substr(0, pos));
        input = input.substr(pos+1, input.length()-pos-1);
    }
    
    //�ܵ�
    int pipe1[2] = {-1, -1};//��һ��ָ��Ĺܵ�����
    int pipe2[2] = {-1, -1};//��ǰ����Ĺܵ�����
    //cmd_lines�������δ����ҵ�����
    for(int i=0;i!=cmd_lines.size();i++)
    {
        //�ȶ�ÿ��ָ��Ԥ�����ض���
        Prepare();
        Pretreat(cmd_lines[i]);
        Redirect();
        //�ڴ����굥��ָ����ض���󣬽��йܵ��ض���
        //�ܵ��ض��򣺵�ǰָ�����������һ��ָ�����������ܸ���ָ�����ض���
        if(i==0)//��һ��ָ����벻��Ҫ���Թܵ�
        {
            pipe1[0] = FILE_IN;
            pipe1[1] = -1;
            pipe(pipe2);//���ɵ�ǰָ��Ĺܵ�
        }
        if(i==cmd_lines.size()-1)//���һ��ָ��������Ҫͨ��ܵ�
        {
            pipe2[0] = -1;
            pipe2[1] = FILE_OUT;
        }
        else//�м��ָ��
        {
            pipe(pipe2);//���ɵ�ǰָ��Ĺܵ�
        }
        //ִ�е�������ض����ļ���ܵ����
        Exec_single(pipe1[0], pipe2[1], FILE_ERR);
        //pipe2��ֵ��pipe1
        pipe1[0] = pipe2[0];
        pipe1[1] = pipe2[1];
        pipe2[0] = pipe2[1] = -1;
    }
}

void Exec_single(int file_in, int file_out, int file_err)
{
    if(error_msg!="")
    {  
        send_err_msg(error_msg);//��ָ���������׶η��ִ������������Ϣ������ִ��
        RecoverDirection();
        return;
    }

    bool last_bg = (para_num>=1 && paras[para_num-1]=="&");//last_bg��ʾָ���Ƿ���&��β
    bool is_fg = (cmd=="fg");//�Ƿ���fgָ��
    bool bg = last_bg || is_fg;//fgǿ�ƺ�ִ̨��
    //���ɴ�����ָ���ַ���
    string cmd_line = cmd;
    for(int i=0;i<para_num;i++)
    {
        if(last_bg && i==para_num-1)
            break;
        cmd_line += " "+ paras[i];
    }

    //���ݱ�׼�������
    STDIN_SAVE = dup(STDIN_FILENO);
    STDOUT_SAVE = dup(STDOUT_FILENO);
    STDERR_SAVE = dup(STDERR_FILENO);

    //��Ƕָ���ϵͳ������أ�ֱ��ִ�У��������ӽ���
    if(cmd=="exit" || cmd=="cd" || cmd=="set" || cmd=="unset" || cmd=="umask")
    {
        do_Redirect(file_in, file_out, file_err);//�������ض���
        if(last_bg)
        {
            paras.erase(paras.end()-1);
            para_num--;
            error_msg += "[Warning] add background identifier & for build-in command \"" + cmd +"\"\n";
        }
        Exec_cmd(cmd, paras);//ִ��
        RecoverDirection();//�ָ��ض���
        return;
    }

    pid_t pid = fork();//�����ӽ��̣�
    if(pid)//������
    {
        Process subpro(pid, bg?RUNNING_BG:RUNNING_FG, cmd_line);//���ɽ�����Ϣ
        if(bg==false)//ǰ̨����
        {
            *ptr_fg_pid = pid;
        }
        else if(is_fg)
        {
            *ptr_fg_pid = -2;
        }
        processes.push_back(subpro);//���̱�������
        setenv("PARENT", SHELLPATH.c_str(), 1);//���û������� 
    }
    else//�ӽ���
    {
        is_sub = true;
        setpgid(0, 0);//ʹ�ӽ��̵�����Ϊһ�������顣��̨�������Զ�����Ctrl+Z��Ctrl+C���ź�
        do_Redirect(file_in, file_out, file_err);//�ӽ������ض���
        if(last_bg)//ĩβ��&����̨�����ӽ���
        {
            paras.erase(paras.end()-1);
            para_num--;
            Exec_cmd(cmd, paras);   
        }
        else
        {
            Exec_cmd(cmd, paras);//ǰ̨�����ӽ���
        }
        //�ӽ���ִ����ϣ����ظ�����
        exit(0);
    }
    while(true)//�����fg���ȴ������
    {
        if(*ptr_fg_pid!=-2)
            break;
    }
    if(*ptr_fg_pid==-3)//fgָ��ִ��ʧ�ܣ�û�н��̱��ᵽǰ̨
    {
        *ptr_fg_pid = -1;
        return;
    }
    //����fg������Ϣ
    for(int i=0;i<processes.size();i++)
    {
        if(processes[i].pid_==*ptr_fg_pid && processes[i].st_!=RUNNING_FG)
            processes[i].st_ = RUNNING_FG;
    }
    //�ȴ�ǰ̨�������
    while(true)
    {
        if(*ptr_fg_pid==-1)
            break;
    }
    
    //�ָ����ܵ��ض��� 
    RecoverDirection();
}

void Exec_cmd(string cmd, vector<string> paras)
{
    bool exit = false;//�Ƿ�Ҫ�˳�myshell
    //����cmd����ִ�о���ָ�paras�ǲ�����
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
            //paras�ĵ�һ��Ԫ����ִ�е��ļ���
            //exec file para1 para2
            if(para_num==0)
            {
                error_msg += "[Error] exec: no input file\n";
                return;
            }
            //�����ӵڶ�����ʼ
            cmd = paras[0];
            paras.erase(paras.begin());
            para_num--;
            exit = true;
        }
        cmd_exec(cmd, paras);
        if(exit)//exec��ɺ��˳�myshell
            cmd_exit();
    }

    //(�ӽ�����)�����Ϣ
    send_output_msg(output);
    send_err_msg(error_msg);
}

void cmd_cd(vector<string>  dirs)
{
    if(para_num!=1)//cd��������ֻ��һ������
    {
        error_msg += "[Error] cd: No argument or too many arguments\n";
        return;
    }
    string dir = dirs[0];
    dir = GetRealString(dir);
    if(dir[0]=='~')//��Ŀ¼�ַ��滻
    {
        dir.erase(dir.begin());
        dir = HOMEDIR + dir;
    }
    if(chdir(dir.c_str())==-1)
    {
        //chdir����ʧ��
        error_msg += "[Error] cd: Unable to change directory to " + dir + "\n";
    }
    else
    {
        //chdir���óɹ�
        char buffer[MAXSIZE];
        getcwd(buffer, MAXSIZE);//��ȡ����ǰ����·��
        WD = buffer;
        //����WD��������
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
    //��ȡϵͳʱ����Ϣ
    time_t tt = time(NULL);
    struct tm * t = localtime(&tt);
    //��stringstream���ɷ�����Ϣ
    string Week[] = {"����һ", "���ڶ�", "������", "������", "������", "������", "������"};
    output += to_string(t->tm_year + 1900) + "��" + to_string(t->tm_mon + 1) + "��" 
    + to_string(t->tm_mday) + "��"+ " " + Week[t->tm_wday]
    + " " + to_string(t->tm_hour) + "ʱ" + to_string(t->tm_min) + "��" + to_string(t->tm_sec) + "��";
}

void cmd_echo(vector<string> txts)
{
    //���ո�ָ��
    for(int i=0;i<para_num;i++)
    {
        output += GetRealString(txts[i]);//����ÿ���ַ���
        if(i!=para_num-1)
            output += " ";
    }
}

void cmd_dir(vector<string> dirs)
{
    //�޲���Ĭ����ʾ����Ŀ¼����
    if(para_num==0)
    {
        paras.push_back(".");
        dirs=paras;
        para_num = 1;
    }
    for(int i=0;i<para_num;i++)
    {
        //��ȡ��ʵ·������ʾ
        string dir = GetRealString(dirs[i]);
        output += dir + ":\n";
        if(dir[0]=='~')//��Ŀ¼�ַ��滻
        {
            dir.erase(dir.begin());
            dir = HOMEDIR + dir;
        }
        //��ȡÿ���ļ�������
        vector<string> files;
        DIR* p_cur_dir;//��ǰĿ¼��ָ��
        struct dirent* ptr;
        if(!(p_cur_dir = opendir(dir.c_str())))
        {
            output += "Can not open directory \"" + dir + "\"\n";
        }
        else//���Ŀ¼�µ��ļ���
        {
            while((ptr = readdir(p_cur_dir)) != NULL)
            {
                files.push_back(ptr->d_name);
            }
            closedir(p_cur_dir);
            for(int i=0;i<files.size();i++)
            {
                if(files[i]=="." || files[i]=="..")//����� . �� .. 
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
    extern char** environ;//��ȡ����������
    //�޲�����ʾȫ����������
    if(para_num==0)
    {
        for(int i=0;environ[i]!=NULL;i++)
        {
            if(i!=0)
                output += "\n";
            output += (string)environ[i];
        }
    }
    //һ�����������ò�������Ϊ��������������ʾ
    else if(para_num==1)
    {
        if(getenv(paras[0].c_str())==NULL)
            output += "No environment variable named \"" + paras[0] + "\"";
        else
            output += paras[0] + "=" + getenv(paras[0].c_str());
    }
    //����������set var val�����û���������ֵ�����var���������½�
    else if(para_num==2)
    {
        if(getenv(paras[0].c_str())==NULL)//�����»�������
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
        else//�޸��Ѵ��ڵĻ�������
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
    //����ɾ��ÿ����������
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
    //�޲�����ֱ�����umaskֵ
    if(para_num==0)
    {
        mode_t mode = umask(0);//mode����umask��ֵ��umask���ĳ�0
        umask(mode);//�ָ�umask��ֵ
        //��8���Ƶ���ʽ���umask
        output += to_string((mode >> 9) & 7) + to_string((mode >> 6) & 7)
                 + to_string((mode >> 3) & 7) + to_string(mode & 7);
    }
    //һ������������ֵ
    else if(para_num==1)
    {
        string umask_str = paras[0];
        if(umask_str.length()>=5)
        {
            error_msg += "[Error] umask: too many digits for umask\n";
            return;
        }
        char* temp = NULL;
        //8����ת��
        mode_t mode = strtol(umask_str.c_str(), &temp, 8);
        if(*temp!='\0')//�����umaskֵ�г����˷�0��7���ַ�
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
    system(((string)"sleep " + sec[0]).c_str());//ϵͳ˯��
}

void cmd_exec(string file, vector<string> paras)
{
    //���ò���
    char* arg[MAXSIZE];
    string strs[MAXSIZE];
    arg[0] = const_cast<char *>(file.c_str());
    //���ɲ�����
    for (int i = 0; i < para_num; i++)
    {
        strs[i] = GetRealString(paras[i]);
        arg[i+1] = const_cast<char *>(strs[i].c_str());
    }
    arg[para_num+1] = NULL;
    //ִ�У�execvp��
    if(execvp(file.c_str(), arg)<0)//execvp����ִ���ⲿ��������
    {
        string input = file;
        for(int i=0;i<paras.size();i++)
            input += " " + paras[i];
        error_msg += "[Error] " + input + ": command or program not found";
        return;
    }
    //execִ�гɹ�����˳�Դ���������ִ�е��������䣬˵��exec����
    error_msg += "[Error] exec: unable to execute \""+ file + "\"\n";
}

void cmd_fg(vector<string> jobs)
{
    if(para_num != 1)//fg��������ֻ��һ��pid��Ϊ����
    {
        error_msg += "[Error] fg: only support exactly one argument\n";
        *ptr_fg_pid = -3;
        return;
    }

    pid_t pid = atoi(paras[0].c_str());
    bool exist = false;
    //�ҵ��ý��̲��ᵽǰ̨
    for(int i=0;i<processes.size();i++)
    {
        if(processes[i].pid_ == pid)
        {
            if(processes[i].st_==SUSPENDED)//SUSPENDED->FG
            {
                *ptr_fg_pid = pid;
                kill(-pid, SIGCONT);//���ͼ������̵��ź�
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
    //�ҵ��ý��̲���̨��������
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
                    //����̨����Ľ��̷���SIGCONT�ź�
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
    //��ֹ�����ӽ��̣���ֹ��ʬ����
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
    if(para_num==0)//�޲�������ʾ���н���
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
    else//�ҵ�����ʾָ�����ƵĽ���
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
    //�ҵ���ɱ��ָ���ĺ�̨����
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
                    //����SIGINT�ź���ֹ����
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
    //�ҵ�������ָ���ĺ�̨����
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
                    //����SIGTSP�źŹ������
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
    if (para_num == 3){//��Ԫ����������ҽ���3������
        string val1 = GetRealString(paras[0]);
        string val2 = GetRealString(paras[2]);
        if (paras[1] == "-ef"){//�ļ�1���ļ�2���豸��inode��ͬ
            struct stat buf1, buf2;
            int ret1 = lstat(val1.c_str(), &buf1);
            int ret2 = lstat(val2.c_str(), &buf2);
            if (ret1 == 0 && ret2 == 0 && buf1.st_dev == buf2.st_dev && buf1.st_ino == buf2.st_ino) output +="true\n";
            else output +="false\n";
        }else if (paras[1] == "-nt"){//�ļ�1���ļ�2����
            struct stat buf1, buf2;
            int ret1 = lstat(val1.c_str(), &buf1);
            int ret2 = lstat(val2.c_str(), &buf2);
            if (ret1 == 0 && ret2 == 0 && cmp_time(buf1.st_mtime, buf2.st_mtime) == 1) output +="true\n";
            else output +="false\n";
        }else if (paras[1] == "-ot"){//�ļ�1���ļ�2����
            struct stat buf1, buf2;
            int ret1 = lstat(val1.c_str(), &buf1);
            int ret2 = lstat(val2.c_str(), &buf2);
            if (ret1 == 0 && ret2 == 0 && cmp_time(buf1.st_mtime, buf2.st_mtime) == -1) output +="true\n";
            else output +="false\n";
        }else if (paras[1] == "="){//�ַ������
            if (val1 == val2) output +="true\n";
            else output +="false\n";
        }else if (paras[1] == "!="){//�ַ�������
            if (val1 != val2) output +="true\n";
            else output +="false\n";
        }
        else
        {
            int Value1, Value2;
            Value1 = atoi(val1.c_str());
            Value2 = atoi(val2.c_str());
            if (paras[1] == "-eq"){//����==
                if (Value1 == Value2) output +="true\n";
                else output +="false\n";
            }
            else if (paras[1] == "-ge"){//����>=
                if (Value1 >= Value2) output +="true\n";
                else output +="false\n";
            }
            else if (paras[1] == "-gt"){//����>
                if (Value1 > Value2) output +="true\n";
                else output +="false\n";
            }
            else if (paras[1] == "-le"){//����<=
                if (Value1 <= Value2) output +="true\n";
                else output +="false\n";
            }
            else if (paras[1] == "-lt"){//����<
                if (Value1 < Value2) output +="true\n";
                else output +="false\n";
            }
            else if (paras[1] == "-ne"){//����!=
                if (Value1 != Value2) output +="true\n";
                else output +="false\n";
            }
            else{//�޷�ʶ��������
                error_msg += "[Error] test: Unknown option " + paras[1] + "\n";
            }
        }
    }
    else if (para_num== 2){//һԪ����������ҽ���2������
        string val = GetRealString(paras[1]);
        if (paras[0] == "-e"){//�ļ�����
            struct stat buf;
            int ret = lstat(val.c_str(), &buf);
            if (ret == 0) output +="true\n";
            else output +="false\n";
        }else if (paras[0] == "-r"){//�ļ��ɶ�
            struct stat buf;
            int ret = lstat(val.c_str(), &buf);
            if (ret == 0 && access(val.c_str(), R_OK)) output +="true\n";
            else output +="false\n";
        }else if (paras[0] == "-w"){//�ļ���д
            struct stat buf;
            int ret = lstat(val.c_str(), &buf);
            if (ret == 0 && access(val.c_str(), W_OK)) output +="true\n";
            else output +="false\n";
        }else if (paras[0] == "-x"){//�ļ���ִ��
            struct stat buf;
            int ret = lstat(val.c_str(), &buf);
            if (ret == 0 && access(val.c_str(), X_OK)) output +="true\n";
            else output +="false\n";
        }else if (paras[0] == "-s"){//�ļ�������һ���ַ�
            struct stat buf;
            int ret = lstat(val.c_str(), &buf);
            if (ret == 0 && buf.st_size) output +="true\n";
            else output +="false\n";
        }else if (paras[0] == "-d"){//�ļ�ΪĿ¼
            struct stat buf;
            int ret = lstat(val.c_str(), &buf);
            if (ret == 0 && S_ISDIR(buf.st_mode)) output +="true\n";
            else output +="false\n";
        }else if (paras[0] == "-f"){//�ļ�Ϊ��ͨ�ļ�
            struct stat buf;
            int ret = lstat(val.c_str(), &buf);
            if (ret == 0 && S_ISREG(buf.st_mode)) output +="true\n";
            else output +="false\n";
        }else if (paras[0] == "-c"){//�ļ�Ϊ�ַ��������ļ�
            struct stat buf;
            int ret = lstat(val.c_str(), &buf);
            if (ret == 0 && S_ISCHR(buf.st_mode)) output +="true\n";
            else output +="false\n";
        }else if (paras[0] == "-b"){//�ļ�Ϊ�������ļ�
            struct stat buf;
            int ret = lstat(val.c_str(), &buf);
            if (ret == 0 && S_ISBLK(buf.st_mode)) output +="true\n";
            else output +="false\n";
        }else if (paras[0] == "-h" || paras[0] == "-L"){//�ļ�Ϊ��������
            struct stat buf;
            int ret = lstat(val.c_str(), &buf);
            if (ret == 0 && S_ISLNK(buf.st_mode)) output +="true\n";
            else output +="false\n";
        }else if (paras[0] == "-p"){//�ļ�Ϊ�����ܵ�
            struct stat buf;
            int ret = lstat(val.c_str(), &buf);
            if (ret == 0 && S_ISFIFO(buf.st_mode)) output +="true\n";
            else output +="false\n";
        }else if (paras[0] == "-S"){//�ļ�ΪǶ����
            struct stat buf;
            int ret = lstat(val.c_str(), &buf);
            if (ret == 0 && S_ISSOCK(buf.st_mode)) output +="true\n";
            else output +="false\n";
        }else if (paras[0] == "-G"){//�ļ���ʵ����ӵ��
            struct stat buf;
            int ret = lstat(val.c_str(), &buf);
            if (ret == 0 && buf.st_gid == getgid()) output +="true\n";
            else output +="false\n";
        }else if (paras[0] == "-O"){//�ļ���ʵ���û�ӵ��
            struct stat buf;
            int ret = lstat(val.c_str(), &buf);
            if (ret == 0 && buf.st_uid == getuid()) output +="true\n";
            else output +="false\n";
        }else if (paras[0] == "-g"){//�ļ���������λ
            struct stat buf;
            int ret = lstat(val.c_str(), &buf);
            if (ret == 0 && (S_ISGID & buf.st_mode)) output +="true\n";
            else output +="false\n";
        }else if (paras[0] == "-u"){//�ļ��������û�λ
            struct stat buf;
            int ret = lstat(val.c_str(), &buf);
            if (ret == 0 && (S_ISUID & buf.st_mode)) output +="true\n";
            else output +="false\n";
        }else if (paras[0] == "-k"){//�ļ�������ճ��λ
            struct stat buf;
            int ret = lstat(val.c_str(), &buf);
            if (ret == 0 && (S_ISVTX & buf.st_mode)) output +="true\n";
            else output +="false\n";
        }else if (paras[0] == "-n"){//�ַ������ȷ�0
            if (val.length()) output +="true\n";
            else output +="false\n";
        }else if (paras[0] == "-z"){//�ַ�������Ϊ0
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
    string Target;//��������������Ĭ��Ϊmyshell����ʾ�鿴�û��ֲᣩ
    if (para_num == 0) Target = "myshell";//���޲�, ���ȫ�ְ����ֲ�
    else Target = paras[0];//���������Ӧָ��İ����ֲ�

    //�򿪰����ļ�
    ifstream ifs(HELPPATH);
    if (!ifs.is_open()){
        error_msg += "[Error] help: Help manual file is not found\n";
        return;
    }
    string line;//һ�е���Ϣ
    while(!ifs.eof())//���ҵ���Ӧ������
    {
        getline(ifs, line);
        if(line[0]=='#')
        {
            line = line.substr(1, line.rfind('#')-1);
            // #Target# ��ʾһ���µİ�������
            if(line==Target)
            {
                //��ʼ�����Ӧ���ֵ���ʾ��Ϣ
                string msg_line;
                while(!ifs.eof())//���ж�ȡ�������
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
        kill(-pid, SIGINT);//��ֹǰ̨������
    }
    else//��ǰ̨�ӽ��̣���ֹshell����
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
        kill(-pid, SIGTSTP);//����ǰ̨������
    }
    else//��ǰ̨�ӽ��̣�����shell����
    {
        send_err_msg("You can not suspend the shell!");
    }
}

//�ӽ���״̬�ı��źŵĴ���������Ҫ��
void sighandle_chld(int sig)
{
    pid_t pid = -1;
    int status;//����״̬
    //�������źź�waitpid������ȵ�����״̬�ı�
    while((pid = waitpid(-1, &status, WNOHANG|WUNTRACED|WCONTINUED)) > 0)
    {
        for(int i=0;i<processes.size();i++)
        {
            if(processes[i].pid_ == pid)
            {
                bool is_fg = (processes[i].st_==RUNNING_FG);
                //����״̬������̨�ӽ��̱�ĸ���
                if(WIFEXITED(status))//���̽���
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
                else if(WIFSIGNALED(status))//���̱���ֹ
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
                else if(WIFSTOPPED(status))//���̱�����ֹͣ��
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
                else if(WIFCONTINUED(status))//���̼���ִ��
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
                else//�������
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

int cmp_time(const time_t& t1, const time_t& t2)
{
    //���αȽ�ʱ����
    if (t1 < t2)
        return -1;
    if (t1 > t2)
        return 1;
    if (t1 < t2)
        return -1;
    if (t1 > t2)
        return 1;
    return 0;
}


void erase_side_spaces(string &str)
{
    //�Ƴ�����ո�
    str.erase(0,str.find_first_not_of(" "));
    str.erase(str.find_last_not_of(" ") + 1);
}

string GetRealString(string txt)
{
    //txt�Ѿ�������ȥ�ո���
    string ret;
    if(txt.length()<=1)
        return txt;
    //form�ַ�����ʽ: 0�����ţ� 1�����ţ� 2˫���� 
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
    
    if(form==1)//�����Ų������ַ����ڵ�����
        return ret;
    //�����Ż�˫����,����$��ͷʱ������$�������
    if(ret.front()=='$')
    {
        if(ret.length()==2 && ret[1]>='1' && ret[1]<='9')//������֧��$1~$9
        {
            int para_index = ret[1] - '0';
            if(para_index<argc)
            {
                ret = argv[para_index];
            }
            else
                ret = "";
        }
        else if(ret=="$#")//��������
        {
            ret = to_string(argc - 1);
        }
        else//������������
        {
            ret = getenv(ret.substr(1).c_str());
        }
    }
    return ret;
}

void send_output_msg(string msg)
{
    //д���׼��������ܱ��ض���
    write(STDOUT_FILENO, msg.c_str(), msg.length()); 
}

void send_err_msg(string msg)
{
    //д���׼���󣬿��ܱ��ض���
    write(STDERR_FILENO, msg.c_str(), msg.length());
}

void send_terminal_msg(string msg)
{
    //д���׼��������ܱ��ض���
    write(STDOUT_FILENO, msg.c_str(), msg.length()); 
}