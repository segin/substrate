%{
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int yylex(void);
void yyerror(const char *s);

extern time_t parsed_time;
extern time_t base_time;

%}

%union {
    int ival;
    struct {
        int hour;
        int min;
    } time_val;
}

%token NOW TEATIME NOON MIDNIGHT TOMORROW
%token PLUS AM PM
%token MINUTES HOURS DAYS WEEKS MONTHS YEARS
%token <ival> MONTH
%token <ival> NUMBER
%token <time_val> TIME

%type <ival> unit
%type <ival> offset

%%

start:
      timespec
    ;

timespec:
      NOW { parsed_time = base_time; }
    | TEATIME {
        struct tm *info = localtime(&base_time);
        info->tm_hour = 16;
        info->tm_min = 0;
        info->tm_sec = 0;
        parsed_time = mktime(info);
        if (parsed_time < base_time) {
            info->tm_mday++;
            parsed_time = mktime(info);
        }
      }
    | NOON {
        struct tm *info = localtime(&base_time);
        info->tm_hour = 12;
        info->tm_min = 0;
        info->tm_sec = 0;
        parsed_time = mktime(info);
        if (parsed_time < base_time) {
            info->tm_mday++;
            parsed_time = mktime(info);
        }
      }
    | MIDNIGHT {
        struct tm *info = localtime(&base_time);
        info->tm_hour = 0;
        info->tm_min = 0;
        info->tm_sec = 0;
        parsed_time = mktime(info);
        if (parsed_time < base_time) {
            info->tm_mday++;
            parsed_time = mktime(info);
        }
      }
    | TOMORROW {
        struct tm *info = localtime(&base_time);
        info->tm_mday++;
        parsed_time = mktime(info);
      }
    | NOW PLUS offset { parsed_time = base_time + $3; }
    | TIME {
        struct tm *info = localtime(&base_time);
        info->tm_hour = $1.hour;
        info->tm_min = $1.min;
        info->tm_sec = 0;
        parsed_time = mktime(info);
        if (parsed_time < base_time) {
            info->tm_mday++;
            parsed_time = mktime(info);
        }
    }
    | TIME AM {
        struct tm *info = localtime(&base_time);
        info->tm_hour = ($1.hour == 12) ? 0 : $1.hour;
        info->tm_min = $1.min;
        info->tm_sec = 0;
        parsed_time = mktime(info);
        if (parsed_time < base_time) {
            info->tm_mday++;
            parsed_time = mktime(info);
        }
    }
    | TIME PM {
        struct tm *info = localtime(&base_time);
        info->tm_hour = ($1.hour == 12) ? 12 : $1.hour + 12;
        info->tm_min = $1.min;
        info->tm_sec = 0;
        parsed_time = mktime(info);
        if (parsed_time < base_time) {
            info->tm_mday++;
            parsed_time = mktime(info);
        }
    }
    | MONTH NUMBER {
        struct tm *info = localtime(&base_time);
        info->tm_mon = $1;
        info->tm_mday = $2;
        info->tm_hour = 0;
        info->tm_min = 0;
        info->tm_sec = 0;
        parsed_time = mktime(info);
        if (parsed_time < base_time) {
            info->tm_year++;
            parsed_time = mktime(info);
        }
    }
    | TIME MONTH NUMBER {
        struct tm *info = localtime(&base_time);
        info->tm_hour = $1.hour;
        info->tm_min = $1.min;
        info->tm_sec = 0;
        info->tm_mon = $2;
        info->tm_mday = $3;
        parsed_time = mktime(info);
        if (parsed_time < base_time) {
            info->tm_year++;
            parsed_time = mktime(info);
        }
    }
    | TIME AM MONTH NUMBER {
        struct tm *info = localtime(&base_time);
        info->tm_hour = ($1.hour == 12) ? 0 : $1.hour;
        info->tm_min = $1.min;
        info->tm_sec = 0;
        info->tm_mon = $3;
        info->tm_mday = $4;
        parsed_time = mktime(info);
        if (parsed_time < base_time) {
            info->tm_year++;
            parsed_time = mktime(info);
        }
    }
    | TIME PM MONTH NUMBER {
        struct tm *info = localtime(&base_time);
        info->tm_hour = ($1.hour == 12) ? 12 : $1.hour + 12;
        info->tm_min = $1.min;
        info->tm_sec = 0;
        info->tm_mon = $3;
        info->tm_mday = $4;
        parsed_time = mktime(info);
        if (parsed_time < base_time) {
            info->tm_year++;
            parsed_time = mktime(info);
        }
    }
    ;

offset:
      NUMBER unit { $$ = $1 * $2; }
    ;

unit:
      MINUTES { $$ = 60; }
    | HOURS   { $$ = 3600; }
    | DAYS    { $$ = 86400; }
    | WEEKS   { $$ = 604800; }
    ;

%%

void yyerror(const char *s) {
    (void)s;
}
