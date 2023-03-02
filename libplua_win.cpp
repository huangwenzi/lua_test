#include <string>
#include <list>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <iostream>
#include <cstdlib>
#include <cstring>
#include <typeinfo>
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
#include<windows.h>
// #include<sys/time.h>
#include "src/lua.h"
#include "src/lualib.h"
#include "src/lauxlib.h"
#include "src/ldebug.c"

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
// 用于排序的结构
struct TimeFun {
    long long time_us;      // 耗时
    char map_key[255];      // 函数名
};

// 全局变量
int g_push_count = 0;             // 保存次数
FILE *g_push_fp = NULL;           // 文件对象
#define MAP_RUN_STRUCT map<std::string, long long>
MAP_RUN_STRUCT g_map_run_time;    // 函数运行时间 <函数名, 运行时间(毫秒)>
long long g_time_us;            // 上次添加的时间
char g_last_map_key[255];       // 上次map_key
bool g_stop = false;            // 是否停止



// 获取当前时间
static long long get_time_us(){
    LARGE_INTEGER nFreq;
    LARGE_INTEGER t1;
    double dt;
    QueryPerformanceFrequency(&nFreq);
    QueryPerformanceCounter(&t1);
    // long long time_us = t1.QuadPart/(double)nFreq.QuadPart;
    long long time_us = t1.QuadPart;
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
        sprintf(map_key, "%s-%s-%s", ar->what, ar->source, ar->name);
        MAP_RUN_STRUCT::iterator iter;
        // 上次时间
        long long time_us = get_time_us();
        iter = g_map_run_time.find(map_key);
        if(iter != g_map_run_time.end()){
            iter->second += time_us - g_time_us;
        }else{
            g_map_run_time[map_key] = time_us - g_time_us;
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
    for(MAP_RUN_STRUCT::iterator iter = g_map_run_time.begin();iter!=g_map_run_time.end();iter++){
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

static int lastlevel(lua_State *L) {
    lua_Debug ar;
    int li = 1, le = 1;
    /* find the bottom index of the call stack. */
    while (lua_getstack(L, le, &ar)) {
        li = le;
        le *= 2;
    }
    /* do a binary search */
    while (li < le) {
        int m = (li + le) / 2;
        if (lua_getstack(L, m, &ar)) li = m + 1;
        else le = m;
    }
    return le - 1;
}

extern "C" int lrealstop(lua_State *L) {
    lua_sethook(L, 0, 0, 0);
    g_stop = true;
    // struct itimerval timer;
    // timer.it_interval.tv_sec = 0;
    // timer.it_interval.tv_usec = 0;
    // timer.it_value = timer.it_interval;
    // int ret = setitimer(ITIMER_PROF, &timer, NULL);
    // if (ret != 0) {
    //     return ret;
    // }

    // 关闭log
    push_map_info();
    fclose(g_push_fp);
    return 0;
}

VOID APIENTRY TimerAPCRoutine( PVOID null,DWORD dwTimerLowValue,DWORD dwTimerHighValue){
    lua_sethook(gL, 0, 0, 0);

    lua_Debug ar;
    int last = lastlevel(gL);
    int i = 0;
    CallStack cs;
    cs.depth = 0;

    // lua_getstack 获取第last级堆栈，错误返回0  否者返回1
    while (lua_getstack(gL, last, &ar) && i < MAX_STACK_SIZE) {
        // lua_getinfo 依赖lua_getstack
        lua_getinfo(gL, "Slnt", &ar);
        push_run(gL, &ar);   // 存一次数据
        const char *funcname = lua_tostring(gL, -1);
        lua_pop(gL, 1);

        i++;
        last--;
    }
}

extern "C" int lrealstart(lua_State *L, int second, const char *file) {
    // 打开文件
    if (!g_push_fp){
        g_push_fp = fopen("./push_log.txt", "w+");
        fprintf(g_push_fp, "open file...\n");
    }
    // 初始要用到的数据
    g_time_us = get_time_us();
    
    // 运行参数
    if (g_stop) {
        return -1;
    }
    g_stop = true;
    const int iter = 10;
    gL = L;

    HANDLE hTimer = CreateWaitableTimer(NULL, FALSE, NULL);
    LARGE_INTEGER li;
    li.QuadPart = 0;
    if (!SetWaitableTimer(hTimer, &li, 1, TimerAPCRoutine, NULL, FALSE)) {
        CloseHandle(hTimer);
        return 0;
    }

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
