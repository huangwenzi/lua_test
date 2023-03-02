#include <string>
#include <list>
#include <vector>
#include <map>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <iostream>
#include <string>
#include <cstdlib>
#include <cstring>
#include <typeinfo>
#include <stdio.h>
#include <time.h>
#include <stdarg.h>
#include <assert.h>
#include <math.h>
#include <signal.h>
#include <errno.h>
#include <unordered_map>
#include <fcntl.h>
#include <sstream>
#include <algorithm>
#include <vector>
#include <unordered_set>
#include <set>
#include<ostream>
#include <map>
#include <sys/time.h>
#include <unistd.h>

extern "C" {
  #include "src/lua.h"
  #include "src/lualib.h"
  #include "src/lauxlib.h"
}

using namespace std;

lua_State *gL;

// һЩ����
static const int MAX_STACK_SIZE = 64;
static const int MAP_KEY_LEN = 60;
struct CallStack {
    int count;
    int depth;
    int stack[MAX_STACK_SIZE];
};

// ������Ϣ
struct RunInfo {
    char map_key[255];      // ������
    long long time_us;      // ��ʱ
    int time_us;            // ���д���
    int fun_begin_num;      // �ڼ�����ʼʱ��
    long long fun_begin_time[255];  // ������ʼ��ʱ��
};

// ȫ�ֱ���
int g_push_count = 0;             // �������
FILE *g_push_fp = NULL;           // �ļ�����
// #define MAP_RUN_STRUCT map<std::string, long long>
#define MAP_RUN_STRUCT map<std::string, TimeFun>
MAP_RUN_STRUCT g_map_run_info;    // ��������ʱ�� <������, ����ʱ��(����)>
long long g_time_us;            // �ϴμ����ʱ��
char g_last_map_key[255];       // �ϴ�map_key
bool g_run = false;            // �Ƿ�ֹͣ



// ��ȡ��ǰʱ��
static long long get_time_us(){
    struct timeval tv;
    gettimeofday(&tv, NULL);
    long long time_us = tv.tv_sec*1000*1000  + tv.tv_usec;
    return time_us;
}

// ����������Ϣ
static int push_run(lua_State *L, lua_Debug *ar){
    g_push_count ++;
    if (ar->name != NULL){
        fprintf(g_push_fp, "\ng_push_count:%d\n", g_push_count);
        // д�������Ϣ
        fprintf(g_push_fp, "name:%s, namewhat:%s, what:%s, source:%s\n", ar->name, ar->namewhat, ar->what, ar->source);
        fprintf(g_push_fp, "currentline:%d, linedefined:%d, lastlinedefined:%d\n", ar->currentline, ar->linedefined, ar->lastlinedefined);
        
        // д��map��Ϣ
        char map_key[255];
        sprintf(map_key, "%s-%s-%s-%d", ar->what, ar->source, ar->name, ar->currentline);
        MAP_RUN_STRUCT::iterator iter;
        // �ϴ�ʱ��
        long long time_us = get_time_us();
        iter = g_map_run_info.find(map_key);
        if(iter != g_map_run_info.end()){
            iter->second += time_us - g_time_us;
        }else{
            g_map_run_info[map_key] = time_us - g_time_us;
        }
        g_time_us = time_us;
    }
    return 1;
}

// ������
bool comp(TimeFun x, TimeFun y) {
	return x.time_us < y.time_us;
}

// ����run_map��Ϣ
static int push_map_info() {
    // ���������б�
    list<TimeFun> sort_list;
    for(MAP_RUN_STRUCT::iterator iter = g_map_run_info.begin();iter!=g_map_run_info.end();iter++){
        TimeFun tmp;
        tmp.time_us = iter->second;
        strcpy(tmp.map_key, iter->first.c_str());
        sort_list.push_back(tmp);
    }
    sort_list.sort(comp);
    
    // ������ļ���
    fprintf(g_push_fp, "\npush_map_info\n");
    for (list<TimeFun>::iterator iter = sort_list.begin(); iter != sort_list.end(); ++iter){
        fprintf(g_push_fp, "%s", iter->map_key);
        // �Ƿ񲹿ո�
        int map_key_len = strlen(iter->map_key);
        if (map_key_len < MAP_KEY_LEN){
            char add_char[100];
            size_t i = 0;
            for (; i < MAP_KEY_LEN - map_key_len; i++){
                add_char[i] = ' ';
            }
            add_char[i] = '\0';
            fprintf(g_push_fp, add_char);
        }
        fprintf(g_push_fp, "%llu\n", iter->time_us);
    }
    return 1;
}

extern "C" int lrealstop(lua_State *L) {
    lua_sethook(L, 0, 0, 0);
    g_run = true;

    // �ر�log
    push_map_info();
    fclose(g_push_fp);
    return 0;
}

// ��ȡmap_key
static int get_map_key(lua_Debug *ar){
    char map_key[255];
    sprintf(map_key, "%s-%s-%s", ar->what, ar->source, ar->name);
    return map_key;
}

// call�ź�
static void SignalHandlerHookCall(lua_State *L, lua_Debug *par) {
    // ��ȡ��ǰ��ջ��Ϣ
    lua_Debug ar;
    lua_getstack(L, 0, &ar);
    lua_getinfo(L, "Slnt", &ar);
    
    // �Ƿ��Ѵ���
    char *map_key = get_map_key(lua_Debug *ar);
    MAP_RUN_STRUCT::iterator iter;
    iter = g_map_run_info.find(map_key);
    if(iter != g_map_run_info.end()){
        
        iter->second += time_us - g_time_us;
    }else{
        g_map_run_info[map_key] = time_us - g_time_us;
    }


    RunInfo tmp_info;



    push_run(L, &ar);   // ��һ������
}

// ret�ź�
static void SignalHandlerHookRet(lua_State *L, lua_Debug *par) {
    // ��ȡ��ǰ��ջ��Ϣ
    lua_Debug ar;
    lua_getstack(L, 0, &ar);
    lua_getinfo(L, "Slnt", &ar);
    push_run(L, &ar);   // ��һ������
}

static void SignalHandler(int sig, siginfo_t *sinfo, void *ucontext) {
    // LUA_MASKCALL�� LUA_MASKRET�� LUA_MASKLINE�� LUA_MASKCOUNT, ���� count ֻ�������а����� LUA_MASKCOUNT ��������
    // call hook: �ڽ���������һ������ʱ�����á� ���ӽ��� Lua ����һ���º����� ������ȡ����ǰ�����á�
    // return hook: �ڽ�������һ�������з���ʱ���á� ���ӽ��� Lua �뿪����֮ǰ����һ�̱����á� û�б�׼���������ʱ��������ص���Щֵ��
    // line hook: �ڽ�����׼����ʼִ���µ�һ�д���ʱ�� ������ת�����д�����ʱ����ʹ��ͬһ������ת�������á� ������¼������� Lua ִ��һ�� Lua ����ʱ��������
    // count hook: �ڽ�����ÿִ�� count ��ָ��󱻵��á� ������¼������� Lua ִ��һ�� Lua ����ʱ��������
    lua_sethook(gL, SignalHandlerHookCall, LUA_MASKCALL, 1);
    lua_sethook(gL, SignalHandlerHookRet, LUA_MASKRET, 1);
}

extern "C" int lrealstart(lua_State *L, int second, const char *file) {
    // ���ļ�
    if (!g_push_fp){
        g_push_fp = fopen("./push_log.txt", "w+");
        fprintf(g_push_fp, "open file...\n");
    }
    
    // ���в���
    if (g_run) {
        return -1;
    }

    // ��ʼҪ�õ�������
    g_time_us = get_time_us();
    g_run = true;
    gL = L;
    SignalHandler();
    return 0;
}

static int lstart(lua_State *L) {
    int second = (int) lua_tointeger(L, 1);
    const char *file = lua_tostring(L, 2);

    int ret = lrealstart(L, second, file);
    lua_pushinteger(L, ret);
    return 1;
}

static int lstop(lua_State *L) {
    int ret = lrealstop(L);

    lua_pushinteger(L, ret);
    return 1;
}

extern "C" int luaopen_libplua(lua_State *L) {
    luaL_checkversion(L);
    luaL_Reg l[] = {
            {"start", lstart},
            {"stop",  lstop},
            {NULL, NULL},
    };
    luaL_newlib(L, l);
    return 1;
}
