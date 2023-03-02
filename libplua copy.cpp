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

// 一些定义
static const int MAX_STACK_SIZE = 64;
static const int MAP_KEY_LEN = 60;
struct CallStack {
    int count;
    int depth;
    int stack[MAX_STACK_SIZE];
};

// 运行信息
struct RunInfo {
    char map_key[255];      // 函数名
    long long time_us;      // 耗时
    int time_us;            // 运行次数
    int fun_begin_num;      // 第几个开始时间
    long long fun_begin_time[255];  // 函数开始的时间
};

// 全局变量
int g_push_count = 0;             // 保存次数
FILE *g_push_fp = NULL;           // 文件对象
// #define MAP_RUN_STRUCT map<std::string, long long>
#define MAP_RUN_STRUCT map<std::string, TimeFun>
MAP_RUN_STRUCT g_map_run_info;    // 函数运行时间 <函数名, 运行时间(毫秒)>
long long g_time_us;            // 上次计算的时间
char g_last_map_key[255];       // 上次map_key
bool g_run = false;            // 是否停止



// 获取当前时间
static long long get_time_us(){
    struct timeval tv;
    gettimeofday(&tv, NULL);
    long long time_us = tv.tv_sec*1000*1000  + tv.tv_usec;
    return time_us;
}

// 保存运行信息
static int push_run(lua_State *L, lua_Debug *ar){
    g_push_count ++;
    if (ar->name != NULL){
        fprintf(g_push_fp, "\ng_push_count:%d\n", g_push_count);
        // 写入调试信息
        fprintf(g_push_fp, "name:%s, namewhat:%s, what:%s, source:%s\n", ar->name, ar->namewhat, ar->what, ar->source);
        fprintf(g_push_fp, "currentline:%d, linedefined:%d, lastlinedefined:%d\n", ar->currentline, ar->linedefined, ar->lastlinedefined);
        
        // 写入map信息
        char map_key[255];
        sprintf(map_key, "%s-%s-%s-%d", ar->what, ar->source, ar->name, ar->currentline);
        MAP_RUN_STRUCT::iterator iter;
        // 上次时间
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

// 排序函数
bool comp(TimeFun x, TimeFun y) {
	return x.time_us < y.time_us;
}

// 保存run_map信息
static int push_map_info() {
    // 生成排序列表
    list<TimeFun> sort_list;
    for(MAP_RUN_STRUCT::iterator iter = g_map_run_info.begin();iter!=g_map_run_info.end();iter++){
        TimeFun tmp;
        tmp.time_us = iter->second;
        strcpy(tmp.map_key, iter->first.c_str());
        sort_list.push_back(tmp);
    }
    sort_list.sort(comp);
    
    // 输出到文件中
    fprintf(g_push_fp, "\npush_map_info\n");
    for (list<TimeFun>::iterator iter = sort_list.begin(); iter != sort_list.end(); ++iter){
        fprintf(g_push_fp, "%s", iter->map_key);
        // 是否补空格
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

    // 关闭log
    push_map_info();
    fclose(g_push_fp);
    return 0;
}

// 获取map_key
static int get_map_key(lua_Debug *ar){
    char map_key[255];
    sprintf(map_key, "%s-%s-%s", ar->what, ar->source, ar->name);
    return map_key;
}

// call信号
static void SignalHandlerHookCall(lua_State *L, lua_Debug *par) {
    // 获取当前堆栈信息
    lua_Debug ar;
    lua_getstack(L, 0, &ar);
    lua_getinfo(L, "Slnt", &ar);
    
    // 是否已存在
    char *map_key = get_map_key(lua_Debug *ar);
    MAP_RUN_STRUCT::iterator iter;
    iter = g_map_run_info.find(map_key);
    if(iter != g_map_run_info.end()){
        
        iter->second += time_us - g_time_us;
    }else{
        g_map_run_info[map_key] = time_us - g_time_us;
    }


    RunInfo tmp_info;



    push_run(L, &ar);   // 存一次数据
}

// ret信号
static void SignalHandlerHookRet(lua_State *L, lua_Debug *par) {
    // 获取当前堆栈信息
    lua_Debug ar;
    lua_getstack(L, 0, &ar);
    lua_getinfo(L, "Slnt", &ar);
    push_run(L, &ar);   // 存一次数据
}

static void SignalHandler(int sig, siginfo_t *sinfo, void *ucontext) {
    // LUA_MASKCALL， LUA_MASKRET， LUA_MASKLINE， LUA_MASKCOUNT, 参数 count 只在掩码中包含有 LUA_MASKCOUNT 才有意义
    // call hook: 在解释器调用一个函数时被调用。 钩子将于 Lua 进入一个新函数后， 函数获取参数前被调用。
    // return hook: 在解释器从一个函数中返回时调用。 钩子将于 Lua 离开函数之前的那一刻被调用。 没有标准方法来访问被函数返回的那些值。
    // line hook: 在解释器准备开始执行新的一行代码时， 或是跳转到这行代码中时（即使在同一行内跳转）被调用。 （这个事件仅仅在 Lua 执行一个 Lua 函数时发生。）
    // count hook: 在解释器每执行 count 条指令后被调用。 （这个事件仅仅在 Lua 执行一个 Lua 函数时发生。）
    lua_sethook(gL, SignalHandlerHookCall, LUA_MASKCALL, 1);
    lua_sethook(gL, SignalHandlerHookRet, LUA_MASKRET, 1);
}

extern "C" int lrealstart(lua_State *L, int second, const char *file) {
    // 打开文件
    if (!g_push_fp){
        g_push_fp = fopen("./push_log.txt", "w+");
        fprintf(g_push_fp, "open file...\n");
    }
    
    // 运行参数
    if (g_run) {
        return -1;
    }

    // 初始要用到的数据
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
