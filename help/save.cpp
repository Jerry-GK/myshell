
//��������myshell
//���ߡ�ѧ�ţ��ܼ��� 3200102557

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
#define MAXSIZE 1024
#define PARENTPATH "\\bin\\zsh"

using namespace std;
//����״̬
enum State
{
    RUNNING,//����
    SUSPENDED,//����
    KILLED,//��ǿ����ֹ
    DONE//��������
};

//
class Process
{
public:
    Process(pid_t pid, State st, string name):pid_(pid),st_(st),cmd_line_(name){}
    string show_msg()
    {
        string st_str;
        if(st_==RUNNING)
            st_str = "running";
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
    string cmd_line_;//������
};

//ȫ�ֱ���
char buffer[MAXSIZE] = {0};
string input;
string WD;//working directory��ǰ����Ŀ¼
string HOMEDIR;//��Ŀ¼
string SHELLPATH;//shell��ִ�г����·��
string promt;//��������ʾ��
string output;//�������
string error_msg;//������Ϣ���
string cmd;//ָ������
vector<string> paras;//ָ�������
int para_num;//ָ���������
int argc;
vector<string> argv;
vector<Process> pros;//������

//��������
void Init(int argc_, char* argv_[]);
void Prepare();
void Pretreat(string input);
void Exec_cmd(string cmd, vector<string> para, bool sub);
void Exec(string input);
void erase_side_spaces(string &str);//ɾ���ַ�������Ŀո�
string GetRealString(string txt);//���ַ������д������ܴ���ȥӡ�š��滻����
//����ִ�к���
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
void cmd_exit();

//�źŴ�����
void sighandle_chld(int sig);

//main����
int main(int Argc, char * Argv[])
{
    signal(SIGCHLD, sighandle_chld);
    Init(Argc, Argv);
    int Flag = 1;
    while(Flag)
    {   
        int i;
        Prepare();

        cout<<"\n\nmyshell > "<<WD<<" $ ";
        getline(cin,input);
        Exec(input);
        cout<<output;
        cout<<error_msg;
    }
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

    //��ȡ���������·��
    char buffer[MAXSIZE];
    uint32_t len = MAXSIZE;
    _NSGetExecutablePath(buffer, &len);
    // int len = readlink("/proc/self/exe", buffer, MAXSIZE);//��ȡ����λ��
    //buffer[len] = '\0';

    // memcpy(buffer, argv[0].c_str(),argv[0].size()+1);
    setenv("SHELL", buffer, 1);
    SHELLPATH = buffer;
    //���ø����̵�·��
    setenv("PARENT", PARENTPATH, 1);
}

void Prepare()
{
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
    if(para_num>=1 && paras[para_num-1]=="&")//&��β����̨����
    {
        //�޸Ĳ����飬ȥ��&
        paras.erase(paras.end()-1);
        para_num--;
        pid_t pid = fork();
        if(pid)//������
        {
            Process subpro(pid, RUNNING, cmd);
            pros.push_back(subpro);//�������̱�
            setenv("PARENT", SHELLPATH.c_str(), 1);//���û������� 
            //���̱����signal���������Զ�����
        }
        else//�ӽ���
        {
            setpgid(0, 0);//ʹ�ӽ��̵�����Ϊһ�������顣��̨�������Զ�����Ctrl+Z��Ctrl+C���ź�
            Exec_cmd(cmd, paras, true);   
            //�ӽ���ִ����ϣ����ظ�����
            exit(0);
        }
    }
    else//����ǰִ̨��
    {
        Exec_cmd(cmd, paras, false);
    }
}

void Exec_cmd(string cmd, vector<string> paras, bool sub)
{
    //cout<<"exec: cmd = "<<cmd<<" para = "<<para<<endl;

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
    else if(cmd=="")
    {
        output = "";
    }
    //exec��������ָ��������(�൱��ǰ�����exec)��
    //�����ʱ�Ǹ����̣�sub=false�����������ӽ��̲�ǰ̨���С�
    //������ӽ��̣�sub=true����˵��ĩβ����&��ֱ�Ӻ�̨���м���
    else
    {
        if(cmd=="exec")
        {
            //paras�ĵ�һ��Ԫ����ִ�е��ļ���
            //exec file para1 para2
            if(para_num==0)
            {
                error_msg = "[Error]: exec: no input file";
                return;
            }
            //�����ӵڶ�����ʼ
            cmd = paras[0];
            paras.erase(paras.begin());
            para_num--;
        }

        if(sub == false)//�������У������ӽ��̲�ǰ̨����
        {
            //fork�������̿���һ�ݱ���ӽ���
            pid_t pid = fork();
            if (pid == 0)
            {   
                //�ӽ���
                //����PARENT��������
                setenv("PARENT", SHELLPATH.c_str(), 1);
                cmd_exec(cmd, paras);
                //�����ִ�е���һ����˵��exec����
                exit(0);
            }
            //�����̵ȴ��ӽ������
            while (pid != -1 && !waitpid(pid, NULL, WNOHANG));
            pid = -1;
        }
        else if(sub == true)//�Լ���ͨ��&���ɵ��ӽ����У��������м���
        {
            cmd_exec(cmd, paras);
        }
    }
}

void cmd_cd(vector<string>  dirs)
{
    if(para_num!=1)
    {
        error_msg = "[Error]: cd: No argument or too many arguments";
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
        error_msg = "[Error]: cd: Unable to change directory to " + dir;
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
    //��stringstream���ɷ�����Ϣ
    string Week[] = {"����һ", "���ڶ�", "������", "������", "������", "������", "������"};
    output = to_string(t->tm_year + 1900) + "��" + to_string(t->tm_mon + 1) + "��" 
    + to_string(t->tm_mday) + "��"+ " " + Week[t->tm_wday]
    + " " + to_string(t->tm_hour) + "ʱ" + to_string(t->tm_min) + "��" + to_string(t->tm_sec) + "��";
}

void cmd_echo(vector<string> txts)
{
    for(int i=0;i<para_num;i++)
    {
        output += GetRealString(txts[i]);
        if(i!=para_num-1)
            output += " ";
    }
    cout<<output;
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
            output = "Can not open directory \"" + dir + "\"\n";
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
        error_msg = "[Error]: set: too many arguments!";
    }
}

void cmd_unset(vector<string> vars)
{
    //����ɾ��ÿ����������
    if(para_num==0)
    {
        error_msg = "[Error]: unset: not enough arguments";
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
        output = to_string((mode >> 9) & 7) + to_string((mode >> 6) & 7)
                 + to_string((mode >> 3) & 7) + to_string(mode & 7);
    }
    //һ������������ֵ
    else if(para_num==1)
    {
        string umask_str = paras[0];
        if(umask_str.length()>=5)
        {
            error_msg = "[Error]: umask: too many digits for umask";
            return;
        }
        char* temp = NULL;
        mode_t mode = strtol(umask_str.c_str(), &temp, 8);
        if(*temp!='\0')//�����umaskֵ�г����˷�0��7���ַ�
        {
            error_msg = "[Error]: umask: invalid umask expression";
            return;
        }
        umask(mode);
        output = "set: umask=" + to_string((mode >> 9) & 7) + to_string((mode >> 6) & 7)
            + to_string((mode >> 3) & 7) + to_string(mode & 7);
    }
    else
    {
        error_msg = "[Error]: umask: too many arguments";
    }
}

void cmd_ssleep(vector<string> sec)
{
    if(para_num!=1)
    {
        error_msg = "[Error]: sleep: need exactly one argument";
        return;
    }
    system(((string)"sleep " + sec[0]).c_str());    
}

void cmd_exec(string file, vector<string> paras)
{
    //���ò���
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
    //ִ��
    execvp(file.c_str(), arg);
    //execִ�гɹ�����˳�Դ���������ִ�е��������䣬˵��exec����
    error_msg = "[Error]: exec: unable to execute \""+ file + "\"";
}

void cmd_exit()
{
    exit(0);
}

void erase_side_spaces(string &str)
{
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
    if(para_num==0)//�޲�������ʾ���н���
    {
        for(int i=0;i<pros.size();i++)
        {
            if(!first)
            {
                output += "\n";
                first = false;
            }
            output += pros[i].show_msg();
        }
    }
    else//��ʾָ���Ľ���
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

void sighandle_chld(int sig)
{
    pid_t pid;
    int status;
    while((pid = waitpid(-1, &status, WNOHANG)) > 0)
    {
        for(int i=0;i<pros.size();i++)
        {
            if(pros[i].pid_ == pid)
            {
                //����״̬������̨�ӽ��̱�ĸ���
                if(WIFEXITED(status))
                    pros[i].st_ = DONE;
                else if(WIFSIGNALED(status))
                    pros[i].st_ = KILLED;
                else if(WIFSTOPPED(status))
                    pros[i].st_ = SUSPENDED;
                cout<<"\n"<<pros[i].show_msg();
            }
        }
    }
}

