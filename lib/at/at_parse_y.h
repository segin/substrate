#ifndef YYTOKENTYPE
#define YYTOKENTYPE
#define error 256
#define NOW 257
#define TEATIME 258
#define NOON 259
#define MIDNIGHT 260
#define TOMORROW 261
#define PLUS 262
#define AM 263
#define PM 264
#define MINUTES 265
#define HOURS 266
#define DAYS 267
#define WEEKS 268
#define MONTHS 269
#define YEARS 270
#define MONTH 271
#define NUMBER 272
#define TIME 273
#endif

#ifndef YYSTYPE
typedef union {

    int ival;
    struct {
        int hour;
        int min;
    } time_val;

} YYSTYPE;
#endif
extern YYSTYPE yylval;
